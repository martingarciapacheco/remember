#pragma once
#include <JuceHeader.h>

// Captures N seconds of audio from the master bus and encodes it to OGG.
// N, OGG quality and mono/stereo are configurable from the browser app and
// snapshotted into members at every prepare() call (so the audio thread
// never touches the config file).
// Trigger: DAW transport starts playing from near position 0.
// Encoding + key detection run on a background thread — audio callback never blocked.
class RecordingEngine : public juce::Thread
{
public:
    enum class State { Idle, Recording, Encoding, Done, Error };

    RecordingEngine();
    ~RecordingEngine() override;

    void prepare (double sampleRate, int maxBlockSize);

    // Call from AudioProcessor::processBlock — never blocks
    void processBlock (const juce::AudioBuffer<float>& buffer,
                       juce::AudioPlayHead*            playHead);

    void setOutputFolder  (const juce::File&   folder);
    void setProjectName   (const juce::String& name);

    // Reads the projectName field from this entry's metadata.json (if it exists).
    // Used to pick up renames the user made from the browser app.
    juce::String readPersistedProjectName() const;

    // Updates the projectName field inside the existing metadata.json (if any)
    // without touching the other fields. Used when the user renames inside the
    // plugin so the browser app sees the change immediately.
    void writePersistedProjectName (const juce::String& name) const;

    // Exposed so PluginProcessor can derive a folder path the same way.
    static juce::String sanitizeFolderNamePublic (const juce::String& name)
        { return sanitizeFolderName (name); }

    State        getState()          const noexcept { return state.load(); }
    float        getProgress()       const noexcept;
    juce::String getDetectedKey()    const;
    double       getDetectedBpm()    const noexcept { return capturedBpm.load(); }
    juce::String getLastSavedTime()  const;
    int          getTotalSeconds()   const noexcept { return (int) currentRecordSeconds; }

private:
    void run() override;
    void startCapture (double bpm);
    void encodeAndSave();
    void saveMetadata (const juce::File& projectFolder, const juce::String& key);
    juce::String detectProjectFilePath() const;
    static juce::String sanitizeFolderName (const juce::String& name);

    static constexpr float kTriggerThreshold = 2.0f;

    // Defaults if config is missing or invalid. Live values come from
    // SettingsManager and are snapshotted in prepare().
    static constexpr float kDefaultRecordSeconds = 60.0f;
    static constexpr int   kDefaultOggQuality    = 4;       // ~128 kbps

    float currentRecordSeconds { kDefaultRecordSeconds };
    int   currentOggQuality    { kDefaultOggQuality };
    bool  currentRecordMono    { false };

    std::atomic<State> state { State::Idle };
    std::atomic<int>   samplesWritten { 0 };
    int                samplesToCapture { 0 };

    double                   currentSampleRate { 44100.0 };
    juce::AudioBuffer<float> captureBuffer;

    std::atomic<bool>   wasPlayingNearStart { false };
    std::atomic<bool>   wasPlaying          { false };
    std::atomic<double> capturedBpm { 0.0 };

    juce::File   outputFolder;
    juce::String projectName;

    // Detected once per recording, used in saveMetadata
    juce::String detectedProjectFilePath;
    juce::String detectedDaw;

    mutable juce::CriticalSection resultsLock;
    juce::String detectedKey;
    juce::String lastSavedTime;

    juce::WaitableEvent encodeTrigger { true };
};
