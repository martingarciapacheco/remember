#pragma once
#include <JuceHeader.h>
#include "../../Shared/ProjectIdentity.h"
#include "SettingsManager.h"
#include <functional>
#include <memory>

// The facade never joins background I/O. Its retained core releases resources
// after cancellation and native thread completion, including during DLL unload.
class RecordingEngine
{
public:
    enum class State { Idle, Recording, Encoding, Done, Pending, Error };
    enum class Association { Unassigned, HostPath, TitleMatch, ManualFile, ManualRecording, Ambiguous };
    struct AssociationInfo
    {
        Association source = Association::Unassigned;
        juce::String id, path, description, confidence, problem;
        bool confirmed = false, verified = false;
    };
    using ProjectDetector = std::function<HostProject()>;
    explicit RecordingEngine (ProjectDetector = ProjectIdentity::detectHostProject);
    ~RecordingEngine();
    // Shared settings are read asynchronously. Explicit snapshots select an
    // independent destination/format without reading shared configuration.
    void prepare (double sampleRate, int maxBlockSize);
    void prepare (double sampleRate, int maxBlockSize, const SettingsManager::Snapshot&);
    void prepareAsync (double sampleRate, int maxBlockSize, SettingsManager::Reader);
    bool isPreparingSettings() const;
    void finishCapture();
    void setCaptureOptions (CaptureOptions);
    void applySharedCaptureOptions (CaptureOptions);
    CaptureOptions getCaptureOptions() const;
    bool isPreviewLocked() const;
    void processBlock (const juce::AudioBuffer<float>&, juce::AudioPlayHead*, bool offline = false);
    void setOutputFolder (const juce::File&);
    void loadOutputFolderFromSettings (SettingsManager::Reader);
    // Control/UI only: diagnostics are written by the retained core worker.
    void logDiagnostic (juce::String);
    void setProjectName (const juce::String&);
    void associateProjectFile (const juce::File&, const juce::String& expectedId = {});
    void keepRecording (const juce::String& name = {}, const juce::String& expectedId = {});
    void useAutomaticAssociation (const juce::String& expectedId = {});
    AssociationInfo getAssociationInfo() const;
    juce::String getProjectName() const;
    bool isProjectConfirmed() const;
    void saveState (juce::XmlElement&) const;
    void restoreState (const juce::XmlElement&, SettingsManager::Reader sharedSettings = {});
    State getState() const noexcept;
    float getProgress() const noexcept;
    juce::String getDetectedKey() const;
    double getDetectedBpm() const noexcept;
    juce::String getLastSavedTime() const;
    juce::String getLastError() const;
    uint64_t getDroppedCaptures() const noexcept;
    uint64_t getShortCaptures() const noexcept;
    uint64_t getUnknownPositions() const noexcept;
    size_t getReservedCaptureBytes() const noexcept;
    static size_t getProcessCaptureBytes() noexcept;
    juce::String getTransportWarning() const;
    int getTotalSeconds() const noexcept;

private:
    class Core;
    std::shared_ptr<Core> core;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingEngine)
};
