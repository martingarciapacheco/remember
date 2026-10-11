#pragma once
#include "KeyDetector.h"
#include "BpmDetector.h"

// One analysis worker owns this cache. It retains compact FFT features, never
// PCM or a decoder. Publication is acknowledged by the encoder after commit.
class PreviewAnalysis
{
public:
    static constexpr int version = 5;
    static constexpr size_t instanceCacheLimit = 512 * 1024;
    static constexpr size_t processCacheLimit = 16 * 1024 * 1024;
    struct Publication
    {
        juce::String pcmFingerprint;
        std::atomic<bool> committed { false };
    };
    struct Source
    {
        juce::File pcm;
        juce::String revision, previousRevision, previousFingerprint;
        double sampleRate = 0;
        int samples = 0, channels = 2, changedPrefix = 0;
        bool hostBpm = false;
    };
    struct Result
    {
        KeyDetector::Estimate key;
        BpmDetector::Estimate bpm;
        bool valid = false;
        int inputSamples = 0, keyComputed = 0, keyReused = 0, bpmComputed = 0, bpmReused = 0;
        size_t cacheBytes = 0;
        juce::String error;
    };
    explicit PreviewAnalysis (size_t retentionLimit = instanceCacheLimit);
    ~PreviewAnalysis();
    Result analyse (const Source&, std::shared_ptr<Publication>, const AnalysisAudio::Cancellation& = {});
    static size_t processCacheBytes();
private:
    struct Cache;
    const size_t retentionLimit;
    std::unique_ptr<Cache> cache;
    JUCE_DECLARE_NON_COPYABLE (PreviewAnalysis)
};
