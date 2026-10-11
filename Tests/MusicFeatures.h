#pragma once
#include "../Plugin/Source/KeyDetector.h"
#include "../Plugin/Source/BpmDetector.h"

// Calibration data only; never linked into the plugin/app product targets.
// Stores original chroma/flux and interpolated spectral peaks from the same
// canonical PCM, so prototypes can be compared without another decode/SRC.
juce::File exportMusicFeatures (const juce::AudioBuffer<float>&,
                                const KeyDetector::Features&,
                                const BpmDetector::Features&,
                                const juce::File& report);
void testMusicFeatures (const juce::File& root);
