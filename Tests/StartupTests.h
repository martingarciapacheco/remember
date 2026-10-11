#pragma once
#include <JuceHeader.h>
void testColdStartupMatrix (const juce::File& root);
void testColdStartupCase (const juce::File& root, int count, int missingPercent, const juce::String& mode,
                          const juce::File& sharedLibrary = {}, const juce::File& sharedProjects = {});
