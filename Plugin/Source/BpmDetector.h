#pragma once
#include <JuceHeader.h>
#include "AnalysisAudio.h"

// Estimates BPM from an audio buffer using spectral-flux onset detection
// + lag/Fourier periodicity in the 40–240 BPM range. Channels are analysed separately
// at a fixed sample rate. Ambiguous/non-rhythmic audio returns an unknown tempo.
class BpmDetector
{
public:
    struct Estimate
    {
        double bpm = 0, confidence = 0, alternative = 0;
        double startBpm = 0, endBpm = 0;
        bool possibleVariation = false;
    };
    struct Frame { double energy = 0, magnitude = 0, flux = 0; };
    struct Features
    {
        std::vector<Frame> frames;
        int samples = 0, channels = 0, computed = 0, reused = 0;
        bool valid = false;
    };
    // Flux depends on this FFT window and the previous one. The reuse
    // boundary includes that extra hop; global tempo/confidence is recomputed.
    static int requiredSamples (int totalSamples, int unchangedFrom, bool reuse);
    static Features extract (const juce::AudioBuffer<float>& prefix, int totalSamples,
                             const Features* previous = nullptr, int unchangedFrom = 0,
                             const AnalysisAudio::Cancellation& = {});
    static Estimate estimate (const Features&, const AnalysisAudio::Cancellation& = {});
    static Estimate analyse (const juce::AudioBuffer<float>&, double sampleRate,
                             const AnalysisAudio::Cancellation& = {});
    static double detectBpm (const juce::AudioBuffer<float>& buffer, double sampleRate);
};
