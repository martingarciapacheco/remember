#pragma once
#include <JuceHeader.h>

// Manages shared config at %APPDATA%/Remember/config.json.
// Both the plugin and the browser app read/write this file.
class SettingsManager
{
public:
    static SettingsManager& getInstance();

    juce::File getPreviewsFolder() const;
    void       setPreviewsFolder (const juce::File& folder);
    bool       isConfigured() const;

private:
    SettingsManager();
    void load();
    void save();

    juce::File configFile;
    juce::File previewsFolder;
};
