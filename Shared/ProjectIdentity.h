#pragma once
#include <JuceHeader.h>

// Names are presentation only. A preview belongs to a stable ID and a verified
// DAW file, never to whichever file happens to share its display name.
struct HostProject
{
    bool available = false; // false while a host window is temporarily unavailable
    juce::String name;
    juce::String path;
    juce::String daw;
};

namespace ProjectIdentity
{
    HostProject parseTitle (const juce::String& title, const juce::String& daw);
    HostProject detectHostProject();
    juce::String fileId (const juce::File& file);
    juce::String fingerprint (const juce::File& file);
    juce::String resolve (const juce::String& name, const juce::String& daw,
                          const juce::Array<juce::File>& roots,
                          const juce::String& previousPath = {},
                          const juce::String& previousFileId = {},
                          const juce::String& previousFingerprint = {});
    juce::Array<juce::File> searchRoots();
}
