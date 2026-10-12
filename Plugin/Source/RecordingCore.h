#pragma once
#include <JuceHeader.h>
#include "RecordingEngine.h"
#include "../../Shared/BackgroundRetirement.h"
#include "SettingsManager.h"
#include "KeyDetector.h"
#include "BpmDetector.h"
#include "PreviewAnalysis.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace LibraryStore { struct AudioRevision; class OwnerLease; }

// Background workers own their context independently of the plugin facade.
class RecordingEngine::Core final : public juce::Thread, public BackgroundRetirement::Job
{
public:
    using State = RecordingEngine::State;
    using ProjectDetector = RecordingEngine::ProjectDetector;
    using Association = RecordingEngine::Association;
    explicit Core (ProjectDetector = ProjectIdentity::detectHostProject);
    ~Core() override;
    void requestStop() override;
    bool readyToRelease() const override;
    void prepare (double sampleRate, int maxBlockSize);
    void prepare (double sampleRate, int maxBlockSize, const SettingsManager::Snapshot&);
    void prepareAsync (double sampleRate, int maxBlockSize, SettingsManager::Reader);
    bool isPreparingSettings() const;
    void finishCapture();
    void setCaptureOptions (CaptureOptions);
    void applySharedCaptureOptions (CaptureOptions);
    CaptureOptions getCaptureOptions() const;
    void armCapture();
    bool isCaptureArmed() const;
    void processBlock (const juce::AudioBuffer<float>&, juce::AudioPlayHead*, bool offline = false);
    void setOutputFolder (const juce::File&);
    void loadOutputFolderFromSettings (SettingsManager::Reader);
    void logDiagnostic (juce::String);
    void setProjectName (const juce::String&);
    void associateProjectFile (const juce::File&, const juce::String&);
    void keepRecording (const juce::String&, const juce::String&);
    void useAutomaticAssociation (const juce::String&);
    RecordingEngine::AssociationInfo getAssociationInfo() const;
    juce::String getProjectName() const;
    bool isProjectConfirmed() const;
    void saveState (juce::XmlElement&) const;
    void restoreState (const juce::XmlElement&, SettingsManager::Reader);
    State getState() const noexcept;
    float getProgress() const noexcept;
    juce::String getDetectedKey() const;
    double getDetectedBpm() const noexcept { const auto manual = manualDetectedBpm.load(); return manual > 0 ? manual : capturedBpm.load(); }
    bool isPreviewLocked() const { return previewsLocked.load() || entryPreviewLocked.load(); }
    juce::String getLastSavedTime() const;
    juce::String getLastError() const;
    uint64_t getDroppedCaptures() const noexcept { return droppedCaptures.load(); }
    uint64_t getShortCaptures() const noexcept { return shortCaptures.load (std::memory_order_relaxed); }
    uint64_t getUnknownPositions() const noexcept { return unknownPositions.load(); }
    size_t getReservedCaptureBytes() const noexcept { return memoryBudget->load(); }
    static size_t getProcessCaptureBytes() noexcept;
    juce::String getTransportWarning() const;
    int getTotalSeconds() const noexcept;

private:
    enum class SlotState { Free, Claimed, Capturing, Draining, Discarding, Ready, Encoding };
    struct Slot
    {
        std::atomic<SlotState> state { SlotState::Free };
        juce::AudioBuffer<float> audio;
        juce::AbstractFifo fifo { 1 };
        juce::File capturedPcm;
        std::unique_ptr<juce::AudioFormatWriter> spoolWriter;
        juce::int64 spooledSamples = 0;
        std::atomic<bool> ioFailed { false }, queued { false }, overflow { false };
        int limit = 0;
        int count = 0;
        double bpm = 0, minimumBpm = 0, maximumBpm = 0;
        uint64_t generation = 0, sequence = 0;
        double rate = 44100;
        int quality = 4;
        bool mono = false, captureMono = false, replace = false;
        std::shared_ptr<SettingsManager::Request> settings;
        juce::File outputRoot;
        juce::int64 settingsRevision = 0;
        juce::String settingsFingerprint;
    };
    struct Bank
    {
        std::array<Slot, 2> slots;
        std::shared_ptr<std::atomic<size_t>> localBudget;
        size_t bytes = 0;
        SettingsManager::Snapshot prepared;
        std::shared_ptr<SettingsManager::Request> settings;
        ~Bank();
    };
    class SpoolThread final : public juce::Thread
    {
    public:
        explicit SpoolThread (Core& owner) : juce::Thread ("RememberCaptureSpool"), engine (owner) {}
        void run() override;
    private:
        Core& engine;
    };
    struct Entry
    {
        juce::String id { juce::Uuid().toString() };
        juce::String name, path, fileId, hash, associationProblem;
        juce::int64 fileStamp = 0;
        bool confirmed = false;
        Association association = Association::Unassigned;
        bool verified = false;
    };
    struct HostObservation
    {
        HostProject host;
        Entry context;
        uint64_t generation = 0;
        juce::String fileId, hash;
        juce::int64 stamp = 0;
        bool previousExists = false;
        Association association = Association::Unassigned;
        bool invalidManualFile = false;
        bool explicitAssociation = false;
        juce::String inventoryError;
    };
    class DiscoveryThread final : public juce::Thread
    {
    public:
        explicit DiscoveryThread (Core& owner) : juce::Thread ("RememberDiscovery"), engine (owner) {}
        void run() override;
    private:
        Core& engine;
    };
    struct AnalysisJob
    {
        std::shared_ptr<LibraryStore::AudioRevision> revision;
        int channels = 2;
        bool hostBpm = false, valid = false;
        uint64_t generation = 0;
        KeyDetector::Estimate key;
        BpmDetector::Estimate bpm;
        PreviewAnalysis::Source source;
        PreviewAnalysis::Result analysis;
        std::shared_ptr<PreviewAnalysis::Publication> publication { std::make_shared<PreviewAnalysis::Publication>() };
        double milliseconds = 0;
        std::atomic<bool> done { false }, cancelRequested { false };
    };
    class AnalysisThread final : public juce::Thread
    {
    public:
        explicit AnalysisThread (Core& owner) : juce::Thread ("RememberAnalysis"), engine (owner) {}
        void run() override;
    private:
        Core& engine;
        PreviewAnalysis analysis;
    };
    std::shared_ptr<AnalysisJob> startAnalysis (std::shared_ptr<LibraryStore::AudioRevision>, const Slot&, int length, int prefix,
                                               const juce::String& previousRevision, const juce::String& previousFingerprint);
    void run() override;
    void applyCommands();
    void prepareBank (double, int, const SettingsManager::Snapshot&, std::shared_ptr<SettingsManager::Request> = {});
    void applyPreparation();
    void applyRestoration();
    juce::File requestedDestination() const; // projectLock held; no I/O.
    void publishProject();
    void refreshEntryPreferences();
    void fail (const juce::String& phase, const juce::File&, const juce::String& reason);
    void startCapture (double bpm);
    void refreshHostProject (const HostObservation&);
    void consumeHostObservation();
    void processReadySlots();
    bool spoolCaptures();
    void discardSlot (Slot&);
    void encodeAndSave (Slot&, const juce::var& initialMetadata = {});
    bool forkPreview (const juce::File& previousFolder);
    bool mergePreview (const juce::File& previousFolder);
    void keepCurrentRecording (const juce::String& name);
    juce::var identityPatch() const;
    void updateMetadata();
    juce::File entryFolder() const;
    void adoptExistingEntry (const juce::String& path);
    bool promotePending (const juce::File& oldFolder);
    void removePending();
    void activateOutputFolder (const juce::File&);
    BackgroundRetirement::ModuleReference module;
    BackgroundRetirement::NativeCompletion workerCompletion, spoolCompletion, discoveryCompletion, analysisCompletion;
    bool workersStarted = false;
    ProjectDetector detector;
    SpoolThread spool { *this };
    DiscoveryThread discovery { *this };
    AnalysisThread analysisWorker { *this };
    juce::CriticalSection analysisLock;
    std::shared_ptr<AnalysisJob> queuedAnalysis;
    juce::CriticalSection observationLock;
    std::unique_ptr<HostObservation> latestObservation;
    juce::File captureCache;
    std::shared_ptr<std::atomic<size_t>> memoryBudget { std::make_shared<std::atomic<size_t>> (0) };
    std::atomic<bool> captureDisabled { false };
    // -1 awaits the creation default; 0/1 belongs to this DAW instance's state.
    std::atomic<int> manualCapture { -1 };
    std::atomic<bool> previewsLocked { false }, replacePreview { false }, captureArmed { false };
    std::atomic<bool> sharedCaptureOptions { true };
    std::atomic<bool> entryPreviewLocked { false };
    std::atomic<double> manualDetectedBpm { 0 };
    juce::String manualDetectedKey, lastPreferenceId;
    juce::uint32 preferencesReadAt = 0;
    juce::uint32 optionsPolledAt = 0;
    // prepare owns the audio-thread pointer; retired banks live until drained.
    std::shared_ptr<Bank> currentBank;
    std::vector<std::shared_ptr<Bank>> banks;
    mutable juce::CriticalSection bankLock;
    int activeSlot = -1; // only used by the audio callback or a suspended host
    uint64_t nextSequence = 0;
    bool wasPlaying = false;
    double previousTime = -1, previousBlockSeconds = 0, offlineTime = 0;
    double captureRate = 44100;
    std::atomic<float> recordSeconds { 60 };
    std::atomic<int> captureLimit { 0 };
    std::atomic<State> state { State::Idle };
    std::atomic<bool> capturing { false };
    std::atomic<int> samplesWritten { 0 };
    std::atomic<double> capturedBpm { 0 };
    std::atomic<uint64_t> generation { 1 };
    uint64_t contextGeneration = 1; // worker has applied this identity epoch.
    std::atomic<int> pendingCaptures { 0 };
    std::atomic<uint64_t> droppedCaptures { 0 }, shortCaptures { 0 }, unknownPositions { 0 };
    enum class TransportIssue { None, Unknown, LateStart, TooShort, SlotsFull, QueueOverflow, Budget };
    std::atomic<TransportIssue> transportIssue { TransportIssue::None };
    juce::WaitableEvent wake;
    // Never acquired by processBlock.
    mutable juce::CriticalSection projectLock;
    Entry publishedEntry;
    std::vector<std::function<void()>> commands;
    juce::File outputFolder;
    juce::File requestedOutputFolder; // projectLock; never queried from audio.
    uint64_t outputFolderRevision = 0; // projectLock; reject stale async reads.
    std::shared_ptr<SettingsManager::Request> preparationRequest; // projectLock
    uint64_t preparationRevision = 0;
    bool preparationApplied = false; // projectLock
    juce::File desiredOutputFolder;   // worker; applied at capture boundaries.
    struct LibraryContext { juce::File root; Entry entry; uint64_t generation = 0; };
    std::vector<LibraryContext> libraryContexts;
    struct OwnedPending { juce::File root, folder; bool preserve = false; };
    std::vector<OwnedPending> ownedPending;
    Entry entry;
    struct Restoration { Entry replacement; uint64_t epoch = 0; };
    std::unique_ptr<Restoration> pendingRestoration; // worker only
    juce::String daw;
    std::unique_ptr<LibraryStore::OwnerLease> storageOwner;
    bool restored = false, manualName = false;
    std::atomic<bool> associationBlocked { false };
    mutable juce::CriticalSection resultsLock;
    juce::String detectedKey, lastSavedTime, lastError;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Core)
};
