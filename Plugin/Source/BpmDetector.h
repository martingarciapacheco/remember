#pragma once
#include <JuceHeader.h>

// Estimates BPM from an audio buffer using spectral-flux onset detection
// + autocorrelation in the 60–200 BPM range. Returns 0.0 if estimation fails.
class BpmDetector
{
public:
    static double detectBpm (const juce::AudioBuffer<float>& buffer, double sampleRate);
};
