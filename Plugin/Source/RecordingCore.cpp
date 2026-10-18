#include "RecordingCore.h"
#include "KeyDetector.h"
#include "BpmDetector.h"
#include "AnalysisAudio.h"
#include "SettingsManager.h"
#include "RememberLogger.h"
#include "BuildInfo.h"
#include "../../Shared/LibraryStore.h"
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <limits>

namespace
{
using Association = RecordingEngine::Association;
juce::String associationName (Association value)
{
    switch (value)
    {
        case Association::HostPath: return "host-path";
        case Association::TitleMatch: return "title-match";
        case Association::ManualFile: return "manual-file";
        case Association::ManualRecording: return "manual-recording";
        case Association::Ambiguous: return "ambiguous";
        default: return "unassigned";
    }
}
juce::String associationConfidence (Association value)
{
    switch (value)
    {
        case Association::HostPath: return "direct";
        case Association::TitleMatch: return "inferred";
        case Association::ManualFile: case Association::ManualRecording: return "explicit";
        default: return "unknown";
    }
}
// Capture queues share a process budget. Per instance, including retired banks,
// the ceiling is 64 MiB; all instances together reserve at most 256 MiB.
std::atomic<size_t> captureBytes { 0 };
constexpr size_t instanceCaptureBudget = 64 * 1024 * 1024;
constexpr size_t processCaptureBudget = 256 * 1024 * 1024;
constexpr size_t slotCaptureBudget = 8 * 1024 * 1024;
struct HeavyJobs
{
    std::mutex mutex;
    std::condition_variable changed;
    int active = 0;
};
HeavyJobs heavyJobs;
class HeavyPermit
{
public:
    explicit HeavyPermit (const std::function<bool()>& cancelled)
    {
        std::unique_lock<std::mutex> lock (heavyJobs.mutex);
        while (heavyJobs.active >= 2)
        {
            if (cancelled()) return;
            heavyJobs.changed.wait_for (lock, std::chrono::milliseconds (10));
        }
        if (cancelled()) return;
        ++heavyJobs.active;
        acquired = true;
    }
    ~HeavyPermit() { release(); }
    void release()
    {
        if (! acquired) return;
        acquired = false;
        { std::lock_guard<std::mutex> lock (heavyJobs.mutex); --heavyJobs.active; }
        heavyJobs.changed.notify_one();
    }
    bool acquired = false;
};
static_assert (std::atomic<uint64_t>::is_always_lock_free, "Capture atomics must be lock-free");
juce::var loadMetadata (const juce::File& folder)
{
    return LibraryStore::readJson (folder.getChildFile ("metadata.json"));
}
juce::Result decodedPreviewPeak (const juce::File& file, int channels, int length, float& peak,
                                const std::function<bool()>& cancelled)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader) return juce::Result::fail ("Cannot decode the newly encoded preview");
    if (reader->numChannels != (unsigned int) channels || reader->lengthInSamples != length)
        return juce::Result::fail ("Decoded preview has a different channel count or duration");
    juce::AudioBuffer<float> block (channels, 4096); peak = 0;
    for (int offset = 0; offset < length;)
    {
        if (cancelled()) return juce::Result::fail ("Preview validation cancelled");
        const auto count = juce::jmin (4096, length - offset);
        if (! reader->read (&block, 0, count, offset, true, true))
            return juce::Result::fail ("Cannot read the newly encoded preview");
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < count; ++i)
            {
                const auto value = block.getSample (ch, i);
                if (! std::isfinite (value)) return juce::Result::fail ("Decoded preview contains invalid samples");
                peak = juce::jmax (peak, std::abs (value));
            }
        offset += count;
    }
    return juce::Result::ok();
}

}

RecordingEngine::Core::Bank::~Bank()
{
    if (bytes == 0) return;
    captureBytes.fetch_sub (bytes);
    localBudget->fetch_sub (bytes);
}

void RecordingEngine::Core::SpoolThread::run()
{
    while (! threadShouldExit()) juce::Thread::sleep (engine.spoolCaptures() ? 2 : 20);
}

void RecordingEngine::Core::discardSlot (Slot& slot)
{
    slot.spoolWriter.reset();
    if (slot.capturedPcm.isAChildOf (captureCache)) slot.capturedPcm.deleteFile();
    slot.fifo.reset();
    if (slot.queued.exchange (false)) pendingCaptures.fetch_sub (1);
    slot.state.store (SlotState::Free, std::memory_order_release);
}

bool RecordingEngine::Core::spoolCaptures()
{
    std::vector<std::shared_ptr<Bank>> available;
    {
        juce::ScopedLock lock (bankLock);
        available = banks;
    }
    bool busy = false;
    for (auto& bank : available)
        for (auto& slot : bank->slots)
        {
            auto status = slot.state.load (std::memory_order_acquire);
            if (status == SlotState::Discarding || (status == SlotState::Draining && threadShouldExit()))
            {
                if (slot.overflow.load()) fail ("Capture queue", slot.capturedPcm,
                    "Audio arrived faster than temporary storage; the last valid preview is retained. Replay from zero");
                discardSlot (slot);
                continue;
            }
            if (status != SlotState::Capturing && status != SlotState::Draining) continue;
            busy = true;
            if (slot.ioFailed.load())
            {
                if (status == SlotState::Draining) discardSlot (slot);
                continue;
            }
            if (! slot.spoolWriter)
            {
                auto created = LibraryStore::ensureDirectory (captureCache);
                if (created.wasOk())
                {
                    juce::WavAudioFormat format;
                    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (slot.capturedPcm);
                    slot.spoolWriter = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                        .withSampleRate (slot.rate).withNumChannels (slot.audio.getNumChannels()).withBitsPerSample (32)
                        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                    slot.spooledSamples = 0;
                }
                if (! slot.spoolWriter)
                {
                    slot.ioFailed.store (true);
                    fail ("Spool capture", slot.capturedPcm, "Cannot create temporary float WAV");
                    continue;
                }
            }
            int first, firstSize, second, secondSize;
            slot.fifo.prepareToRead (16384, first, firstSize, second, secondSize);
            if ((firstSize > 0 && ! slot.spoolWriter->writeFromAudioSampleBuffer (slot.audio, first, firstSize))
                || (secondSize > 0 && ! slot.spoolWriter->writeFromAudioSampleBuffer (slot.audio, second, secondSize)))
            {
                slot.ioFailed.store (true);
                fail ("Spool capture", slot.capturedPcm, "Temporary WAV write failed");
                continue;
            }
            slot.fifo.finishedRead (firstSize + secondSize);
            slot.spooledSamples += firstSize + secondSize;
            // Re-read producer publication: a stop can arrive during the write.
            if (slot.state.load (std::memory_order_acquire) == SlotState::Draining && slot.fifo.getNumReady() == 0)
            {
                slot.spoolWriter.reset();
                if (slot.spooledSamples != slot.count)
                {
                    fail ("Finish capture", slot.capturedPcm, "Spool length does not match capture");
                    discardSlot (slot);
                }
                else slot.state.store (SlotState::Ready, std::memory_order_release);
            }
        }
    return busy;
}

RecordingEngine::Core::Core (ProjectDetector detect)
    : juce::Thread ("RememberEncoding"), detector (std::move (detect))
{
    publishedEntry = entry;
    captureCache = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("RememberCapture-" + juce::Uuid().toString());
    if (! module.valid())
    {
        captureDisabled.store (true);
        lastError = "Cannot retain the plugin module for background workers";
        return;
    }
    const auto spoolStarted = spool.startThread();
    const auto discoveryStarted = discovery.startThread();
    const auto analysisStarted = analysisWorker.startThread();
    const auto workerStarted = startThread();
    workersStarted = spoolStarted && discoveryStarted && analysisStarted && workerStarted;
    if (spoolStarted) workersStarted = spoolCompletion.capture ((uintptr_t) spool.getThreadId()) && workersStarted;
    if (discoveryStarted) workersStarted = discoveryCompletion.capture ((uintptr_t) discovery.getThreadId()) && workersStarted;
    if (analysisStarted) workersStarted = analysisCompletion.capture ((uintptr_t) analysisWorker.getThreadId()) && workersStarted;
    if (workerStarted) workersStarted = workerCompletion.capture ((uintptr_t) getThreadId()) && workersStarted;
    if (! workersStarted)
    {
        captureDisabled.store (true);
        lastError = "Cannot start or monitor the background recording workers";
    }
}

RecordingEngine::Core::~Core()
{
    // Retirement checks OS completion before deleting any thread object.
    jassert (readyToRelease());
}
void RecordingEngine::Core::requestStop()
{
    finishCapture();
    generation.fetch_add (1);
    signalThreadShouldExit();
    spool.signalThreadShouldExit();
    discovery.signalThreadShouldExit();
    discovery.notify();
    analysisWorker.signalThreadShouldExit();
    analysisWorker.notify();
    wake.signal();
}
bool RecordingEngine::Core::readyToRelease() const
{
    return ! isThreadRunning() && ! spool.isThreadRunning() && ! discovery.isThreadRunning()
        && ! analysisWorker.isThreadRunning() && workerCompletion.finished() && spoolCompletion.finished()
        && discoveryCompletion.finished() && analysisCompletion.finished();
}

void RecordingEngine::Core::prepare (double newSampleRate, int maxBlockSize)
{
    prepareAsync (newSampleRate, maxBlockSize, [] { return SettingsManager::getInstance().readSnapshot(); });
}

void RecordingEngine::Core::prepare (double newSampleRate, int maxBlockSize, const SettingsManager::Snapshot& settings)
{
    sharedCaptureOptions.store (false);
    applySharedCaptureOptions (settings.capture);
    if (settings.previews != juce::File{}) setOutputFolder (settings.previews);
    {
        juce::ScopedLock lock (projectLock);
        preparationRequest.reset();
        ++outputFolderRevision;
    }
    prepareBank (newSampleRate, maxBlockSize, settings);
}

void RecordingEngine::Core::prepareAsync (double rate, int block, SettingsManager::Reader reader)
{
    sharedCaptureOptions.store (true);
    auto request = SettingsManager::readSnapshotAsync (std::move (reader));
    {
        juce::ScopedLock lock (projectLock);
        preparationRequest = request;
        preparationRevision = ++outputFolderRevision;
        // The bank must be installed before the worker applies this request.
        preparationApplied = true;
    }
    SettingsManager::Snapshot capacity;
    capacity.seconds = 120;
    if (request->complete.load (std::memory_order_acquire) && request->result.wasOk())
        capacity = request->snapshot;
    // Keep both input channels until the immutable settings arrive. The
    // encoder performs the same averaged mono conversion as a known bank.
    prepareBank (rate, block, capacity, request);
    {
        juce::ScopedLock lock (projectLock);
        if (preparationRequest == request) preparationApplied = false;
    }
    wake.signal();
}

bool RecordingEngine::Core::isPreparingSettings() const
{
    juce::ScopedLock lock (projectLock);
    return preparationRequest && ! preparationRequest->complete.load (std::memory_order_acquire);
}

juce::File RecordingEngine::Core::requestedDestination() const
{
    if (preparationRequest && preparationRevision == outputFolderRevision
        && preparationRequest->complete.load (std::memory_order_acquire) && preparationRequest->result.wasOk())
        return preparationRequest->snapshot.previews;
    return requestedOutputFolder;
}

void RecordingEngine::Core::applyPreparation()
{
    std::shared_ptr<SettingsManager::Request> request;
    {
        juce::ScopedLock lock (projectLock);
        if (! preparationRequest || preparationApplied
            || ! preparationRequest->complete.load (std::memory_order_acquire)) return;
        request = preparationRequest;
        preparationApplied = true;
        if (preparationRevision != outputFolderRevision) return;
        if (request->result.wasOk())
            requestedOutputFolder = request->snapshot.previews;
    }
    if (request->result.failed())
    {
        captureDisabled.store (true);
        fail ("Prepare settings", LibraryStore::configFile(), request->result.getErrorMessage());
        return;
    }
    applySharedCaptureOptions (request->snapshot.capture);
    recordSeconds.store ((float) juce::jlimit (15, 120, request->snapshot.seconds));
    {
        juce::ScopedLock lock (resultsLock);
        if (lastError.startsWith ("Prepare settings:")) lastError.clear();
    }
    desiredOutputFolder = request->snapshot.previews;
    publishOutputFolder (desiredOutputFolder);
    if (pendingCaptures.load() == 0 && ! capturing.load()) activateOutputFolder (desiredOutputFolder);
}

void RecordingEngine::Core::prepareBank (double newSampleRate, int maxBlockSize, const SettingsManager::Snapshot& settings,
                                        std::shared_ptr<SettingsManager::Request> request)
{
    if (! workersStarted) { captureDisabled.store (true); state.store (State::Error); return; }
    finishCapture();
    // Hosts suspend the callback here. Previous banks belong to the worker
    // until it drains them, so this never joins an encoder or resizes its data.
    captureRate = newSampleRate > 0 ? newSampleRate : 44100;
    recordSeconds = (float) juce::jlimit (15, 120, settings.seconds);
    captureLimit = (int) (captureRate * recordSeconds.load());
    juce::File destination;
    {
        juce::ScopedLock lock (projectLock);
        destination = requestedOutputFolder;
    }
    // Retired free banks can be released before allocating a replacement.
    {
        juce::ScopedLock lock (bankLock);
        currentBank.reset();
        banks.erase (std::remove_if (banks.begin(), banks.end(), [] (const auto& bank)
        {
            for (auto& slot : bank->slots) if (slot.state.load() != SlotState::Free) return false;
            return true;
        }), banks.end());
    }
    const auto channels = settings.mono ? 1 : 2;
    // Construct the reservation owner before changing the global counter.
    // Allocation failure then cannot strand a process-wide reservation.
    auto next = std::make_shared<Bank>();
    next->localBudget = memoryBudget;
    next->prepared = settings;
    next->prepared.previews = destination;
    next->settings = request;
    size_t reserved = captureBytes.load();
    size_t frames = 0, bytes = 0;
    for (;;)
    {
        const auto local = memoryBudget->load();
        const auto available = juce::jmin (local < instanceCaptureBudget ? instanceCaptureBudget - local : size_t{},
                                          reserved < processCaptureBudget ? processCaptureBudget - reserved : size_t{});
        const auto frameBytes = (size_t) channels * sizeof (float);
        const auto wanted = juce::jmin ((size_t) captureLimit.load() + 1, slotCaptureBudget / frameBytes);
        frames = juce::jmin (wanted, available / (frameBytes * 2));
        if (frames > 1) --frames; // AbstractFifo retains one empty position.
        if (frames < (size_t) juce::jmax (4096, maxBlockSize * 2)) { frames = 0; break; }
        bytes = (frames + 1) * frameBytes * 2;
        if (captureBytes.compare_exchange_weak (reserved, reserved + bytes)) break;
    }
    if (frames == 0)
    {
        captureDisabled.store (true);
        transportIssue.store (TransportIssue::Budget);
        {
            juce::ScopedLock lock (resultsLock);
            lastError = "Capture memory budget is busy; retry after pending jobs finish";
        }
        state.store (State::Error);
        return;
    }
    next->bytes = bytes;
    memoryBudget->fetch_add (bytes);
    for (auto& slot : next->slots)
    {
        slot.audio.setSize (channels, (int) frames + 1, false, false, true);
        slot.fifo.setTotalSize ((int) frames + 1);
        slot.limit = captureLimit.load();
        slot.capturedPcm = captureCache.getChildFile (juce::Uuid().toString() + ".wav");
        slot.rate = captureRate;
        slot.quality = juce::jlimit (0, 10, settings.quality);
        slot.mono = settings.mono;
        slot.captureMono = settings.mono;
        slot.settings = request;
        slot.preparedDestination = outputDestination.load();
        slot.outputRoot = destination;
        slot.settingsRevision = settings.revision;
        slot.settingsFingerprint = settings.fingerprint;
    }
    {
        juce::ScopedLock lock (bankLock);
        banks.push_back (next);
        currentBank = std::move (next);
    }
    captureDisabled.store (false);
    transportIssue.store (TransportIssue::None);
    activeSlot = -1;
    wasPlaying = false;
    previousTime = -1;
    previousBlockSeconds = offlineTime = 0;
    samplesWritten.store (0);
    capturing.store (false);
    if (pendingCaptures.load() == 0) state.store (State::Idle);
}

void RecordingEngine::Core::startCapture (double bpm)
{
    if (! currentBank) { droppedCaptures.fetch_add (1, std::memory_order_relaxed); return; }
    auto& slots = currentBank->slots;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto expected = SlotState::Free;
        if (! slots[i].state.compare_exchange_strong (expected, SlotState::Claimed)) continue;
        activeSlot = (int) i;
        auto& slot = slots[i];
        slot.count = 0;
        slot.fifo.reset();
        slot.ioFailed.store (false);
        slot.overflow.store (false);
        slot.queued.store (false);
        slot.bpm = std::isfinite (bpm) && bpm > 0 ? bpm : 0;
        slot.minimumBpm = slot.maximumBpm = slot.bpm;
        slot.replace = replacePreview.load();
        slot.generation = generation.load();
        slot.sequence = ++nextSequence;
        slot.capturedDestination = outputDestination.load();
        capturedBpm.store (bpm);
        samplesWritten.store (0);
        capturing.store (true);
        transportIssue.store (TransportIssue::None);
        slot.state.store (SlotState::Capturing, std::memory_order_release);
        return;
    }
    droppedCaptures.fetch_add (1, std::memory_order_relaxed);
    transportIssue.store (TransportIssue::SlotsFull);
}

void RecordingEngine::Core::finishCapture()
{
    if (activeSlot < 0) return;
    auto& slot = currentBank->slots[(size_t) activeSlot];
    const bool valid = ! slot.ioFailed.load() && slot.count >= (int) slot.rate;
    if (! slot.ioFailed.load() && slot.count > 0 && slot.count < (int) slot.rate && slot.generation == generation.load())
    {
        shortCaptures.fetch_add (1, std::memory_order_relaxed);
        transportIssue.store (TransportIssue::TooShort);
    }
    activeSlot = -1;
    capturing.store (false);
    if (valid)
    {
        slot.queued.store (true);
        pendingCaptures.fetch_add (1, std::memory_order_relaxed);
        state.store (State::Encoding);
    }
    // The worker polls Ready slots. Audio never signals a locking event.
    slot.state.store (valid ? SlotState::Draining : SlotState::Discarding, std::memory_order_release);
}

void RecordingEngine::Core::processBlock (const juce::AudioBuffer<float>& buffer,
                                    juce::AudioPlayHead* playHead, bool offline)
{
    if (isPreviewLocked() || associationBlocked.load (std::memory_order_acquire))
    {
        finishCapture(); wasPlaying = false; previousTime = -1; return;
    }
    auto position = playHead != nullptr ? playHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
    if (! position.hasValue() && ! offline)
    {
        finishCapture();
        if (transportIssue.exchange (TransportIssue::Unknown) != TransportIssue::Unknown)
            unknownPositions.fetch_add (1, std::memory_order_relaxed);
        wasPlaying = false;
        previousTime = -1;
        return;
    }
    const bool playing = offline || position->getIsPlaying();
    const auto seconds = position.hasValue() ? position->getTimeInSeconds() : juce::Optional<double>{};
    const auto samples = position.hasValue() ? position->getTimeInSamples() : juce::Optional<juce::int64>{};
    if (! seconds.hasValue() && ! samples.hasValue() && ! offline)
    {
        finishCapture();
        if (playing && ! wasPlaying) unknownPositions.fetch_add (1, std::memory_order_relaxed);
        if (playing) transportIssue.store (TransportIssue::Unknown);
        wasPlaying = playing;
        previousTime = -1;
        return;
    }
    const double time = seconds.hasValue() ? *seconds
        : samples.hasValue() ? (double) *samples / captureRate : offlineTime;
    const double blockSeconds = (double) buffer.getNumSamples() / captureRate;
    const bool movedBack = previousTime >= 0 && time + blockSeconds * 0.5 < previousTime;
    const bool jumpedForward = previousTime >= 0 && time > previousTime + previousBlockSeconds + blockSeconds * 2;
    // A stopped block, seek, or another loop must not be appended to the preview.
    if (activeSlot >= 0 && (! playing || movedBack || jumpedForward)) finishCapture();
    const bool manual = manualCapture.load() == 1;
    // Any-position mode follows transport boundaries directly; it never needs
    // a UI request, even when the host sends no stopped audio blocks.
    const bool requested = playing && manual
        && (! wasPlaying || ! wasAnyPosition || previousTime < 0 || movedBack || jumpedForward);
    wasAnyPosition = manual;
    if (requested || (! manual && playing && time >= 0 && time <= 0.5 / captureRate && (! wasPlaying || movedBack)))
    {
        if (activeSlot < 0) startCapture (position.hasValue() ? position->getBpm().orFallback (0) : 0);
    }
    else if (! manual && playing && ! wasPlaying && time > 0.5 / captureRate)
        transportIssue.store (TransportIssue::LateStart);
    if (playing && activeSlot >= 0)
    {
        auto& slot = currentBank->slots[(size_t) activeSlot];
        if (slot.generation != generation.load()) finishCapture();
        else
        {
            const auto hostBpm = position.hasValue() ? position->getBpm().orFallback (0) : 0;
            if (std::isfinite (hostBpm) && hostBpm > 0)
            {
                if (slot.bpm <= 0) slot.bpm = hostBpm;
                slot.minimumBpm = slot.minimumBpm > 0 ? juce::jmin (slot.minimumBpm, hostBpm) : hostBpm;
                slot.maximumBpm = juce::jmax (slot.maximumBpm, hostBpm);
            }
            if (slot.ioFailed.load()) finishCapture();
            else
            {
                int limit = slot.limit;
                if (slot.settings && slot.settings->complete.load (std::memory_order_acquire))
                {
                    if (slot.settings->result.failed())
                    { slot.ioFailed.store (true); finishCapture(); wasPlaying = playing; previousTime = time; return; }
                    limit = (int) (slot.rate * juce::jlimit (15, 120, slot.settings->snapshot.seconds));
                    captureLimit.store (limit);
                }
                // A slow settings read can finish after this bank has already
                // captured its configured duration. The encoder trims that
                // prefix; audio never mutates the immutable format/destination.
                if (slot.count >= limit)
                { finishCapture(); wasPlaying = playing; previousTime = time; return; }
                const int count = juce::jmin (buffer.getNumSamples(), limit - slot.count);
                if (offline && count <= slot.fifo.getTotalSize() - 1)
                {
                    // An explicitly non-real-time host export can outrun disk
                    // indefinitely. Pace only that mode against the spooler;
                    // real-time playback never waits for storage or a worker.
                    const auto deadline = juce::Time::getMillisecondCounter() + 5000;
                    while (slot.fifo.getFreeSpace() < count && ! slot.ioFailed.load()
                        && slot.generation == generation.load() && ! threadShouldExit()
                        && (int32_t) (deadline - juce::Time::getMillisecondCounter()) > 0)
                        juce::Thread::sleep (1);
                }
                if (slot.fifo.getFreeSpace() < count)
                {
                    slot.ioFailed.store (true);
                    slot.overflow.store (true);
                    transportIssue.store (TransportIssue::QueueOverflow);
                    droppedCaptures.fetch_add (1, std::memory_order_relaxed);
                    finishCapture();
                }
                else
                {
                    int first, firstSize, second, secondSize;
                    slot.fifo.prepareToWrite (count, first, firstSize, second, secondSize);
                    auto copy = [&] (int destination, int source, int size)
                    {
                        for (int ch = 0; ch < slot.audio.getNumChannels(); ++ch)
                        {
                            if (buffer.getNumChannels() > 0)
                                slot.audio.copyFrom (ch, destination, buffer,
                                    juce::jmin (ch, buffer.getNumChannels() - 1), source, size);
                            else slot.audio.clear (ch, destination, size);
                        }
                        if (slot.captureMono && buffer.getNumChannels() > 1)
                        {
                            slot.audio.applyGain (0, destination, size, 0.5f);
                            slot.audio.addFrom (0, destination, buffer, 1, source, size, 0.5f);
                        }
                    };
                    copy (first, 0, firstSize);
                    copy (second, firstSize, secondSize);
                    slot.fifo.finishedWrite (firstSize + secondSize);
                    slot.count += count;
                    samplesWritten.store (slot.count);
                    if (slot.count == limit) finishCapture();
                }
            }
        }
    }
    wasPlaying = playing;
    previousTime = time;
    previousBlockSeconds = blockSeconds;
    offlineTime += blockSeconds;
}

juce::File RecordingEngine::Core::entryFolder() const
{
    return entry.confirmed ? outputFolder.getChildFile (entry.id)
                           : outputFolder.getChildFile (".pending").getChildFile (entry.id);
}

void RecordingEngine::Core::removePending()
{
    if (! storageOwner) return;
    for (const auto& pending : ownedPending)
        if (! pending.preserve && pending.folder.isDirectory())
            if (auto result = LibraryStore::discardOwnedPending (pending.root, pending.folder, *storageOwner); result.failed())
                juce::Logger::writeToLog ("Pending cleanup: " + result.getErrorMessage());
    ownedPending.clear();
}

bool RecordingEngine::Core::promotePending (const juce::File& oldFolder)
{
    const auto target = entryFolder();
    if (target == oldFolder || ! oldFolder.isDirectory()) return true;
    if (! oldFolder.isAChildOf (outputFolder.getChildFile (".pending"))) return false;
    if (! storageOwner) storageOwner = std::make_unique<LibraryStore::OwnerLease>();
    if (auto claimed = LibraryStore::claimPending (outputFolder, oldFolder, *storageOwner); claimed.failed())
    { fail ("Own pending association", oldFolder, claimed.getErrorMessage()); return false; }
    if (target.exists())
    {
        // Pending audio can arrive before the host identifies an existing file.
        // Merge its prefix into that entry instead of creating a duplicate.
        if (! mergePreview (oldFolder)) return false;
        return LibraryStore::discardOwnedPending (outputFolder, oldFolder, *storageOwner).wasOk();
    }
    juce::var patch (new juce::DynamicObject()); auto* object = patch.getDynamicObject();
    object->setProperty ("projectId", entry.id); object->setProperty ("projectName", entry.name);
    object->setProperty ("projectPath", entry.path); object->setProperty ("projectFileId", entry.fileId);
    object->setProperty ("projectFingerprint", entry.hash);
    object->setProperty ("projectAssociation", associationName (entry.association));
    object->setProperty ("projectAssociationConfidence", associationConfidence (entry.association));
    const auto result = LibraryStore::promoteOwnedPending (outputFolder, oldFolder, *storageOwner, patch);
    if (result.failed()) fail ("Promote pending association", oldFolder, result.getErrorMessage());
    else if (state.load() == State::Pending) state.store (State::Done);
    return result.wasOk();
}

void RecordingEngine::Core::adoptExistingEntry (const juce::String& path)
{
    if (! outputFolder.isDirectory()) return;
    const juce::File project (path);
    const auto actualId = ProjectIdentity::fileId (project);
    juce::String actualHash;
    juce::File match;
    for (auto& folder : outputFolder.findChildFiles (juce::File::findDirectories, false))
    {
        if (folder.getFileName().startsWithChar ('.') || folder.isSymbolicLink() || LibraryStore::isDeleted (folder)) continue;
        auto metadata = loadMetadata (folder);
        auto existingPath = metadata.getProperty ("projectPath", "").toString();
        const auto knownId = metadata.getProperty ("projectFileId", "").toString();
        const auto knownHash = metadata.getProperty ("projectFingerprint", "").toString();
        const bool samePath = existingPath.isNotEmpty() && juce::File (existingPath) == project;
        bool identityMatches = knownId.isNotEmpty() && actualId == knownId;
        if (! identityMatches && knownHash.isNotEmpty() && (samePath || (existingPath.isNotEmpty() && ! juce::File (existingPath).existsAsFile())))
        {
            if (actualHash.isEmpty()) actualHash = ProjectIdentity::fingerprint (project, [this] { return threadShouldExit(); });
            identityMatches = actualHash.isNotEmpty() && actualHash == knownHash;
        }
        if (identityMatches || (samePath && knownId.isEmpty() && knownHash.isEmpty()))
        {
            if (match != juce::File{}) return;
            match = folder;
        }
    }
    if (match == juce::File{}) return;
    auto metadata = loadMetadata (match);
    auto id = metadata.getProperty ("projectId", "").toString();
    id = id.isNotEmpty() && ! juce::Uuid (id).isNull() ? juce::Uuid (id).toString() : entry.id;
    auto target = outputFolder.getChildFile (id);
    if (match != target && LibraryStore::renameEntryWithoutReplacement (match, target).failed()) return;
    entry.id = id;
}

void RecordingEngine::Core::DiscoveryThread::run()
{
    ProjectIdentity::CandidateIndex candidates;
    juce::String rootsKey;
    juce::uint32 indexedAt = 0;
    bool indexed = false;
    bool attemptedInventory = false;
    juce::String inventoryError;
    while (! threadShouldExit())
    {
        const auto started = juce::Time::getMillisecondCounterHiRes();
        auto observation = std::make_unique<HostObservation>();
        {
            juce::ScopedLock lock (engine.projectLock);
            observation->context = engine.publishedEntry;
            observation->generation = engine.generation.load();
        }
        const auto requestGeneration = observation->generation;
        const auto cancelled = [this, requestGeneration]
        { return threadShouldExit() || requestGeneration != engine.generation.load(); };
        candidates.setCancellation (cancelled);
        if (observation->context.association != Association::ManualRecording) observation->host = engine.detector();
        auto& host = observation->host;
        const auto& context = observation->context;
        observation->association = host.path.isNotEmpty() ? Association::HostPath : Association::Unassigned;
        double inventoryMs = 0, hashMs = 0;
        const bool manualFile = context.association == Association::ManualFile;
        const bool directManualPath = manualFile && host.path.isNotEmpty() && juce::File (host.path) == juce::File (context.path);
        const bool verifyManualIdentity = manualFile && host.path.isEmpty();
        if (verifyManualIdentity)
        {
            host.available = true; host.path = context.path; host.name = context.name;
            observation->association = Association::ManualFile;
        }
        else if (directManualPath) observation->association = Association::ManualFile;
        if (! cancelled() && host.available)
        {
            const bool verifiedManualPath = verifyManualIdentity && context.fileId.isNotEmpty()
                && ProjectIdentity::fileId (juce::File (context.path)) == context.fileId;
            if ((host.path.isEmpty() && host.name.isNotEmpty()) || (verifyManualIdentity && ! verifiedManualPath))
            {
                auto roots = ProjectIdentity::searchRoots();
                if (observation->context.path.isNotEmpty())
                    roots.addIfNotAlreadyThere (juce::File (observation->context.path).getParentDirectory());
                juce::String key;
                for (const auto& root : roots) key += root.getFullPathName() + "\n";
                const auto now = juce::Time::getMillisecondCounter();
                const bool lostManualPath = verifyManualIdentity && context.verified && ! juce::File (context.path).existsAsFile();
                if (! attemptedInventory || key != rootsKey || now - indexedAt >= (indexed ? 30000u : 5000u) || lostManualPath)
                {
                    const auto start = juce::Time::getMillisecondCounterHiRes();
                    indexed = candidates.rebuild (roots, cancelled);
                    inventoryMs = juce::Time::getMillisecondCounterHiRes() - start;
                    if (! cancelled())
                    {
                        attemptedInventory = true; rootsKey = key; indexedAt = now;
                        const auto error = candidates.getLastError();
                        if (error != inventoryError && error.isNotEmpty()) juce::Logger::writeToLog (error);
                        inventoryError = error;
                    }
                }
                // A known path does not prove a same-title Save As still refers
                // to that file. Resolve the title conservatively across roots.
                if (indexed && ! cancelled())
                {
                    host.path = verifyManualIdentity
                        ? candidates.resolve (context.name, {}, context.path, context.fileId, context.hash)
                        : candidates.resolve (host.name, host.daw);
                    if (! verifyManualIdentity) observation->association = host.path.isNotEmpty() ? Association::TitleMatch
                        : candidates.wasLastResolutionAmbiguous() ? Association::Ambiguous : Association::Unassigned;
                }
                if (! indexed && ! cancelled()) observation->inventoryError = candidates.getLastError();
            }
            if (host.path.isNotEmpty() && ! cancelled())
            {
                const juce::File observedFile (host.path);
                const auto preferred = ProjectIdentity::preferredProjectFile (observedFile);
                const bool redirectedBackup = preferred != juce::File{}
                    && (preferred != observedFile
                        || (verifyManualIdentity && preferred != juce::File (context.path)
                            && ProjectIdentity::preferredProjectFile (juce::File (context.path)) == preferred));
                host.path = preferred == juce::File{} ? juce::String{} : preferred.getFullPathName();
                if (redirectedBackup) host.name = preferred.getFileNameWithoutExtension();
                const auto file = preferred;
                if (host.path.isEmpty() || ! file.existsAsFile()) host.path.clear();
                else
                {
                    observation->stamp = file.getLastModificationTime().toMilliseconds();
                    observation->fileId = ProjectIdentity::fileId (file);
                    if (host.path != context.path || observation->stamp != context.fileStamp
                        || observation->fileId != context.fileId || context.hash.isEmpty())
                    {
                        const auto start = juce::Time::getMillisecondCounterHiRes();
                        observation->hash = ProjectIdentity::fingerprint (file, cancelled);
                        hashMs = juce::Time::getMillisecondCounterHiRes() - start;
                    }
                    else observation->hash = context.hash;
                    // Changes during the hash are retried; never publish mixed
                    // identity/stamp/hash evidence or an incomplete fingerprint.
                    if (observation->hash.isEmpty()
                        || observation->stamp != file.getLastModificationTime().toMilliseconds()
                        || observation->fileId != ProjectIdentity::fileId (file)) host.path.clear();
                    if (verifyManualIdentity && ! redirectedBackup && ! ((context.fileId.isNotEmpty() && observation->fileId == context.fileId)
                                                  || (context.hash.isNotEmpty() && observation->hash == context.hash))) host.path.clear();
                }
            }
            observation->previousExists = observation->context.path.isNotEmpty()
                && juce::File (observation->context.path).existsAsFile();
            observation->invalidManualFile = verifyManualIdentity && host.path.isEmpty();
        }
        const auto stable = observation->context.confirmed;
        if (! cancelled())
        {
            const auto discoveryMs = juce::Time::getMillisecondCounterHiRes() - started;
            if (inventoryMs > 0 || hashMs > 0 || discoveryMs > 50)
                juce::Logger::writeToLog ("Discovery totalMs=" + juce::String (discoveryMs, 2)
                    + " inventoryMs=" + juce::String (inventoryMs, 2) + " hashMs=" + juce::String (hashMs, 2)
                    + " candidates=" + juce::String ((juce::int64) candidates.getCandidateCount()));
            {
                juce::ScopedLock lock (engine.observationLock);
                engine.latestObservation = std::move (observation);
            }
            engine.wake.signal(); // Background thread, never the audio callback.
        }
        wait (stable ? 1000 : 250);
    }
}

void RecordingEngine::Core::consumeHostObservation()
{
    std::unique_ptr<HostObservation> observation;
    {
        juce::ScopedLock lock (observationLock);
        observation.swap (latestObservation);
    }
    if (! observation || observation->generation != generation.load()
        || observation->context.id != entry.id || observation->context.path != entry.path
        || observation->context.association != entry.association) return;
    refreshHostProject (*observation);
}

void RecordingEngine::Core::refreshHostProject (const HostObservation& observation)
{
    const auto& host = observation.host;
    if (! host.available) return;
    if (entry.association == Association::ManualRecording && ! observation.explicitAssociation) return;
    if (observation.inventoryError.isNotEmpty())
    {
        // A failed tree traversal is not evidence of a new/unassigned project.
        // Retain its identity and preview; stop recording until the host can
        // be verified or the user supplies a specific file.
        entry.associationProblem = observation.inventoryError;
        entry.verified = false;
        if (! associationBlocked.exchange (true)) contextGeneration = generation.fetch_add (1) + 1;
        return;
    }
    if (entry.associationProblem.isNotEmpty())
    {
        entry.associationProblem.clear();
        associationBlocked.store (false);
    }
    if (observation.invalidManualFile)
    {
        entry.verified = false;
        if (! associationBlocked.exchange (true)) contextGeneration = generation.fetch_add (1) + 1;
        fail ("Verify manual association", juce::File (entry.path), "Selected project is missing or its identity changed. Select the project file again");
        return;
    }
    daw = host.daw;
    if (host.path.isEmpty())
    {
        if (restored || entry.confirmed)
        {
            const bool replacingLiveProject = ! restored;
            removePending();
            entry = Entry{};
            // restoreState already invalidated old audio before publishing its
            // command. Audio captured afterwards belongs to the current host.
            if (replacingLiveProject) contextGeneration = generation.fetch_add (1) + 1;
            state.store (State::Idle);
            manualName = false;
        }
        restored = false;
        entry.association = observation.association;
        entry.verified = false;
        if (! manualName || host.name.isNotEmpty())
        {
            entry.name = host.name;
            if (host.name.isNotEmpty()) manualName = false;
            updateMetadata();
        }
        return;
    }
    juce::File file (host.path);
    if (! restored && entry.confirmed && entry.verified && entry.association == observation.association
        && entry.path == host.path && entry.fileId == observation.fileId
        && entry.hash == observation.hash && entry.fileStamp == observation.stamp) return;
    auto previousFolder = entryFolder();
    if (entry.association == Association::ManualFile && ! entry.confirmed)
    {
        const auto permanent = outputFolder.getChildFile (entry.id);
        if (loadMetadata (permanent).getProperty ("projectId", "").toString() == entry.id) previousFolder = permanent;
    }
    bool changed = entry.path.isNotEmpty() && juce::File (entry.path) != file;
    const bool replacedSelection = observation.explicitAssociation
        && (entry.confirmed || entry.association == Association::ManualFile) && ! changed
        && entry.fileId.isNotEmpty() && observation.fileId != entry.fileId
        && entry.hash.isNotEmpty() && observation.hash != entry.hash;
    const bool sameFile = entry.fileId.isNotEmpty() && entry.fileId == observation.fileId;
    bool fork = (changed && observation.previousExists && ! sameFile) || replacedSelection;
    bool mergeKeptRecording = false;
    if (fork)
    {
        const bool replacingLiveProject = entry.confirmed;
        entry = Entry{};
        if (replacingLiveProject) contextGeneration = generation.fetch_add (1) + 1;
        adoptExistingEntry (host.path);
    }
    else if (! entry.confirmed)
    {
        adoptExistingEntry (host.path);
    }
    else if (entry.association == Association::ManualRecording && observation.explicitAssociation)
    {
        const auto previousId = entry.id;
        adoptExistingEntry (host.path);
        mergeKeptRecording = previousId != entry.id;
        if (mergeKeptRecording) contextGeneration = generation.fetch_add (1) + 1;
    }
    entry.path = file.getFullPathName();
    entry.name = file.getFileNameWithoutExtension();
    entry.confirmed = true;
    entry.association = observation.association;
    entry.verified = true;
    manualName = false;
    restored = false;
    entry.fileId = observation.fileId;
    entry.hash = observation.hash;
    entry.fileStamp = observation.stamp;
    if (associationBlocked.exchange (false))
    {
        juce::ScopedLock result (resultsLock);
        if (lastError.startsWith ("Verify manual association"))
        { lastError.clear(); if (state.load() == State::Error) state.store (State::Idle); }
    }
    if (fork && ! entryFolder().exists() && LibraryStore::audioFile (previousFolder, false).existsAsFile())
        if (! forkPreview (previousFolder) && state.load() != State::Error)
            fail ("Save As preview", entryFolder(), "Cannot publish the previous preview; its original entry was preserved");
    if (mergeKeptRecording)
    {
        if (! mergePreview (previousFolder) && state.load() != State::Error)
            fail ("Associate kept recording", previousFolder, "Cannot merge this recording; both original entries were preserved");
    }
    else if (! fork && ! promotePending (previousFolder) && state.load() != State::Error)
        fail ("Associate pending audio", previousFolder, "Cannot merge or move the pending preview");
    updateMetadata();
}

void RecordingEngine::Core::updateMetadata()
{
    auto folder = entryFolder();
    auto metadata = loadMetadata (folder);
    if (metadata.getDynamicObject() == nullptr) return;
    const auto* existing = metadata.getDynamicObject();
    auto* obj = existing;
    bool changed = obj->getProperty ("projectName").toString() != entry.name
        || obj->getProperty ("projectPath").toString() != entry.path
        || obj->getProperty ("projectId").toString() != entry.id
        || obj->getProperty ("projectFileId").toString() != entry.fileId
        || obj->getProperty ("projectFingerprint").toString() != entry.hash
        || obj->getProperty ("projectAssociation").toString() != associationName (entry.association)
        || (bool) obj->getProperty ("pending") == entry.confirmed;
    if (! changed) return;
    metadata = juce::var (new juce::DynamicObject());
    auto* patch = metadata.getDynamicObject();
    patch->setProperty ("projectId", entry.id);
    patch->setProperty ("projectName", entry.name);
    patch->setProperty ("projectPath", entry.path);
    patch->setProperty ("projectFileId", entry.fileId);
    patch->setProperty ("projectFingerprint", entry.hash);
    patch->setProperty ("pending", ! entry.confirmed);
    patch->setProperty ("projectAssociation", associationName (entry.association));
    patch->setProperty ("projectAssociationConfidence", associationConfidence (entry.association));
    if (auto result = LibraryStore::patchJson (folder.getChildFile ("metadata.json"), metadata); result.failed())
        fail ("Update project metadata", folder, result.getErrorMessage());
    else if (entry.confirmed && state.load() == State::Pending) state.store (State::Done);
}

void RecordingEngine::Core::applyCommands()
{
    std::vector<std::function<void()>> pending;
    {
        juce::ScopedLock lock (projectLock);
        pending.swap (commands);
    }
    for (size_t index = 0; index < pending.size(); ++index)
    {
        // Restoration is an ordered command even when a newer preparation
        // still lacks settings. Keep subsequent edits behind that barrier.
        applyPreparation();
        applyRestoration();
        if (pendingRestoration)
        {
            juce::ScopedLock lock (projectLock);
            commands.insert (commands.begin(), std::make_move_iterator (pending.begin() + (ptrdiff_t) index),
                             std::make_move_iterator (pending.end()));
            return;
        }
        pending[index]();
    }
}

void RecordingEngine::Core::logDiagnostic (juce::String message)
{
    {
        juce::ScopedLock lock (projectLock);
        if (threadShouldExit()) return;
        commands.push_back ([message = std::move (message)] { juce::Logger::writeToLog (message); });
    }
    wake.signal();
}

void RecordingEngine::Core::publishProject()
{
    bool changed = false;
    {
        juce::ScopedLock lock (projectLock);
        if (commands.empty())
        {
            changed = publishedEntry.id != entry.id || publishedEntry.path != entry.path
                || publishedEntry.confirmed != entry.confirmed || publishedEntry.hash != entry.hash
                || publishedEntry.association != entry.association || publishedEntry.verified != entry.verified
                || publishedEntry.associationProblem != entry.associationProblem;
            publishedEntry = entry;
        }
    }
    if (changed) discovery.notify();
}

void RecordingEngine::Core::fail (const juce::String& phase, const juce::File& file, const juce::String& reason)
{
    auto message = phase + ": " + reason + " (" + file.getFullPathName() + ")";
    {
        juce::ScopedLock lock (resultsLock);
        lastError = message;
    }
    state.store (State::Error);
    juce::Logger::writeToLog (message);
}

void RecordingEngine::Core::run()
{
    for (;;)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (sharedCaptureOptions.load() && manualCapture.load() != -1 && now - optionsPolledAt >= 1000)
        {
            optionsPolledAt = now;
            const auto options = SettingsManager::readSnapshot (LibraryStore::configFile());
            if (options.fingerprint.isNotEmpty())
            {
                applySharedCaptureOptions (options.capture);
                if (! isPreparingSettings() && options.previews.isDirectory())
                {
                    juce::ScopedLock lock (projectLock);
                    if (requestedDestination() != options.previews)
                    {
                        requestedOutputFolder = options.previews;
                        ++outputFolderRevision;
                        desiredOutputFolder = options.previews;
                        publishOutputFolder (desiredOutputFolder);
                    }
                }
            }
        }
        applyCommands();
        applyPreparation();
        applyRestoration();
        if (! threadShouldExit() && ! isPreparingSettings()) consumeHostObservation();
        // Commands arriving during migration override that discovery result
        // before selecting a context for the next capture.
        applyCommands();
        refreshEntryPreferences();
        processReadySlots();
        if (pendingCaptures.load() == 0 && ! capturing.load()) activateOutputFolder (desiredOutputFolder);
        publishProject();
        if (threadShouldExit())
        {
            // A command can arrive while the final batch is blocked in I/O.
            // New diagnostics are rejected after stop, but accepted commands
            // must receive the same shutdown pass as commands in that batch.
            applyCommands();
            break;
        }
        wake.wait (20);
    }
    // Resource/file cleanup can still wait for a stalled I/O operation, but this
    // worker and its module are retained independently of the plugin instance.
    while (spool.isThreadRunning() || discovery.isThreadRunning() || analysisWorker.isThreadRunning())
        juce::Thread::sleep (2);
    queuedAnalysis.reset();
    for (auto& bank : banks) for (auto& slot : bank->slots) discardSlot (slot);
    removePending();
    const auto temporary = juce::File::getSpecialLocation (juce::File::tempDirectory);
    if (captureCache.isAChildOf (temporary)) captureCache.deleteRecursively();
    {
        juce::ScopedLock lock (bankLock);
        currentBank.reset(); banks.clear();
    }
}

void RecordingEngine::Core::processReadySlots()
{
    std::vector<std::shared_ptr<Bank>> available;
    {
        juce::ScopedLock lock (bankLock);
        available = banks;
    }
    for (;;)
    {
        if (contextGeneration != generation.load()) break;
        Slot* next = nullptr;
        for (auto& bank : available)
            for (auto& slot : bank->slots)
                if (slot.state.load (std::memory_order_acquire) == SlotState::Ready
                    && (next == nullptr || slot.sequence < next->sequence)) next = &slot;
        if (next == nullptr) break;
        // A lower sequence can still be draining its PCM. Publishing a newer
        // prefix before that job would let the older capture overwrite it.
        bool earlierDraining = false;
        for (auto& bank : available) for (auto& slot : bank->slots)
        {
            const auto status = slot.state.load (std::memory_order_acquire);
            if ((status == SlotState::Capturing || status == SlotState::Draining)
                && slot.sequence < next->sequence) earlierDraining = true;
        }
        if (earlierDraining) break;
        // Preserve ordering across banks whose settings finish out of order.
        if (next->settings && ! next->settings->complete.load (std::memory_order_acquire)) break;
        auto ready = SlotState::Ready;
        if (! next->state.compare_exchange_strong (ready, SlotState::Encoding)) continue;
        if (next->settings)
        {
            if (next->settings->result.failed())
            {
                fail ("Prepare settings", LibraryStore::configFile(), next->settings->result.getErrorMessage());
                discardSlot (*next);
                continue;
            }
            const auto& settings = next->settings->snapshot;
            next->limit = (int) (next->rate * juce::jlimit (15, 120, settings.seconds));
            next->quality = juce::jlimit (0, 10, settings.quality);
            next->mono = settings.mono;
            next->outputRoot = settings.previews;
            next->settingsRevision = settings.revision;
            next->settingsFingerprint = settings.fingerprint;
        }
        if (next->generation == generation.load())
        {
            if (next->capturedDestination > next->preparedDestination)
                next->outputRoot = outputDestinations[next->capturedDestination];
            activateOutputFolder (next->outputRoot);
            encodeAndSave (*next);
        }
        else if (! associationBlocked.load()) state.store (State::Idle);
        discardSlot (*next);
    }
    {
        juce::ScopedLock lock (bankLock);
        banks.erase (std::remove_if (banks.begin(), banks.end(), [&] (const auto& bank)
        {
            if (bank == currentBank) return false;
            for (auto& slot : bank->slots) if (slot.state.load() != SlotState::Free) return false;
            return true;
        }), banks.end());
    }
}

std::shared_ptr<RecordingEngine::Core::AnalysisJob> RecordingEngine::Core::startAnalysis (
    std::shared_ptr<LibraryStore::AudioRevision> revision, const Slot& slot, int length, int prefix,
    const juce::String& previousRevision, const juce::String& previousFingerprint)
{
    auto job = std::make_shared<AnalysisJob>();
    job->revision = std::move (revision);
    job->channels = slot.mono ? 1 : 2;
    job->hostBpm = slot.bpm > 0;
    job->generation = slot.generation;
    job->source = { job->revision->pcm, job->revision->id, previousRevision, previousFingerprint,
                    slot.rate, length, job->channels, prefix, job->hostBpm };
    std::shared_ptr<AnalysisJob> superseded;
    {
        juce::ScopedLock lock (analysisLock);
        superseded.swap (queuedAnalysis);
        queuedAnalysis = job;
    }
    analysisWorker.notify();
    return job;
}

void RecordingEngine::Core::AnalysisThread::run()
{
    while (! threadShouldExit())
    {
        std::shared_ptr<AnalysisJob> job;
        {
            juce::ScopedLock lock (engine.analysisLock);
            job.swap (engine.queuedAnalysis);
        }
        if (! job) { wait (-1); continue; }
        const auto cancelled = [this, &job]
        { return threadShouldExit() || job->cancelRequested.load() || job->generation != engine.generation.load(); };
        {
            HeavyPermit permit (cancelled);
            if (permit.acquired)
            {
                const auto started = juce::Time::getMillisecondCounterHiRes();
                job->analysis = analysis.analyse (job->source, job->publication, cancelled);
                if (! cancelled() && job->analysis.valid)
                {
                    job->key = job->analysis.key;
                    job->bpm = job->analysis.bpm;
                    job->valid = ! cancelled();
                }
                job->milliseconds = juce::Time::getMillisecondCounterHiRes() - started;
            }
        }
        job->done.store (true, std::memory_order_release);
    }
}

void RecordingEngine::Core::encodeAndSave (Slot& slot, const juce::var& initialMetadata)
{
    const auto cancelled = [&] { return threadShouldExit() || previewsLocked.load() || slot.generation != generation.load(); };
    const auto sampleRate = slot.rate;
    const auto channels = slot.mono ? 1 : 2;
    if (! outputFolder.isDirectory()) { fail ("Open library", outputFolder, "Folder is unavailable"); return; }
    if (! storageOwner) storageOwner = std::make_unique<LibraryStore::OwnerLease>();
    if (! storageOwner->valid()) { fail ("Create owner", outputFolder, "Cannot retain the pending capture owner token"); return; }
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (cancelled()) { state.store (State::Idle); return; }
        HeavyPermit permit (cancelled);
        if (! permit.acquired) { state.store (State::Idle); return; }
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto folder = entryFolder();
        if (auto created = LibraryStore::ensureDirectory (folder); created.failed())
        { fail ("Create entry", folder, created.getErrorMessage()); return; }
        if (! entry.confirmed)
        {
            if (auto claimed = LibraryStore::claimPending (outputFolder, folder, *storageOwner); claimed.failed())
            { fail ("Own pending capture", folder, claimed.getErrorMessage()); return; }
            if (std::none_of (ownedPending.begin(), ownedPending.end(), [&] (const auto& pending) { return pending.folder == folder; }))
                ownedPending.push_back ({ outputFolder, folder });
        }
        LibraryStore::RecoveryResult integrity;
        const auto observedMetadata = loadMetadata (folder);
        const bool hasPreviousStorage = observedMetadata.getProperty ("audioRevision", "").toString().isNotEmpty()
            || folder.getChildFile ("preview.wav").existsAsFile() || folder.getChildFile ("preview.ogg").existsAsFile()
            || ! folder.getChildFile (".audio").findChildFiles (juce::File::findDirectories, false).isEmpty()
            || (folder.getChildFile ("metadata.json").existsAsFile() && observedMetadata.getDynamicObject() == nullptr);
        if (hasPreviousStorage)
        {
            // Never bless a corrupt old PCM tail with a new receipt. Verify or
            // recover the previous complete pair before merging a fresh prefix.
            integrity = LibraryStore::recoverEntry (folder, cancelled);
            if (integrity.result.failed())
            {
                if (cancelled()) { state.store (State::Idle); return; }
                if (attempt < 2 && integrity.result.getErrorMessage().startsWith ("Entry changed during recovery")) continue;
                fail ("Validate previous audio", folder, integrity.result.getErrorMessage()); return;
            }
        }
        const auto previousMetadata = loadMetadata (folder);
        if (previewsLocked.load() || (bool) previousMetadata.getProperty ("previewLocked", false))
        { state.store (entry.confirmed ? State::Done : State::Pending); return; }
        if (hasPreviousStorage && previousMetadata.getProperty ("audioRevision", "").toString() != integrity.revision)
        {
            if (attempt < 2) continue;
            fail ("Validate previous audio", folder, "Audio revision changed during validation; retry the capture"); return;
        }
        const auto pcm = LibraryStore::audioFile (folder, previousMetadata, true);
        const auto ogg = LibraryStore::audioFile (folder, previousMetadata, false);
        const auto previousFile = pcm.existsAsFile() ? pcm : ogg;
        AnalysisAudio::FileStream captured (slot.capturedPcm, sampleRate, cancelled), previous (previousFile, sampleRate, cancelled);
        if (! captured.valid()) { fail ("Read capture", slot.capturedPcm, "Temporary WAV is unreadable"); return; }
        if (previousFile.existsAsFile() && ! previous.valid())
        { fail ("Read previous audio", previousFile, "Unsupported or corrupt preview"); return; }
        const auto prefix = juce::jmin (slot.limit, juce::jmin (slot.count, captured.length()));
        const auto oldLength = ! slot.replace && previous.valid() ? juce::jmin (slot.limit, previous.length()) : 0;
        const auto length = juce::jmax (prefix, oldLength);
        if (length <= 0) { fail ("Read capture", slot.capturedPcm, "Capture is empty"); return; }
        // An analysis reader may outlive a cancelled encoder. Shared ownership
        // keeps this immutable PCM revision until both phases have stopped.
        auto revision = std::make_shared<LibraryStore::AudioRevision> (folder);
        if (auto prepared = revision->prepare(); prepared.failed())
        { fail ("Prepare revision", folder, prepared.getErrorMessage()); return; }
        auto makeWriter = [&] (const juce::File& file, juce::AudioFormat& format, int bits, int quality)
        {
            auto output = std::make_unique<juce::FileOutputStream> (file);
            if (! output->openedOk() || ! output->setPosition (0) || output->truncate().failed())
                return std::unique_ptr<juce::AudioFormatWriter>{};
            std::unique_ptr<juce::OutputStream> stream = std::move (output);
            return format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                .withSampleRate (sampleRate).withNumChannels (channels).withBitsPerSample (bits)
                .withQualityOptionIndex (quality).withSampleFormat (bits == 32
                    ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                    : juce::AudioFormatWriterOptions::SampleFormat::automatic));
        };
        juce::WavAudioFormat wavFormat;
        auto writer = makeWriter (revision->pcm, wavFormat, 32, 0);
        if (! writer) { fail ("Create PCM", revision->pcm, "Cannot open float WAV writer"); return; }
        juce::AudioBuffer<float> fresh (2, 4096), old (2, 4096), merged (channels, 4096);
        const auto fade = oldLength > prefix ? juce::jmin (prefix, (int) std::round (sampleRate * 0.005)) : 0;
        float peak = 0;
        juce::int64 aboveUnity = 0;
        for (int offset = 0; offset < length;)
        {
            if (cancelled()) { state.store (State::Idle); return; }
            const auto count = juce::jmin (4096, length - offset);
            fresh.clear(); old.clear();
            const auto newCount = juce::jlimit (0, count, prefix - offset);
            if ((newCount > 0 && ! captured.read (fresh, newCount))
                || (offset < oldLength && ! previous.read (old, count)))
            { fail ("Merge audio", previousFile, "PCM read failed"); return; }
            for (int i = 0; i < count; ++i)
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto oldValue = channels == 1 ? 0.5f * (old.getSample (0, i) + old.getSample (1, i))
                                                        : old.getSample (ch, i);
                    float value = oldValue;
                    if (i < newCount)
                    {
                        value = channels == 1 ? 0.5f * (fresh.getSample (0, i) + fresh.getSample (1, i))
                                              : fresh.getSample (ch, i);
                        if (fade > 1 && offset + i >= prefix - fade)
                        {
                            const auto mix = (float) (offset + i - (prefix - fade)) / (float) (fade - 1);
                            value += mix * (oldValue - value);
                        }
                    }
                    if (! std::isfinite (value)) value = 0;
                    merged.setSample (ch, i, value);
                    peak = juce::jmax (peak, std::abs (value));
                    if (std::abs (value) > 1) ++aboveUnity;
                }
            if (! writer->writeFromAudioSampleBuffer (merged, 0, count))
            { fail ("Write PCM", revision->pcm, "Float WAV write failed"); return; }
            offset += count;
        }
        writer.reset();
        const auto analysis = startAnalysis (revision, slot, length, prefix, integrity.revision, integrity.pcmFingerprint);
        struct CancelUnusedAnalysis
        {
            std::shared_ptr<AnalysisJob> job;
            ~CancelUnusedAnalysis() { job->cancelRequested.store (true); }
        } cancelUnused { analysis };
        auto gain = peak > 1 ? 0.98f / peak : 1.0f;
        const auto pcmMs = juce::Time::getMillisecondCounterHiRes() - started;
        const auto oggStarted = juce::Time::getMillisecondCounterHiRes();
        juce::OggVorbisAudioFormat oggFormat;
        float decodedPeak = 0;
        int gainPasses = 0;
        bool previewValidated = false;
        for (; gainPasses < 3 && ! previewValidated; ++gainPasses)
        {
            writer = makeWriter (revision->ogg, oggFormat, 16, slot.quality);
            if (! writer) { fail ("Create OGG", revision->ogg, "Cannot open preview writer"); return; }
            AnalysisAudio::FileStream finalAudio (revision->pcm, sampleRate);
            juce::AudioBuffer<float> block (2, 4096), preview (channels, 4096);
            for (int offset = 0; offset < length;)
            {
                if (cancelled()) { state.store (State::Idle); return; }
                const auto count = juce::jmin (4096, length - offset);
                if (! finalAudio.read (block, count)) { fail ("Read PCM for OGG", revision->pcm, "Audio read failed"); return; }
                for (int ch = 0; ch < channels; ++ch) preview.copyFrom (ch, 0, block, ch, 0, count);
                preview.applyGain (gain);
                if (! writer->writeFromAudioSampleBuffer (preview, 0, count))
                { fail ("Write OGG", revision->ogg, "Preview write failed"); return; }
                offset += count;
            }
            writer.reset();
            const auto validation = decodedPreviewPeak (revision->ogg, channels, length, decodedPeak, cancelled);
            if (validation.failed())
            {
                if (cancelled()) { state.store (State::Idle); return; }
                fail ("Validate OGG preview", revision->ogg, validation.getErrorMessage()); return;
            }
            previewValidated = decodedPeak <= 1.0f;
            if (! previewValidated) gain *= 0.98f / decodedPeak;
        }
        if (! previewValidated)
        { fail ("Limit OGG preview", revision->ogg, "Codec still exceeds unity after three gain passes. Lower its input level or increase OGG quality and retry; the previous preview is retained"); return; }
        const auto oggMs = juce::Time::getMillisecondCounterHiRes() - oggStarted;
        // Release before waiting: two encoders must not retain both global
        // permits while their analysis workers wait to acquire one.
        permit.release();
        const auto analysisWaitStarted = juce::Time::getMillisecondCounterHiRes();
        while (! analysis->done.load (std::memory_order_acquire) && ! cancelled()) wait (5);
        if (cancelled()) { state.store (State::Idle); return; }
        if (! analysis->valid)
        { fail ("Analyse audio", revision->pcm, analysis->analysis.error.isNotEmpty() ? analysis->analysis.error : "Analysis source is unreadable or analysis failed"); return; }
        const auto& keyEstimate = analysis->key;
        const auto& bpmEstimate = analysis->bpm;
        const auto key = keyEstimate.key;
        const auto bpm = slot.bpm > 0 ? slot.bpm : bpmEstimate.bpm;
        const auto analysisMs = analysis->milliseconds;
        const auto analysisWaitMs = juce::Time::getMillisecondCounterHiRes() - analysisWaitStarted;
        juce::var metadata = initialMetadata.getDynamicObject() != nullptr ? initialMetadata.clone() : juce::var (new juce::DynamicObject());
        auto* obj = metadata.getDynamicObject();
        obj->setProperty ("projectId", entry.id);
        obj->setProperty ("projectName", entry.name);
        obj->setProperty ("projectPath", entry.path);
        obj->setProperty ("projectFileId", entry.fileId);
        obj->setProperty ("projectFingerprint", entry.hash);
        obj->setProperty ("pending", ! entry.confirmed);
        obj->setProperty ("projectAssociation", associationName (entry.association));
        obj->setProperty ("projectAssociationConfidence", associationConfidence (entry.association));
        obj->setProperty ("owner", storageOwner->proof());
        obj->setProperty ("daw", daw);
        obj->setProperty ("recordedAt", juce::Time::getCurrentTime().toISO8601 (true));
        obj->setProperty ("durationSeconds", (double) length / sampleRate);
        obj->setProperty ("sampleRate", sampleRate);
        obj->setProperty ("channels", channels);
        obj->setProperty ("settingsRevision", slot.settingsRevision);
        obj->setProperty ("settingsFingerprint", slot.settingsFingerprint);
        obj->setProperty ("recordSeconds", (int) std::llround (slot.limit / sampleRate));
        obj->setProperty ("oggQuality", slot.quality);
        obj->setProperty ("recordMono", slot.mono);
        obj->setProperty ("previewUpdatePolicy", slot.replace ? "replace" : "preserve-tail");
        obj->setProperty ("preservedTailSeconds", juce::jmax (0.0, (double) (oldLength - prefix) / sampleRate));
        obj->setProperty ("bpm", bpm);
        obj->setProperty ("bpmSource", slot.bpm > 0 ? "host" : "estimated");
        obj->setProperty ("bpmConfidence", slot.bpm > 0 ? 1.0 : bpmEstimate.confidence);
        obj->setProperty ("bpmAlternative", bpmEstimate.alternative);
        obj->setProperty ("bpmVariable", slot.bpm > 0 ? slot.maximumBpm - slot.minimumBpm > 0.1 : bpmEstimate.possibleVariation);
        obj->setProperty ("bpmMinimum", slot.minimumBpm);
        obj->setProperty ("bpmMaximum", slot.maximumBpm);
        obj->setProperty ("estimatedBpmStart", bpmEstimate.startBpm);
        obj->setProperty ("estimatedBpmEnd", bpmEstimate.endBpm);
        obj->setProperty ("key", key);
        obj->setProperty ("keyConfidence", keyEstimate.confidence);
        obj->setProperty ("analysisSampleRate", AnalysisAudio::rate);
        obj->setProperty ("analysisVersion", PreviewAnalysis::version);
        obj->setProperty ("analysisInputSamples", analysis->analysis.inputSamples);
        obj->setProperty ("analysisKeyComputedFrames", analysis->analysis.keyComputed);
        obj->setProperty ("analysisKeyReusedFrames", analysis->analysis.keyReused);
        obj->setProperty ("analysisBpmComputedFrames", analysis->analysis.bpmComputed);
        obj->setProperty ("analysisBpmReusedFrames", analysis->analysis.bpmReused);
        obj->setProperty ("analysisCacheBytes", (juce::int64) analysis->analysis.cacheBytes);
        obj->setProperty ("previewGain", gain);
        obj->setProperty ("previewDecodedPeak", decodedPeak);
        obj->setProperty ("previewGainPasses", gainPasses);
        obj->setProperty ("previewGainPolicy", "decoded-sample-peak-guard-v1");
        obj->setProperty ("pcmPeak", peak);
        obj->setProperty ("samplesAboveUnity", aboveUnity);
        obj->setProperty ("seamCrossfadeMs", fade > 0 ? 5 : 0);
        if (cancelled()) { state.store (State::Idle); return; }
        // Receipt checksums also stream both files. Bound this disk work by the
        // same process-wide allowance, after releasing/waiting for analysis.
        const auto commitWaitStarted = juce::Time::getMillisecondCounterHiRes();
        HeavyPermit commitPermit (cancelled);
        if (! commitPermit.acquired) { state.store (State::Idle); return; }
        const auto commitStarted = juce::Time::getMillisecondCounterHiRes();
        if (sharedCaptureOptions.load())
        {
            const auto latestOptions = SettingsManager::readSnapshot (LibraryStore::configFile());
            if (latestOptions.fingerprint.isNotEmpty() && latestOptions.capture.locked)
            { applySharedCaptureOptions (latestOptions.capture); state.store (entry.confirmed ? State::Done : State::Pending); return; }
        }
        const auto committed = LibraryStore::commitAudio (folder, *revision, metadata,
            previousMetadata.getProperty ("audioRevision", "").toString(), cancelled);
        const auto commitMs = juce::Time::getMillisecondCounterHiRes() - commitStarted;
        if (committed.failed())
        {
            if (attempt < 2 && committed.getErrorMessage().startsWith ("Audio revision changed")) continue;
            fail ("Commit audio", folder, committed.getErrorMessage()); return;
        }
        analysis->publication->pcmFingerprint = revision->pcmFingerprint;
        analysis->publication->committed.store (true, std::memory_order_release);
        LibraryStore::pruneAudioRevisions (folder);
        {
            juce::ScopedLock lock (resultsLock);
            detectedKey = key;
            lastSavedTime = juce::Time::getCurrentTime().toString (true, true, false, true);
            lastError.clear();
        }
        capturedBpm.store (bpm);
        state.store (entry.confirmed ? State::Done : State::Pending);
        juce::Logger::writeToLog ("Saved id=" + entry.id + " seconds=" + juce::String ((double) length / sampleRate, 2)
            + " pcmMs=" + juce::String (pcmMs, 2) + " oggMs=" + juce::String (oggMs, 2)
            + " analysisMs=" + juce::String (analysisMs, 2) + " analysisWaitMs=" + juce::String (analysisWaitMs, 2) + " totalMs="
            + juce::String (juce::Time::getMillisecondCounterHiRes() - started, 2)
            + " commitWaitMs=" + juce::String (commitStarted - commitWaitStarted, 2) + " commitMs=" + juce::String (commitMs, 2)
            + " captureQueueBytes=" + juce::String ((juce::int64) memoryBudget->load())
            + " analysisInputSamples=" + juce::String (analysis->analysis.inputSamples)
            + " keyFrames=" + juce::String (analysis->analysis.keyComputed) + "/" + juce::String (analysis->analysis.keyReused)
            + " bpmFrames=" + juce::String (analysis->analysis.bpmComputed) + "/" + juce::String (analysis->analysis.bpmReused)
            + " featureCacheBytes=" + juce::String ((juce::int64) PreviewAnalysis::processCacheBytes()));
        return;
    }
}

void RecordingEngine::Core::setOutputFolder (const juce::File& folder)
{
    {
        juce::ScopedLock lock (projectLock);
        requestedOutputFolder = folder;
        ++outputFolderRevision;
        preparationRequest.reset();
        commands.push_back ([this, folder]
        {
            desiredOutputFolder = folder;
            publishOutputFolder (folder);
            if (pendingCaptures.load() == 0 && ! capturing.load()) activateOutputFolder (folder);
        });
    }
    wake.signal();
}
void RecordingEngine::Core::publishOutputFolder (const juce::File& folder)
{
    if (outputDestinations.back() == folder) return;
    outputDestinations.push_back (folder);
    outputDestination.store (outputDestinations.size() - 1);
}
void RecordingEngine::Core::activateOutputFolder (const juce::File& folder)
{
    if (outputFolder == folder) return;
    const auto currentGeneration = generation.load();
    libraryContexts.erase (std::remove_if (libraryContexts.begin(), libraryContexts.end(), [&] (const auto& context)
    { return context.generation != currentGeneration || context.root == outputFolder; }), libraryContexts.end());
    if (outputFolder != juce::File{}) libraryContexts.push_back ({ outputFolder, entry, currentGeneration });
    outputFolder = folder;
    for (const auto& context : libraryContexts)
        if (context.root == folder && context.entry.path == entry.path && context.entry.confirmed == entry.confirmed)
        { entry.id = context.entry.id; break; }
    if (entry.confirmed) adoptExistingEntry (entry.path);
}
void RecordingEngine::Core::setProjectName (const juce::String& name)
{
    {
        juce::ScopedLock lock (projectLock);
        if (publishedEntry.confirmed && publishedEntry.association != Association::ManualRecording) return;
        publishedEntry.name = name.trim();
        const auto requestedName = name.trim();
        commands.push_back ([this, requestedName]
        {
            if (entry.confirmed && entry.association != Association::ManualRecording) return;
            entry.name = requestedName;
            manualName = entry.name.isNotEmpty();
            updateMetadata();
        });
    }
    wake.signal();
}
juce::var RecordingEngine::Core::identityPatch() const
{
    juce::var patch (new juce::DynamicObject()); auto* object = patch.getDynamicObject();
    object->setProperty ("projectId", entry.id); object->setProperty ("projectName", entry.name);
    object->setProperty ("projectPath", entry.path); object->setProperty ("projectFileId", entry.fileId);
    object->setProperty ("projectFingerprint", entry.hash); object->setProperty ("pending", ! entry.confirmed);
    object->setProperty ("projectAssociation", associationName (entry.association));
    object->setProperty ("projectAssociationConfidence", associationConfidence (entry.association));
    return patch;
}
void RecordingEngine::Core::associateProjectFile (const juce::File& file, const juce::String& expectedId)
{
    {
        juce::ScopedLock lock (projectLock);
        const auto id = expectedId.isEmpty() ? publishedEntry.id : expectedId;
        const auto epoch = generation.load();
        commands.push_back ([this, selectedFile = file, id, epoch]
        {
            const auto file = ProjectIdentity::preferredProjectFile (selectedFile);
            if (file == juce::File{})
            { fail ("Associate project file", selectedFile, "Select a primary DAW project outside its backup folder"); return; }
            if (epoch != generation.load() || id != entry.id)
            { fail ("Associate project file", file, "The project changed while choosing a file. Select it again"); return; }
            if (! file.existsAsFile() || ! file.hasFileExtension ("flp;als;rpp;cpr;npr;song;ptx;bwproject;logicx"))
            { fail ("Associate project file", file, "Select an existing supported DAW project file"); return; }
            const auto cancelled = [this, epoch] { return threadShouldExit() || epoch != generation.load(); };
            HostObservation observation; observation.context = entry; observation.generation = epoch;
            observation.explicitAssociation = true; observation.association = Association::ManualFile;
            observation.host = { true, file.getFileNameWithoutExtension(), file.getFullPathName(), {} };
            observation.previousExists = entry.path.isNotEmpty() && juce::File (entry.path).existsAsFile();
            observation.fileId = ProjectIdentity::fileId (file); observation.stamp = file.getLastModificationTime().toMilliseconds();
            {
                HeavyPermit permit (cancelled); if (! permit.acquired) return;
                observation.hash = ProjectIdentity::fingerprint (file, cancelled);
            }
            if (cancelled()) return;
            if (observation.hash.isEmpty() || observation.fileId != ProjectIdentity::fileId (file)
                || observation.stamp != file.getLastModificationTime().toMilliseconds())
            { fail ("Associate project file", file, "The project could not be read consistently. Close its save dialog and retry"); return; }
            for (auto& pending : ownedPending) if (pending.folder.getFileName() == entry.id) pending.preserve = true;
            refreshHostProject (observation);
            if (entry.confirmed && entry.association == Association::ManualFile && entry.verified)
            {
                juce::ScopedLock result (resultsLock);
                if (lastError.startsWith ("Verify manual association") || lastError.startsWith ("Associate project file"))
                { lastError.clear(); if (state.load() == State::Error) state.store (State::Idle); }
            }
        });
    }
    wake.signal();
}
void RecordingEngine::Core::loadOutputFolderFromSettings (SettingsManager::Reader reader)
{
    {
        juce::ScopedLock lock (projectLock);
        const auto revision = outputFolderRevision, epoch = generation.load();
        commands.push_back ([this, reader = std::move (reader), revision, epoch]
        {
            if (threadShouldExit()) return;
            RememberLogger::ensureInitialised ("plugin", juce::String ("Plugin ") + REMEMBER_VERSION_LINE
                + " | " + REMEMBER_BUILD_PROVENANCE_LINE);
            juce::Logger::writeToLog (juce::String ("Processor created. Host: ") + juce::PluginHostType().getHostDescription());
            if (! reader || epoch != generation.load()) return;
            const auto settings = reader();
            if (threadShouldExit() || epoch != generation.load()) return;
            applySharedCaptureOptions (settings.capture);
            {
                juce::ScopedLock result (projectLock);
                // Preparation or an explicit editor choice has newer evidence.
                // A slow constructor read cannot redirect its capture banks.
                if (revision != outputFolderRevision) return;
                requestedOutputFolder = settings.previews;
                ++outputFolderRevision;
            }
            desiredOutputFolder = settings.previews;
            if (pendingCaptures.load() == 0 && ! capturing.load()) activateOutputFolder (desiredOutputFolder);
        });
    }
    wake.signal();
}
void RecordingEngine::Core::keepCurrentRecording (const juce::String& requestedName)
{
    if (! outputFolder.isDirectory())
    {
        entry.confirmed = entry.verified = false; associationBlocked.store (true);
        fail ("Keep recording", outputFolder, "Configure an accessible output library and retry"); return;
    }
    const auto id = entry.id;
    const auto existing = loadMetadata (outputFolder.getChildFile (id));
    entry.name = requestedName.trim().isNotEmpty() ? requestedName.trim()
        : existing.getProperty ("projectName", entry.name).toString();
    if (entry.name.isEmpty()) entry.name = "Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M");
    entry.path.clear(); entry.fileId.clear(); entry.hash.clear(); entry.fileStamp = 0;
    entry.association = Association::ManualRecording; entry.confirmed = true; entry.verified = true;
    entry.associationProblem.clear();
    restored = false; manualName = true;
    if (! storageOwner) storageOwner = std::make_unique<LibraryStore::OwnerLease>();
    // State restored after a crash can own an orphan, but never another live
    // instance. An app-kept ID is already permanent and is reused explicitly.
    const auto inherited = outputFolder.getChildFile (".pending").getChildFile (id);
    if (inherited.isDirectory() && ! inherited.findChildFiles (juce::File::findFiles | juce::File::findDirectories, false).isEmpty()
        && std::none_of (ownedPending.begin(), ownedPending.end(), [&] (const auto& item) { return item.folder == inherited; }))
    {
        if (auto claim = LibraryStore::claimPending (outputFolder, inherited, *storageOwner); claim.failed())
        { entry.confirmed = entry.verified = false; associationBlocked.store (true); fail ("Keep recording", inherited, claim.getErrorMessage()); return; }
        ownedPending.push_back ({ outputFolder, inherited, true });
    }
    bool failed = false;
    for (auto& pending : ownedPending)
        if (pending.folder.getFileName() == id && pending.folder.isDirectory())
        {
            pending.preserve = true;
            const auto result = LibraryStore::promoteOwnedPending (pending.root, pending.folder, *storageOwner, identityPatch());
            if (result.failed()) { failed = true; fail ("Keep recording", pending.folder, result.getErrorMessage()); }
        }
    if (! failed)
    {
        const auto target = entryFolder();
        if (target.getChildFile ("metadata.json").existsAsFile()
            && loadMetadata (target).getProperty ("projectId", "").toString() != id)
        { failed = true; fail ("Keep recording", target, "This directory belongs to another entry; audio was preserved"); }
        else if (auto result = LibraryStore::patchJson (target.getChildFile ("metadata.json"), identityPatch(), true); result.failed())
        { failed = true; fail ("Keep recording", target, result.getErrorMessage()); }
    }
    associationBlocked.store (failed);
    if (failed) { entry.confirmed = entry.verified = false; return; }
    { juce::ScopedLock result (resultsLock); lastError.clear(); }
    state.store (LibraryStore::audioFile (entryFolder(), false).existsAsFile() ? State::Done : State::Idle);
}
void RecordingEngine::Core::keepRecording (const juce::String& name, const juce::String& expectedId)
{
    {
        juce::ScopedLock lock (projectLock);
        const auto id = expectedId.isEmpty() ? publishedEntry.id : expectedId;
        const auto epoch = generation.load();
        commands.push_back ([this, name, id, epoch]
        {
            if (epoch != generation.load() || id != entry.id)
            { fail ("Keep recording", outputFolder, "The project changed. Choose Keep recording again"); return; }
            keepCurrentRecording (name);
        });
    }
    wake.signal();
}
void RecordingEngine::Core::useAutomaticAssociation (const juce::String& expectedId)
{
    {
        juce::ScopedLock lock (projectLock);
        const auto id = expectedId.isEmpty() ? publishedEntry.id : expectedId;
        commands.push_back ([this, id]
        {
            if (entry.id != id) { fail ("Change association", outputFolder, "The project changed. Open the association menu again"); return; }
            contextGeneration = generation.fetch_add (1) + 1; removePending(); entry = Entry{}; restored = manualName = false;
            associationBlocked.store (false); state.store (State::Idle);
            { juce::ScopedLock result (resultsLock); lastError.clear(); }
        });
    }
    discovery.notify(); wake.signal();
}
RecordingEngine::AssociationInfo RecordingEngine::Core::getAssociationInfo() const
{
    juce::ScopedLock lock (projectLock); const auto& project = publishedEntry;
    RecordingEngine::AssociationInfo result;
    result.id = project.id; result.path = project.path; result.source = project.association;
    result.confirmed = project.confirmed; result.verified = project.verified;
    result.confidence = associationConfidence (project.association);
    result.problem = project.associationProblem;
    switch (project.association)
    {
        case Association::HostPath: result.description = "Automatic: host file path"; break;
        case Association::TitleMatch: result.description = "Automatic: title match"; break;
        case Association::ManualFile: result.description = project.verified ? "Manual: selected project file" : "Manual file needs verification"; break;
        case Association::ManualRecording: result.description = project.confirmed ? "Manual: kept without a file" : "Manual recording needs confirmation"; break;
        case Association::Ambiguous: result.description = "Ambiguous: choose a project file"; break;
        default: result.description = "Unassigned recording"; break;
    }
    return result;
}
bool RecordingEngine::Core::mergePreview (const juce::File& previousFolder)
{
    const auto epoch = generation.load();
    const auto cancelled = [this, epoch] { return threadShouldExit() || epoch != generation.load(); };
    {
        HeavyPermit permit (cancelled); if (! permit.acquired) return false;
        if (auto checked = LibraryStore::recoverEntry (previousFolder, cancelled); checked.result.failed())
        { fail ("Read association preview", previousFolder, checked.result.getErrorMessage()); return false; }
    }
    std::shared_ptr<Bank> bank;
    { juce::ScopedLock lock (bankLock); bank = currentBank; }
    if (! bank) return false;
    auto settings = bank->prepared;
    if (bank->settings)
    {
        if (! bank->settings->complete.load (std::memory_order_acquire) || bank->settings->result.failed()) return false;
        settings = bank->settings->snapshot;
    }
    const auto metadata = loadMetadata (previousFolder);
    const auto source = LibraryStore::audioFile (previousFolder, metadata, true);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (! reader || reader->lengthInSamples <= 0) return false;
    Slot copy;
    copy.rate = bank->slots[0].rate; copy.limit = (int) (copy.rate * juce::jlimit (15, 120, settings.seconds));
    copy.quality = juce::jlimit (0, 10, settings.quality); copy.mono = settings.mono;
    copy.settingsRevision = settings.revision;
    copy.settingsFingerprint = settings.fingerprint;
    copy.outputRoot = outputFolder; copy.generation = epoch; copy.capturedPcm = source;
    copy.count = (int) juce::jmin ((double) copy.limit, std::round (reader->lengthInSamples * copy.rate / reader->sampleRate));
    if (metadata.getProperty ("bpmSource", "").toString() == "host") copy.bpm = (double) metadata.getProperty ("bpm", 0.0);
    copy.minimumBpm = (double) metadata.getProperty ("bpmMinimum", copy.bpm);
    copy.maximumBpm = (double) metadata.getProperty ("bpmMaximum", copy.bpm);
    state.store (State::Encoding); encodeAndSave (copy);
    return ! cancelled() && state.load() != State::Error;
}
bool RecordingEngine::Core::forkPreview (const juce::File& previousFolder)
{
    const auto epoch = generation.load();
    const auto cancelled = [this, epoch] { return threadShouldExit() || epoch != generation.load(); };
    {
        HeavyPermit permit (cancelled); if (! permit.acquired) return false;
        if (auto checked = LibraryStore::recoverEntry (previousFolder, cancelled); checked.result.failed())
        { fail ("Read Save As preview", previousFolder, checked.result.getErrorMessage()); return false; }
    }
    auto metadata = loadMetadata (previousFolder);
    const auto source = LibraryStore::audioFile (previousFolder, metadata, true);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (! reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max()) return false;
    Slot copy; copy.rate = reader->sampleRate; copy.limit = copy.count = (int) reader->lengthInSamples;
    copy.mono = reader->numChannels == 1; copy.quality = juce::jlimit (0, 10, (int) metadata.getProperty ("oggQuality", 4));
    copy.generation = epoch; copy.capturedPcm = source; copy.outputRoot = outputFolder;
    copy.settingsRevision = (juce::int64) metadata.getProperty ("settingsRevision", 0);
    copy.settingsFingerprint = metadata.getProperty ("settingsFingerprint", "").toString();
    if (metadata.getProperty ("bpmSource", "").toString() == "host") copy.bpm = (double) metadata.getProperty ("bpm", 0.0);
    copy.minimumBpm = (double) metadata.getProperty ("bpmMinimum", copy.bpm);
    copy.maximumBpm = (double) metadata.getProperty ("bpmMaximum", copy.bpm);
    for (const auto* field : { "pendingRecovery", "pendingRecoveryAuthorized", "pendingRecoveryConfirmed", "pendingRecoveredAt", "audioRecovery", "audioRecoveredAt" })
        metadata.getDynamicObject()->removeProperty (field);
    state.store (State::Encoding); encodeAndSave (copy, metadata);
    return ! cancelled() && state.load() != State::Error;
}
juce::String RecordingEngine::Core::getProjectName() const
{
    juce::ScopedLock lock (projectLock);
    return publishedEntry.name;
}
bool RecordingEngine::Core::isProjectConfirmed() const
{
    juce::ScopedLock lock (projectLock);
    return publishedEntry.confirmed;
}
void RecordingEngine::Core::saveState (juce::XmlElement& xml) const
{
    Entry snapshot;
    juce::File destination;
    {
        juce::ScopedLock lock (projectLock);
        snapshot = publishedEntry;
        destination = requestedDestination();
    }
    xml.setAttribute ("projectId", snapshot.id);
    xml.setAttribute ("projectName", snapshot.name);
    xml.setAttribute ("projectPath", snapshot.path);
    xml.setAttribute ("projectFileId", snapshot.fileId);
    xml.setAttribute ("projectFingerprint", snapshot.hash);
    xml.setAttribute ("projectAssociation", associationName (snapshot.association));
    xml.setAttribute ("manualAssociationConfirmed", snapshot.confirmed);
    xml.setAttribute ("previewsFolder", destination.getFullPathName());
    xml.setAttribute ("manualCapture", manualCapture.load() == 1);
}
void RecordingEngine::Core::restoreState (const juce::XmlElement& xml, SettingsManager::Reader sharedSettings)
{
    // Legacy projects must not inherit a default enabled after they were saved.
    manualCapture.store (xml.getBoolAttribute ("manualCapture", false) ? 1 : 0);
    Entry replacement;
    auto id = xml.getStringAttribute ("projectId");
    if (id.isNotEmpty() && ! juce::Uuid (id).isNull()) replacement.id = juce::Uuid (id).toString();
    replacement.path = xml.getStringAttribute ("projectPath");
    if (! juce::File::isAbsolutePath (replacement.path)) replacement.path.clear();
    replacement.fileId = xml.getStringAttribute ("projectFileId");
    replacement.hash = xml.getStringAttribute ("projectFingerprint");
    const auto source = xml.getStringAttribute ("projectAssociation");
    if (source == "manual-recording" && id.isNotEmpty() && ! juce::Uuid (id).isNull())
    {
        replacement.association = Association::ManualRecording;
        replacement.name = xml.getStringAttribute ("projectName").trim();
        replacement.path.clear(); replacement.fileId.clear(); replacement.hash.clear();
        replacement.confirmed = xml.getBoolAttribute ("manualAssociationConfirmed", true);
        replacement.verified = replacement.confirmed;
    }
    else if (source == "manual-file" && replacement.path.isNotEmpty()
             && (replacement.fileId.isNotEmpty() || replacement.hash.isNotEmpty()))
    {
        replacement.association = Association::ManualFile;
        replacement.name = juce::File (replacement.path).getFileNameWithoutExtension();
    }
    {
        juce::ScopedLock lock (projectLock);
        const auto epoch = generation.fetch_add (1) + 1;
        const auto destination = requestedOutputFolder;
        const auto destinationRevision = outputFolderRevision;
        associationBlocked.store (replacement.association == Association::ManualFile
                                  || (replacement.association == Association::ManualRecording && ! replacement.confirmed));
        publishedEntry = replacement;
        if (replacement.association == Association::ManualRecording)
        { publishedEntry.confirmed = false; publishedEntry.verified = false; }
        commands.push_back ([this, replacement, epoch, destination, destinationRevision, sharedSettings = std::move (sharedSettings)]
        {
            if (epoch != generation.load()) return;
            auto target = destination;
            if (sharedSettings)
            {
                const auto current = sharedSettings();
                if (threadShouldExit() || epoch != generation.load()) return;
                {
                    juce::ScopedLock result (projectLock);
                    if (destinationRevision == outputFolderRevision)
                    { requestedOutputFolder = current.previews; ++outputFolderRevision; preparationRequest.reset(); }
                    // A preparation/selection made while reading remains the
                    // authoritative destination, even if this read is older.
                    target = requestedDestination();
                }
                desiredOutputFolder = target;
            }
            juce::ignoreUnused (target);
            pendingRestoration = std::make_unique<Restoration> (Restoration { replacement, epoch });
        });
    }
    discovery.notify();
    wake.signal();
}
void RecordingEngine::Core::applyRestoration()
{
    if (! pendingRestoration) return;
    if (pendingRestoration->epoch != generation.load()) { pendingRestoration.reset(); return; }
    juce::File target;
    {
        juce::ScopedLock lock (projectLock);
        if (preparationRequest)
        {
            if (! preparationRequest->complete.load (std::memory_order_acquire) || preparationRequest->result.failed()) return;
        }
        target = requestedDestination();
    }
    const auto restoration = std::move (pendingRestoration);
    removePending();
    entry = restoration->replacement;
    contextGeneration = restoration->epoch;
    activateOutputFolder (target);
    restored = true;
    manualName = entry.association == Association::ManualRecording;
    if (entry.association == Association::ManualRecording && entry.confirmed) keepCurrentRecording (entry.name);
    juce::Logger::writeToLog ("Processor state restored.");
}
RecordingEngine::Core::State RecordingEngine::Core::getState() const noexcept
{
    if (capturing.load()) return State::Recording;
    if (captureDisabled.load()) return State::Error;
    if (pendingCaptures.load() > 0) return State::Encoding;
    return state.load();
}
float RecordingEngine::Core::getProgress() const noexcept
{
    auto total = captureLimit.load();
    return total > 0 ? (float) samplesWritten.load() / (float) total : 0;
}
int RecordingEngine::Core::getTotalSeconds() const noexcept
{
    juce::ScopedLock lock (projectLock);
    if (preparationRequest)
    {
        if (! preparationRequest->complete.load (std::memory_order_acquire)) return 0;
        if (preparationRequest->result.wasOk()) return juce::jlimit (15, 120, preparationRequest->snapshot.seconds);
    }
    return (int) recordSeconds.load();
}
juce::String RecordingEngine::Core::getDetectedKey() const
{
    juce::ScopedLock lock (resultsLock);
    return manualDetectedKey.isNotEmpty() ? manualDetectedKey : detectedKey;
}
juce::String RecordingEngine::Core::getLastSavedTime() const
{
    juce::ScopedLock lock (resultsLock);
    return lastSavedTime;
}

juce::String RecordingEngine::Core::getLastError() const
{
    juce::ScopedLock lock (resultsLock);
    return lastError;
}

size_t RecordingEngine::Core::getProcessCaptureBytes() noexcept { return captureBytes.load(); }
juce::String RecordingEngine::Core::getTransportWarning() const
{
    if (entryPreviewLocked.load()) return "This project's preview is locked in app details";
    switch (transportIssue.load())
    {
        case TransportIssue::None: return {};
        case TransportIssue::Unknown: return "Host position is unavailable; use an export with position or replay from zero";
        case TransportIssue::LateStart: return "Playback began after zero; replay exactly from the beginning";
        case TransportIssue::TooShort: return "Capture below 1 second ignored; replay from zero for at least 1 second";
        case TransportIssue::SlotsFull: return "Capture buffers are busy; wait for saving and replay from zero";
        case TransportIssue::QueueOverflow: return "Capture storage could not keep up; retry playback/export at a slower rate";
        case TransportIssue::Budget: return "Capture memory is busy; retry preparation after pending jobs finish";
    }
    return {};
}

void RecordingEngine::Core::setCaptureOptions (CaptureOptions value)
{
    manualCapture.store (value.manual ? 1 : 0); previewsLocked.store (value.locked); replacePreview.store (value.replace);
}
CaptureOptions RecordingEngine::Core::getCaptureOptions() const
{ return { manualCapture.load() == 1, previewsLocked.load(), replacePreview.load() }; }
void RecordingEngine::Core::applySharedCaptureOptions (CaptureOptions value)
{
    int uninitialised = -1;
    manualCapture.compare_exchange_strong (uninitialised, value.manual ? 1 : 0);
    previewsLocked.store (value.locked); replacePreview.store (value.replace);
}

void RecordingEngine::Core::refreshEntryPreferences()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto identity = outputFolder.getFullPathName() + "/" + entry.id;
    if (identity == lastPreferenceId && now - preferencesReadAt < 1000) return;
    const auto metadata = outputFolder != juce::File{} ? loadMetadata (entryFolder()) : juce::var{};
    const auto bpm = (double) metadata.getProperty ("bpmOverride", 0.0);
    manualDetectedBpm.store (std::isfinite (bpm) && bpm > 0 ? bpm : 0);
    entryPreviewLocked.store ((bool) metadata.getProperty ("previewLocked", false));
    {
        juce::ScopedLock lock (resultsLock);
        manualDetectedKey = metadata.getProperty ("keyOverride", "").toString();
    }
    preferencesReadAt = now; lastPreferenceId = identity;
}
