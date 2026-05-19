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

    // Recording parameters — read live from config.json on every call (cheap;
    // only invoked at plugin prepare time, never on the audio loop).
    // Defaults match the previously hard-coded constants so existing users see
    // no behaviour change until they explicitly opt in.
    int  readRecordSeconds() const;   // default 60
    int  readOggQuality()    const;   // default 4 (Vorbis index, ~128 kbps)
    bool readRecordMono()    const;   // default false (stereo)

private:
    SettingsManager();
    void load();
    void save();

    juce::File configFile;
    juce::File previewsFolder;
};
