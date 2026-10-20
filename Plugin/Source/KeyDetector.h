#pragma once
#include <JuceHeader.h>
#include "AnalysisAudio.h"

// Estimates key from interpolated spectral peaks, locally weighted chroma
// and development-fitted profiles. Ambiguous input returns Unknown.
// All computation happens on the calling thread — run from a background thread.
class KeyDetector
{
public:
    struct Estimate
    {
        juce::String key { "Unknown" };
        double confidence = 0, correlation = 0, margin = 0;
    };
    struct Features
    {
        std::vector<std::array<float, 12>> frames;
        int samples = 0, channels = 0, computed = 0, reused = 0;
        bool valid = false;
    };
    // Fixed 22.05 kHz timeline. A cached frame is reusable only when its
    // complete window starts at/after unchangedFrom, supplied by the merger.
    static int requiredSamples (int totalSamples, int unchangedFrom, bool reuse);
    static Features extract (const juce::AudioBuffer<float>& prefix, int totalSamples,
                             const Features* previous = nullptr, int unchangedFrom = 0,
                             const AnalysisAudio::Cancellation& = {});
    static Estimate estimate (const Features&);
    static Estimate analyse (const juce::AudioBuffer<float>&, double sampleRate,
                             const AnalysisAudio::Cancellation& = {});
    static juce::String detectKey (const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    static Estimate findBestKey (const std::array<float, 12>& chroma);
    static float                 pearson        (const std::array<float, 12>& a,
                                                 const std::array<float, 12>& b);
};
