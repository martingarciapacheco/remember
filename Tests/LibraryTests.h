#pragma once
#include <JuceHeader.h>
void testLibraryCache (const juce::File& root);
void testLibraryFailureRecovery (const juce::File& root);
void testLibraryWatch (const juce::File& root);
void benchmarkLibrary (const juce::File& root);
void testSettingsLoad (const juce::File& root);
void testSettingsRecovery (const juce::File& root);
