#include "FeatureCacheTests.h"
#include "../Plugin/Source/PreviewAnalysis.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check (bool ok, const juce::String& message) { if (! ok) throw std::runtime_error (message.toStdString()); }
juce::AudioBuffer<float> music (double rate, double seconds, bool different = false, int channels = 2)
{
    juce::AudioBuffer<float> audio (channels, (int) std::round (rate * seconds));
    const auto root = different ? 311.1269837 : 261.6255653;
    const auto third = root * std::pow (2.0, (different ? 3 : 4) / 12.0), fifth = root * std::pow (2.0, 7.0 / 12);
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto t = i / rate;
        const auto beat = std::fmod (juce::jmax (0.0, t - 0.2), 60.0 / (different ? 150 : 120));
        const auto click = t >= 0.2 && beat < 0.02 ? 0.15 * std::exp (-beat / 0.004) * std::cos (juce::MathConstants<double>::twoPi * 1500 * beat) : 0;
        const auto value = (float) (0.12 * std::sin (juce::MathConstants<double>::twoPi * root * t)
            + 0.07 * std::sin (juce::MathConstants<double>::twoPi * third * t)
            + 0.09 * std::sin (juce::MathConstants<double>::twoPi * fifth * t) + click);
        audio.setSample (0, i, value);
        if (channels == 2) audio.setSample (1, i, -value);
    }
    return audio;
}
void write (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
        .withNumChannels (audio.getNumChannels()).withBitsPerSample (32)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    check (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "Cannot write feature-cache fixture");
}
PreviewAnalysis::Source source (const juce::File& file, double rate, int samples, int channels = 2)
{
    return { file, juce::Uuid().toString(), {}, {}, rate, samples, channels, samples, false };
}
using Publication = std::shared_ptr<PreviewAnalysis::Publication>;
Publication publication() { return std::make_shared<PreviewAnalysis::Publication>(); }
void publish (const Publication& token, const juce::File& pcm)
{
    token->pcmFingerprint = ProjectIdentity::fingerprint (pcm);
    check (token->pcmFingerprint.length() == 64, "Cannot fingerprint published fixture");
    token->committed.store (true, std::memory_order_release);
}
void same (const PreviewAnalysis::Result& cached, const PreviewAnalysis::Result& full)
{
    check (cached.valid && full.valid && cached.key.key == full.key.key && cached.key.confidence == full.key.confidence
        && cached.key.correlation == full.key.correlation && cached.key.margin == full.key.margin
        && cached.bpm.bpm == full.bpm.bpm && cached.bpm.confidence == full.bpm.confidence
        && cached.bpm.alternative == full.bpm.alternative && cached.bpm.possibleVariation == full.bpm.possibleVariation
        && cached.bpm.startBpm == full.bpm.startBpm && cached.bpm.endBpm == full.bpm.endBpm,
        "Cached key/tempo/confidence differs from complete analysis");
}
PreviewAnalysis::Result full (const PreviewAnalysis::Source& s)
{
    const auto audio = AnalysisAudio::fromFile (s.pcm, s.channels);
    PreviewAnalysis::Result result;
    result.valid = audio.getNumSamples() > 0;
    result.key = KeyDetector::analyse (audio, AnalysisAudio::rate);
    if (! s.hostBpm) result.bpm = BpmDetector::analyse (audio, AnalysisAudio::rate);
    return result;
}
void frameEquality (const PreviewAnalysis::Source& before, const PreviewAnalysis::Source& after)
{
    const auto old = AnalysisAudio::fromFile (before.pcm, before.channels), all = AnalysisAudio::fromFile (after.pcm, after.channels);
    const auto oldKey = KeyDetector::extract (old, old.getNumSamples());
    const auto oldBpm = BpmDetector::extract (old, old.getNumSamples());
    const auto expectedKey = KeyDetector::extract (all, all.getNumSamples());
    const auto expectedBpm = BpmDetector::extract (all, all.getNumSamples());
    const auto unchanged = AnalysisAudio::unchangedOutputSample (after.changedPrefix, after.sampleRate);
    const auto required = juce::jmax (KeyDetector::requiredSamples (all.getNumSamples(), unchanged, true),
                                     BpmDetector::requiredSamples (all.getNumSamples(), unchanged, true));
    const auto prefix = AnalysisAudio::fromFilePrefix (after.pcm, after.channels, required);
    const auto key = KeyDetector::extract (prefix, all.getNumSamples(), &oldKey, unchanged);
    const auto bpm = BpmDetector::extract (prefix, all.getNumSamples(), &oldBpm, unchanged);
    check (key.valid && bpm.valid && key.reused > 0 && bpm.reused > 0 && key.frames == expectedKey.frames,
           "Cached chroma windows differ across the sinc/crossfade boundary");
    check (bpm.frames.size() == expectedBpm.frames.size(), "Cached onset duration differs");
    for (size_t i = 0; i < bpm.frames.size(); ++i)
        check (bpm.frames[i].energy == expectedBpm.frames[i].energy && bpm.frames[i].magnitude == expectedBpm.frames[i].magnitude
            && bpm.frames[i].flux == expectedBpm.frames[i].flux, "Cached spectral flux used stale FFT history");
}
void componentMatrix (const juce::File& root)
{
    for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        const auto old = music (rate, 12), replacement = music (rate, 12, true);
        auto merged = old;
        const auto prefix = (int) std::round (rate * 1.21347), fade = (int) std::round (rate * 0.005);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < prefix; ++i)
            {
                const auto mix = i < prefix - fade ? 0.0f : (float) (i - (prefix - fade)) / (fade - 1);
                const auto value = replacement.getSample (ch, i);
                merged.setSample (ch, i, value + mix * (old.getSample (ch, i) - value));
            }
        const auto aFile = root.getChildFile ("a-" + juce::String ((int) rate) + ".wav");
        const auto bFile = root.getChildFile ("b-" + juce::String ((int) rate) + ".wav"); write (aFile, old, rate); write (bFile, merged, rate);
        const auto a = source (aFile, rate, old.getNumSamples());
        auto b = source (bFile, rate, old.getNumSamples()); b.changedPrefix = prefix;
        PreviewAnalysis cache; auto token = publication();
        const auto first = cache.analyse (a, token); same (first, full (a)); publish (token, aFile);
        b.previousRevision = a.revision; b.previousFingerprint = token->pcmFingerprint;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto cached = cache.analyse (b, publication());
        const auto cacheMs = juce::Time::getMillisecondCounterHiRes() - started;
        PreviewAnalysis uncached (0); const auto fullStarted = juce::Time::getMillisecondCounterHiRes();
        const auto recomputed = uncached.analyse (b, publication());
        const auto fullMs = juce::Time::getMillisecondCounterHiRes() - fullStarted;
        same (cached, recomputed); same (cached, full (b)); frameEquality (a, b);
        check (cached.keyReused > 0 && cached.bpmReused > 0 && cached.inputSamples < (int) (12 * 22050 / 5)
            && cached.cacheBytes > 0 && cached.cacheBytes <= PreviewAnalysis::instanceCacheLimit
            && recomputed.keyReused == 0 && recomputed.bpmReused == 0 && recomputed.cacheBytes == 0,
            "Partial recording did not avoid processing the intact tail");
        std::cout << "FEATURE rate=" << rate << " inputSamples=" << cached.inputSamples << "/" << recomputed.inputSamples
            << " keyFrames=" << cached.keyComputed << "/" << cached.keyReused << " bpmFrames=" << cached.bpmComputed << "/" << cached.bpmReused
            << " cacheBytes=" << cached.cacheBytes << " cachedMs=" << cacheMs << " fullMs=" << fullMs << '\n';
    }
    std::cout << "PASS: five sample rates, changed musical prefix/crossfade, exact chroma/flux windows and exact cached/full estimates\n";
}
void invalidation (const juce::File& root)
{
    const auto a = source (root.getChildFile ("a-48000.wav"), 48000, 48000 * 12);
    const auto changed = source (root.getChildFile ("b-48000.wav"), 48000, 48000 * 12);
    const auto monoFile = root.getChildFile ("mono.wav"), shortFile = root.getChildFile ("short.wav");
    write (monoFile, music (48000, 12, true, 1), 48000); write (shortFile, music (48000, 6, true), 48000);
    for (int scenario = 0; scenario < 7; ++scenario)
    {
        PreviewAnalysis cache; auto token = publication();
        auto seed = a; seed.hostBpm = scenario == 6;
        check (cache.analyse (seed, token).valid, "Cannot seed invalidation fixture");
        if (scenario != 0) publish (token, a.pcm);
        auto b = changed; b.previousRevision = a.revision; b.previousFingerprint = ProjectIdentity::fingerprint (a.pcm); b.changedPrefix = 58247;
        if (scenario == 1) b.previousFingerprint = juce::String::repeatedString ("0", 64);
        if (scenario == 2) b.previousRevision = juce::Uuid().toString();
        if (scenario == 3) { b = source (root.getChildFile ("b-44100.wav"), 44100, 44100 * 12); b.previousRevision = a.revision; b.previousFingerprint = token->pcmFingerprint; b.changedPrefix = 53500; }
        if (scenario == 4) { b.pcm = monoFile; b.channels = 1; }
        if (scenario == 5) { b.pcm = shortFile; b.samples = 48000 * 6; }
        const auto result = cache.analyse (b, publication()); same (result, full (b));
        check (result.bpmReused == 0 && (scenario == 6 ? result.keyReused > 0 : result.keyReused == 0),
               "Unpublished/stale/changed-format or host-only features were reused");
    }
    PreviewAnalysis cache; auto token = publication(); check (cache.analyse (a, token).valid, "Cannot seed cancellation fixture"); publish (token, a.pcm);
    auto b = changed; b.previousRevision = a.revision; b.previousFingerprint = token->pcmFingerprint; b.changedPrefix = 58247;
    int visits = 0;
    check (! cache.analyse (b, publication(), [&] { return ++visits > 500; }).valid && visits <= 502,
           "Cached extraction ignored cancellation");
    const auto retry = cache.analyse (b, publication()); same (retry, full (b));
    check (retry.keyReused > 0 && retry.bpmReused > 0, "Cancelled extraction replaced the previously published cache");
    for (int mismatch = 0; mismatch < 3; ++mismatch)
    {
        auto invalid = b;
        if (mismatch == 0) invalid.samples /= 2;
        if (mismatch == 1) invalid.sampleRate = 96000;
        if (mismatch == 2) invalid.channels = 1;
        const auto rejected = cache.analyse (invalid, publication());
        check (! rejected.valid && rejected.error.contains ("PCM header"), "Incorrect PCM format metadata was analysed or cached");
    }
    std::cout << "PASS: unpublished revision, checksum/UUID, rate/channels/length, host BPM transition, cancellation and retry\n";
}
void budget (const juce::File& root)
{
    const auto initial = PreviewAnalysis::processCacheBytes();
    const auto file = root.getChildFile ("budget-silence.wav");
    juce::AudioBuffer<float> silence (1, 22050 * 120); silence.clear(); write (file, silence, 22050);
    const auto s = source (file, 22050, silence.getNumSamples(), 1);
    std::vector<std::unique_ptr<PreviewAnalysis>> caches;
    int declined = 0;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 58; ++i)
    {
        auto cache = std::make_unique<PreviewAnalysis>(); const auto result = cache->analyse (s, publication());
        check (result.valid && result.key.key == "Unknown" && result.bpm.bpm == 0, "Cache exhaustion invalidated complete analysis");
        if (result.cacheBytes == 0) ++declined;
        check (result.cacheBytes <= PreviewAnalysis::instanceCacheLimit && PreviewAnalysis::processCacheBytes() <= PreviewAnalysis::processCacheLimit,
               "Feature retention exceeded its process/instance budget");
        caches.push_back (std::move (cache));
    }
    check (declined > 0 && PreviewAnalysis::processCacheBytes() > initial, "Fixture did not exhaust the actual global feature budget");
    const auto peak = PreviewAnalysis::processCacheBytes(); caches.clear();
    check (PreviewAnalysis::processCacheBytes() == initial, "Feature budget was not released on destruction");
    PreviewAnalysis retry; check (retry.analyse (s, publication()).cacheBytes > 0, "Released budget did not allow cache retention again");
    std::cout << "FEATURE budgetPeak=" << peak << " declined=" << declined << " elapsedMs=" << juce::Time::getMillisecondCounterHiRes() - started << '\n';
}
struct Head : juce::AudioPlayHead
{
    bool playing = true; double time = 0;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo info; info.setIsPlaying (playing); info.setTimeInSeconds (time); return info; }
};
void capture (RecordingEngine& engine, const juce::AudioBuffer<float>& audio, double rate, int samples)
{
    Head head;
    for (int offset = 0; offset < samples;)
    {
        const auto count = juce::jmin (512, samples - offset); juce::AudioBuffer<float> block (2, count);
        for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, audio, juce::jmin (ch, audio.getNumChannels() - 1), offset, count);
        head.time = offset / rate; engine.processBlock (block, &head); offset += count;
    }
    head.playing = false; head.time = samples / rate;
    juce::AudioBuffer<float> block (2, 512); block.clear(); engine.processBlock (block, &head);
    const auto until = juce::Time::getMillisecondCounter() + 20000;
    while (engine.getState() != RecordingEngine::State::Pending && engine.getState() != RecordingEngine::State::Error)
    { check (juce::Time::getMillisecondCounter() < until, "Cached engine capture timed out"); juce::Thread::sleep (2); }
    check (engine.getState() == RecordingEngine::State::Pending, "Cached engine failed: " + engine.getLastError());
}
void checkMetadata (const juce::File& folder)
{
    const auto data = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
    auto s = source (LibraryStore::audioFile (folder, true), (double) data.getProperty ("sampleRate", 0), 1, (int) data.getProperty ("channels", 0));
    const auto expected = full (s);
    check ((int) data.getProperty ("analysisVersion", 0) == PreviewAnalysis::version && data.getProperty ("key", "").toString() == expected.key.key
        && std::abs ((double) data.getProperty ("keyConfidence", -1.0) - expected.key.confidence) < 1.0e-10
        && std::abs ((double) data.getProperty ("bpm", -1.0) - expected.bpm.bpm) < 1.0e-10
        && std::abs ((double) data.getProperty ("bpmConfidence", -1.0) - expected.bpm.confidence) < 1.0e-10,
        "Committed cached metadata differs from full analysis of its actual PCM");
}
void integration (const juce::File& root)
{
    {
        SettingsManager::Snapshot settings; settings.previews = root.getChildFile ("engine-cache"); settings.seconds = 15;
        check (settings.previews.createDirectory().wasOk(), "Cannot create engine-cache fixture library");
        RecordingEngine engine ([] { return HostProject{}; }); engine.prepare (48000, 512, settings);
        const auto original = music (48000, 12), replacement = music (48000, 12, true);
        capture (engine, original, 48000, original.getNumSamples());
        const auto folder = settings.previews.getChildFile (".pending").getChildFile (engine.getAssociationInfo().id);
        const auto first = LibraryStore::readJson (folder.getChildFile ("metadata.json")); checkMetadata (folder);
        const auto receipt = LibraryStore::recoverEntry (folder);
        check (receipt.result.wasOk() && ! receipt.repaired && receipt.pcmFingerprint == ProjectIdentity::fingerprint (LibraryStore::audioFile (folder, true)),
               "Verified cache identity does not describe the actual published PCM");
        capture (engine, replacement, 48000, 58247); checkMetadata (folder);
        auto data = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
        check ((int) data.getProperty ("analysisKeyReusedFrames", 0) > 0 && (int) data.getProperty ("analysisBpmReusedFrames", 0) > 0
            && (int) data.getProperty ("analysisInputSamples", 999999) < 12 * 22050 / 5, "Production pipeline did not reuse its published tail features");
        check (LibraryStore::audioFile (folder, true).replaceWithText ("corrupt published PCM fixture"), "Cannot damage cache fixture");
        const auto repaired = LibraryStore::recoverEntry (folder);
        data = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
        check (repaired.result.wasOk() && repaired.repaired && repaired.revision == first.getProperty ("audioRevision", "").toString()
            && data.getProperty ("analysisKeyReusedFrames", {}) == first.getProperty ("analysisKeyReusedFrames", {}),
               "Recovery mixed analysis diagnostics from different revisions");
        for (const auto* field : { "analysisVersion", "analysisInputSamples", "analysisKeyComputedFrames", "analysisKeyReusedFrames",
                                  "analysisBpmComputedFrames", "analysisBpmReusedFrames", "analysisCacheBytes" })
            check (data.getProperty (field, {}) == first.getProperty (field, {}), "Recovery mixed the analysis manifest fields");
        capture (engine, replacement, 48000, 58247); checkMetadata (folder);
        data = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
        check ((int) data.getProperty ("analysisKeyReusedFrames", -1) == 0 && (int) data.getProperty ("analysisBpmReusedFrames", -1) == 0,
               "Recovering an older revision reused features from the damaged revision");
        settings.mono = true; engine.prepare (96000, 512, settings);
        const auto mono = music (96000, 2, true, 1); capture (engine, mono, 96000, 116494); checkMetadata (folder);
        data = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
        check ((int) data.getProperty ("analysisKeyReusedFrames", -1) == 0 && (int) data.getProperty ("analysisBpmReusedFrames", -1) == 0,
               "Production format change reused old features");
    }
    const auto until = juce::Time::getMillisecondCounter() + 15000;
    while (BackgroundRetirement::pending() != 0)
    { check (juce::Time::getMillisecondCounter() < until, "Engine feature cache did not retire"); juce::Thread::sleep (2); }
    std::cout << "PASS: production commit/checksum handoff, partial reuse, gain/analysis recovery, changed mono/rate and retirement\n";
}
}
void testFeatureCache (const juce::File& root)
{
    const auto initial = PreviewAnalysis::processCacheBytes();
    componentMatrix (root); invalidation (root); budget (root); integration (root);
    check (PreviewAnalysis::processCacheBytes() == initial, "Feature caches leaked after all fixtures retired");
}
