#include "PreviewAnalysis.h"

namespace
{
std::atomic<size_t> retainedFeatures { 0 };
bool reserve (size_t bytes)
{
    auto current = retainedFeatures.load();
    do
    {
        if (bytes > PreviewAnalysis::processCacheLimit || current > PreviewAnalysis::processCacheLimit - bytes) return false;
    } while (! retainedFeatures.compare_exchange_weak (current, current + bytes));
    return true;
}
}
struct PreviewAnalysis::Cache
{
    ~Cache() { if (bytes > 0) retainedFeatures.fetch_sub (bytes); }
    Source source;
    std::shared_ptr<Publication> publication;
    KeyDetector::Features key;
    BpmDetector::Features bpm;
    size_t bytes = 0;
};
PreviewAnalysis::PreviewAnalysis (size_t limit) : retentionLimit (juce::jmin (limit, instanceCacheLimit)) {}
PreviewAnalysis::~PreviewAnalysis() = default;
size_t PreviewAnalysis::processCacheBytes() { return retainedFeatures.load(); }
PreviewAnalysis::Result PreviewAnalysis::analyse (const Source& source, std::shared_ptr<Publication> publication,
                                                 const AnalysisAudio::Cancellation& cancelled)
{
    Result result;
    if (! std::isfinite (source.sampleRate) || source.sampleRate <= 0 || source.samples <= 0
        || source.changedPrefix < 0 || source.changedPrefix > source.samples || ! publication
        || source.channels < 1 || source.channels > 2 || (cancelled && cancelled())) return result;
    const auto length = AnalysisAudio::convertedLength (source.samples, source.sampleRate, AnalysisAudio::rate);
    // A checksum is read only after acquiring its publication flag. It is
    // written once, after the manifest commit, and never mutated afterwards.
    const auto compatible = cache && cache->publication->committed.load (std::memory_order_acquire)
        && source.previousRevision.isNotEmpty() && source.previousRevision == cache->source.revision
        && source.previousFingerprint.length() == 64 && source.previousFingerprint == cache->publication->pcmFingerprint
        && source.sampleRate == cache->source.sampleRate && source.samples == cache->source.samples
        && source.channels == cache->source.channels;
    const auto unchanged = AnalysisAudio::unchangedOutputSample (source.changedPrefix, source.sampleRate);
    const auto* previousKey = compatible ? &cache->key : nullptr;
    const auto* previousBpm = compatible && cache->bpm.valid ? &cache->bpm : nullptr;
    const auto required = juce::jmax (KeyDetector::requiredSamples (length, unchanged, previousKey != nullptr),
        source.hostBpm ? 0 : BpmDetector::requiredSamples (length, unchanged, previousBpm != nullptr));
    AnalysisAudio::FileStream input (source.pcm, AnalysisAudio::rate, cancelled);
    if (! input.valid()) { result.error = "Cannot open the PCM analysis source or prepare its sample-rate converter"; return result; }
    if (input.sourceRate() != source.sampleRate || input.sourceLength() != source.samples || input.sourceChannels() != source.channels
        || input.length() != length)
    { result.error = "PCM header differs from the recording format/length; the previous preview is retained"; return result; }
    juce::AudioBuffer<float> audio (source.channels, required), block (2, 4096);
    for (int offset = 0; offset < required;)
    {
        const auto count = juce::jmin (4096, required - offset);
        if (! input.read (block, count)) { result.error = "Cannot read the required PCM analysis prefix"; return result; }
        for (int ch = 0; ch < source.channels; ++ch) audio.copyFrom (ch, offset, block, ch, 0, count);
        offset += count;
    }
    if (cancelled && cancelled()) return result;
    auto key = KeyDetector::extract (audio, length, previousKey, unchanged, cancelled);
    auto bpm = source.hostBpm ? BpmDetector::Features{} : BpmDetector::extract (audio, length, previousBpm, unchanged, cancelled);
    if (! key.valid || (! source.hostBpm && ! bpm.valid) || (cancelled && cancelled())) return result;
    result.key = KeyDetector::estimate (key);
    if (! source.hostBpm) result.bpm = BpmDetector::estimate (bpm, cancelled);
    if (cancelled && cancelled()) return result;
    result.valid = true;
    result.inputSamples = required;
    result.keyComputed = key.computed; result.keyReused = key.reused;
    result.bpmComputed = bpm.computed; result.bpmReused = bpm.reused;
    const auto bytes = key.frames.capacity() * sizeof (std::array<float, 12>) + bpm.frames.capacity() * sizeof (BpmDetector::Frame);
    // Once the new source has been analysed, the previous cache is obsolete.
    // Budget exhaustion only disables retention; analysis is still complete.
    cache.reset();
    if (bytes > 0 && bytes <= retentionLimit)
    {
        auto next = std::make_unique<Cache>();
        next->source = source; next->publication = std::move (publication);
        if (reserve (bytes))
        {
            next->bytes = bytes;
            next->key = std::move (key); next->bpm = std::move (bpm);
            result.cacheBytes = bytes; cache = std::move (next);
        }
    }
    return result;
}
