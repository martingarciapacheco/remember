#pragma once
#include <JuceHeader.h>

// Detects the musical key of an audio buffer using a chromagram
// and the Krumhansl-Schmuckler key-finding algorithm.
// All computation happens on the calling thread — run from a background thread.
class KeyDetector
{
public:
    static juce::String detectKey (const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    static std::array<float, 12> computeChroma (const float* samples, int numSamples, double sampleRate);
    static juce::String          findBestKey    (const std::array<float, 12>& chroma);
    static float                 pearson        (const std::array<float, 12>& a,
                                                 const std::array<float, 12>& b);
};
