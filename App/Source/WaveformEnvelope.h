#pragma once
#include <JuceHeader.h>
#include <array>
#include <functional>
#include <vector>

struct WaveformEnvelope
{
    float normalizationGain = 1;
    double loudness = -100, decodedPeak = 0;
    int channelCount = 1;
    struct Peak { float low = 0, high = 0, spectralPower = 0, frequencyMoment = 0; };
    std::array<std::vector<Peak>, 2> channels;
    // Entire file, bounded working buffer and at most 32,768 peaks per channel.
    // Read/open only on a background thread; painting reads this snapshot.
    juce::Result read (const juce::File&, const std::function<bool()>& cancelled);
};
