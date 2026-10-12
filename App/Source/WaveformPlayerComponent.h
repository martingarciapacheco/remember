#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "ProjectScanner.h"
#include "ReadAheadSource.h"
#include "WaveformEnvelope.h"
#include "PlaybackQueue.h"

class WaveformPlayerComponent : public juce::Component,
                                public juce::ChangeListener,
                                public juce::Timer
{
public:
    explicit WaveformPlayerComponent (juce::AudioDeviceManager* externalDevice = nullptr, bool persistDeviceState = true);
    ~WaveformPlayerComponent() override;

    void loadEntry (const ProjectEntry& entry);
    void releaseEntry (const juce::File& metadataFile);
    void stop();
    void play();         // start playback if a preview is loaded
    void setQueue (const juce::Array<ProjectEntry>& entries)
    { queue.setEntries (entries); nextBtn.setEnabled (! entries.isEmpty()); previousBtn.setEnabled (! entries.isEmpty()); }
    std::function<void (const ProjectEntry&)> onEntryLoaded;
    void next (bool automatic = false);
    void previous();
    void setVolume (double value);
    double getVolume() const { return volumeSlider.getValue(); }
    void setMuted (bool);
    bool isMuted() const { return muteBtn.getToggleState(); }
    void setNormalized (bool);
    float getOutputGain() const { return transport.getGain(); }
    bool autoplaySelection() const { return autoplay; }
    void togglePlay();   // public wrapper for keyboard shortcut
    bool hasLoadedEntry() const noexcept { return isLoaded; }
    juce::String getPlaybackError() const { return playbackError; }
    bool isAudioDeviceOpen() const noexcept { return audioDeviceOpen; }
    uint64_t getUnderruns() const noexcept { return readerSource ? readerSource->underruns() : 0; }
    static int pendingLoads();
    bool isPlaying() const { return transport.isPlaying(); }
    void setUiActive (bool);

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

private:
    RememberUI::Style uiStyle { *this };
    PlaybackQueue queue;
    juce::TextButton previousBtn, nextBtn, shuffleBtn, repeatBtn, muteBtn, normalizeBtn, playbackOptionsBtn { "..." };
    juce::Slider volumeSlider;
    bool normalize = false, autoplay = true, applyingPreferences = false, preferencesTouched = false;
    void applyOutputGain();
    void savePlaybackPreferences();
    void showPlaybackOptions();
    void   seekTo (double normalizedPos);
    double getPositionNormalized() const;
    void   updateTimeLabel();
    void   updatePathLabel();
    bool   ensureAudioDevice();
    void   detachSource();
    void   releaseAudioDevice();
    void   armIdleRelease();
    void   showAudioSettings();
    void   handleDeviceFailure (const juce::String&);
    void   clearDeviceError();
    double duration() const;
    double position() const;

    juce::AudioDeviceManager        deviceManager;
    juce::AudioDeviceManager&       outputManager;
    juce::AudioTransportSource      transport;
    struct MonitoredPlayer : juce::AudioSourcePlayer
    {
        std::function<void(juce::String)> onError;
        void audioDeviceError (const juce::String& error) override { if (onError) onError (error); }
    } sourcePlayer;
    std::unique_ptr<ReadAheadSource> readerSource;
    std::shared_ptr<const WaveformEnvelope> waveform;
    std::unique_ptr<juce::FileChooser>             fileChooser;

    ProjectEntry currentEntry;
    bool         isLoaded { false };
    bool         isLoading { false }, playWhenLoaded { false }, audioDeviceOpen { false };
    bool         waitingForBuffer = false;
    bool         uiActive = true, persistAudioState = true, deviceSettingsLoaded = false, openSettingsWhenReady = false;
    juce::uint32 idleSince = 0;
    uint64_t reportedUnderruns = 0;
    juce::String savedAudioState, lastSavedAudioState;
    juce::Component::SafePointer<juce::DialogWindow> audioDialog;
    juce::String playbackError, previewError;
    bool playbackErrorFromDevice = false;
    std::shared_ptr<std::atomic<uint64_t>> loadGeneration { std::make_shared<std::atomic<uint64_t>> (0) };

    // Buttons
    juce::TextButton playBtn   { ">" };
    juce::TextButton openBtn   { "Open in DAW" };
    juce::TextButton assignBtn { "Assign path" };
    juce::TextButton audioBtn { "Audio" };

    juce::Label nameLabel;
    juce::Label pathLabel;
    juce::Label timeLabel;

    // Waveform area cached bounds (set in resized)
    juce::Rectangle<int> waveArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPlayerComponent)
};
