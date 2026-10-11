#pragma once
#include <JuceHeader.h>

// Offline evaluation only. A completed run reports accuracy, including
// abstentions; it does not turn low musical accuracy into a passing assertion.
void evaluateMusicCorpus (const juce::File& manifest, const juce::File& report, bool features = false);
void testMusicCorpusEvaluation (const juce::File& root);
