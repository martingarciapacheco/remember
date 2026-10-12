#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include "../../Shared/CaptureOptions.h"

// Manages shared config at %APPDATA%/Remember/config.json.
// Both the plugin and the browser app read/write this file.
class SettingsManager
{
public:
    static SettingsManager& getInstance();

    juce::File getPreviewsFolder() const;
    juce::Result setPreviewsFolder (const juce::File& folder);
    bool       isConfigured() const;

    // Recording parameters are read together at preparation, never on audio.
    // Defaults match the previously hard-coded constants so existing users see
    // no behaviour change until they explicitly opt in.
    int  readRecordSeconds() const;   // default 60
    int  readOggQuality()    const;   // default 4 (Vorbis index, ~128 kbps)
    bool readRecordMono()    const;   // default false (stereo)
    struct Snapshot
    {
        juce::File previews;
        int seconds = 60, quality = 4;
        bool mono = false;
        juce::int64 revision = 0, modifiedMs = 0;
        juce::String fingerprint;
        bool darkAppearance = false;
        CaptureOptions capture;
    };
    using Reader = std::function<Snapshot()>;
    Snapshot readSnapshot() const;
    static Snapshot readSnapshot (const juce::File& config);
    // Finite jobs are retained until the native worker finishes, even when the
    // editor closes during a filesystem operation. UI polls the publication;
    // no callback contains an editor pointer or outlives a plugin DLL reference.
    struct Request
    {
        Snapshot snapshot;
        juce::Result result = juce::Result::ok();
        bool configured = false;
        std::atomic<bool> complete { false };
    };
    std::shared_ptr<Request> readSnapshotAsync() const;
    // Preparation uses its own retained job, independently of discovery,
    // encoding and an older constructor/restore read.
    static std::shared_ptr<Request> readSnapshotAsync (Reader);
    std::shared_ptr<Request> setPreviewsFolderAsync (const juce::File&) const;
    std::shared_ptr<Request> setAppearanceAsync (bool dark) const;
    // Only shared lock/replacement; manualCapture is the app's creation default.
    std::shared_ptr<Request> setCaptureOptionsAsync (CaptureOptions) const;

private:
    SettingsManager();

    juce::File configFile;
};
