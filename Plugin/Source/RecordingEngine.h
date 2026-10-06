#pragma once
#include <JuceHeader.h>
#include "../../Shared/ProjectIdentity.h"
#include <array>
#include <functional>

// Host/file discovery and encoding run independently of the editor. Two
// preallocated slots let playback record while the previous OGG is encoded.
class RecordingEngine : public juce::Thread
{
public:
    enum class State { Idle, Recording, Encoding, Done, Pending, Error };
    using ProjectDetector = std::function<HostProject()>;
    explicit RecordingEngine (ProjectDetector = ProjectIdentity::detectHostProject);
    ~RecordingEngine() override;
    void prepare (double sampleRate, int maxBlockSize);
    void finishCapture();
    void processBlock (const juce::AudioBuffer<float>&, juce::AudioPlayHead*, bool offline = false);
    void setOutputFolder (const juce::File&);
    void setProjectName (const juce::String&);
    juce::String getProjectName() const;
    bool isProjectConfirmed() const;
    void saveState (juce::XmlElement&) const;
    void restoreState (const juce::XmlElement&);
    State getState() const noexcept;
    float getProgress() const noexcept;
    juce::String getDetectedKey() const;
    double getDetectedBpm() const noexcept { return capturedBpm.load(); }
    juce::String getLastSavedTime() const;
    int getTotalSeconds() const noexcept { return (int) recordSeconds.load(); }

private:
    enum class SlotState { Free, Capturing, Ready, Encoding };
    struct Slot
    {
        std::atomic<SlotState> state { SlotState::Free };
        juce::AudioBuffer<float> audio;
        int count = 0;
        double bpm = 0;
        uint64_t generation = 0, sequence = 0;
    };
    struct Entry
    {
        juce::String id { juce::Uuid().toString() };
        juce::String name, path, fileId, hash;
        juce::int64 fileStamp = 0;
        bool confirmed = false;
    };
    void run() override;
    void startCapture (double bpm);
    void refreshHostProject();
    void processReadySlots();
    void encodeAndSave (Slot&);
    void updateMetadata();
    juce::File entryFolder() const;
    void adoptExistingEntry (const juce::String& path);
    bool promotePending (const juce::File& oldFolder);
    void removePending();
    ProjectDetector detector;
    std::array<Slot, 2> slots;
    int activeSlot = -1; // only used by the audio callback or a suspended host
    uint64_t nextSequence = 0;
    bool wasPlaying = false;
    double previousTime = -1, previousBlockSeconds = 0, offlineTime = 0;
    double sampleRate = 44100;
    std::atomic<float> recordSeconds { 60 };
    std::atomic<int> samplesToCapture { 0 };
    int oggQuality = 4;
    bool recordMono = false;
    std::atomic<State> state { State::Idle };
    std::atomic<bool> capturing { false };
    std::atomic<int> samplesWritten { 0 };
    std::atomic<double> capturedBpm { 0 };
    std::atomic<uint64_t> generation { 1 };
    juce::WaitableEvent wake;
    // Never acquired by processBlock.
    mutable juce::CriticalSection projectLock;
    juce::File outputFolder;
    Entry entry;
    juce::String daw, lastHostName;
    juce::uint32 lastResolve = 0;
    bool restored = false, manualName = false;
    mutable juce::CriticalSection resultsLock;
    juce::String detectedKey, lastSavedTime;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingEngine)
};
