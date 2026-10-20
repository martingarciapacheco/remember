#include "../Shared/PreviewAudio.h"
#include "StressTests.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>
#if defined(__SANITIZE_ADDRESS__)
 #include <sanitizer/allocator_interface.h>
#endif
#if JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
#endif

namespace
{
constexpr size_t mib = 1024 * 1024;
void check (bool ok, const juce::String& reason) { if (! ok) throw std::runtime_error (reason.toStdString()); }
void check (bool ok, const char* reason) { if (! ok) throw std::runtime_error (reason); }
class PhaseLogger final : public juce::Logger
{
public:
    PhaseLogger()
    {
        check (juce::Logger::getCurrentLogger() == nullptr, "Stress phase logging requires its isolated CLI");
        juce::Logger::setCurrentLogger (this);
    }
    ~PhaseLogger() override
    {
        if (juce::Logger::getCurrentLogger() == this) juce::Logger::setCurrentLogger (nullptr);
    }
    void logMessage (const juce::String& message) override
    {
        if (message.startsWith ("Saved id="))
        {
            juce::ScopedLock guard (outputLock);
            std::cout << "CAPTURE_PHASES " << message << std::endl;
        }
    }
private:
    juce::CriticalSection outputLock;
};
void enablePhaseLogging()
{
    // Keep the logger alive through the CLI's native retirement barrier, also
    // on a failed fixture. A stack logger could outlive neither a retained core
    // nor a log call already in progress when an assertion throws.
    static PhaseLogger logger;
}
template <class Condition> void waitFor (Condition condition, int milliseconds, const char* reason)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + milliseconds;
    while (! condition()) { check (juce::Time::getMillisecondCounterHiRes() < end, reason); juce::Thread::sleep (5); }
}
struct Memory
{
    size_t privateBytes = 0, peakCommit = 0, workingSet = 0, peakWorkingSet = 0;
    size_t instrumentedLiveBytes = 0, instrumentedHeapBytes = 0;
};
Memory memory()
{
    Memory result;
   #if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters {};
    check (GetProcessMemoryInfo (GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*> (&counters), sizeof (counters)) != 0,
           "Cannot read Windows process memory counters");
    result = { counters.PrivateUsage, counters.PeakPagefileUsage, counters.WorkingSetSize, counters.PeakWorkingSetSize };
   #endif
   #if defined(__SANITIZE_ADDRESS__)
    result.instrumentedLiveBytes = __sanitizer_get_current_allocated_bytes();
    result.instrumentedHeapBytes = __sanitizer_get_heap_size();
   #endif
    return result;
}
struct Head : juce::AudioPlayHead
{
    bool playing = true;
    juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info; info.setIsPlaying (playing); info.setTimeInSamples (sample); info.setBpm (120.0); return info;
    }
};
}

void testSustainedCapture (const juce::File& root)
{
    enablePhaseLogging();
    // Separate from the short regression matrix. Real wall time, four pipelines,
    // real files; callback timing excludes fixture filling/pacing/memory queries.
    constexpr int rate = 192000, blockSize = 512, seconds = 120, instances = 4;
    const auto baseline = memory();
    const auto initialCaptureBytes = RecordingEngine::getProcessCaptureBytes();
    std::vector<std::unique_ptr<RecordingEngine>> engines;
    std::vector<juce::File> libraries;
    for (int i = 0; i < instances; ++i)
    {
        const auto library = root.getChildFile ("stress-" + juce::String (i));
        check (library.createDirectory().wasOk(), "Cannot create sustained capture library");
        const auto project = root.getChildFile ("Stress-" + juce::String (i) + ".rpp");
        check (project.replaceWithText ("sustained capture fixture"), "Cannot create sustained project fixture");
        auto engine = std::make_unique<RecordingEngine> ([project]
        { return HostProject { true, project.getFileNameWithoutExtension(), project.getFullPathName(), "REAPER" }; });
        SettingsManager::Snapshot settings; settings.seconds = seconds; settings.mono = (i % 2) == 1;
        settings.previews = library; settings.fingerprint = "sustained-" + juce::String (i);
        engine->prepareAsync (rate, blockSize, [settings] { return settings; });
        waitFor ([&] { return engine->isProjectConfirmed(); }, 15000, "Sustained project discovery timed out");
        libraries.push_back (library); engines.push_back (std::move (engine));
    }
    const auto captureBytes = RecordingEngine::getProcessCaptureBytes() - initialCaptureBytes;
    check (captureBytes <= instances * 16 * mib, "Sustained capture exceeded the bank budget");
    std::vector<double> times; times.reserve ((size_t) instances * rate * seconds / blockSize + instances);
    juce::AudioBuffer<float> input (2, blockSize);
    Head head;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    double nextMemorySample = start, maxDeliveryLateness = 0;
    size_t samples = 0;
    Memory peak = baseline;
    auto sampleMemory = [&]
    {
        const auto m = memory(); ++samples;
        peak.privateBytes = juce::jmax (peak.privateBytes, m.privateBytes);
        peak.peakCommit = juce::jmax (peak.peakCommit, m.peakCommit);
        peak.peakWorkingSet = juce::jmax (peak.peakWorkingSet, m.peakWorkingSet);
        peak.instrumentedLiveBytes = juce::jmax (peak.instrumentedLiveBytes, m.instrumentedLiveBytes);
    };
    for (int position = 0; position < rate * seconds; position += blockSize)
    {
        const auto deadline = start + position * 1000.0 / rate;
        for (;;)
        {
            const auto remaining = deadline - juce::Time::getMillisecondCounterHiRes();
            if (remaining <= 0) break;
            if (remaining >= 1) juce::Thread::sleep (1); else juce::Thread::yield();
        }
        maxDeliveryLateness = juce::jmax (maxDeliveryLateness, juce::Time::getMillisecondCounterHiRes() - deadline);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto tone = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (position + i) / rate);
            input.setSample (0, i, (float) (1.25 * tone)); input.setSample (1, i, (float) (0.25 * tone));
        }
        head.sample = position;
        for (const auto& engine : engines)
        {
            const auto begun = juce::Time::getMillisecondCounterHiRes();
            engine->processBlock (input, &head);
            times.push_back (juce::Time::getMillisecondCounterHiRes() - begun);
            if (engine->getDroppedCaptures() != 0 || engine->getState() == RecordingEngine::State::Error)
                check (false, "Sustained capture lost audio: " + engine->getLastError());
        }
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now >= nextMemorySample) { sampleMemory(); nextMemorySample = now + 50; }
    }
    head.playing = false; head.sample = rate * seconds;
    for (const auto& engine : engines) engine->processBlock (input, &head);
    const auto capturedAt = juce::Time::getMillisecondCounterHiRes();
    const auto end = capturedAt + 120000;
    bool done = false;
    while (! done)
    {
        done = true;
        for (const auto& engine : engines)
        {
            if (engine->getState() == RecordingEngine::State::Error)
                check (false, "Sustained save failed: " + engine->getLastError());
            done = done && engine->getState() == RecordingEngine::State::Done;
        }
        sampleMemory();
        check (juce::Time::getMillisecondCounterHiRes() < end, "Sustained encoding/analysis/receipt commit timed out");
        if (! done) juce::Thread::sleep (20);
    }
    const auto savedAt = juce::Time::getMillisecondCounterHiRes();
    juce::int64 pcmBytes = 0, oggBytes = 0;
    for (int i = 0; i < instances; ++i)
    {
        juce::File folder;
        for (const auto& candidate : libraries[(size_t) i].findChildFiles (juce::File::findDirectories, false))
            if (! candidate.getFileName().startsWithChar ('.')) { check (folder == juce::File{}, "Sustained capture created duplicate entries"); folder = candidate; }
        check (folder != juce::File{}, "Sustained capture did not publish an entry");
        const auto m = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
        check ((double) m.getProperty ("durationSeconds", 0) == seconds, "Sustained capture was truncated");
        juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
        {
            const auto file = LibraryStore::audioFile (folder, m, false);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            check (reader && reader->lengthInSamples == (juce::int64) rate * seconds && reader->sampleRate == rate
                && reader->numChannels == (i % 2 == 1 ? 1u : 2u), "Sustained preview rate, duration or channels changed");
            juce::AudioBuffer<float> probe (2, 1);
            check (reader->read (&probe, 0, 1, reader->lengthInSamples - 1, true, true), "Sustained audio tail is unreadable");
            check (! file.getSiblingFile ("preview.wav").existsAsFile(), "Sustained capture retained permanent PCM");
            oggBytes += file.getSize();
        }
        check (LibraryStore::recoverEntry (folder).result.wasOk(), "Sustained published audio failed receipt verification");
    }
    sampleMemory();
   #if defined(__SANITIZE_ADDRESS__)
    // MSVC ASan retains allocator/shadow pages after free. Its live allocation
    // counter tests the application's heap; OS commit is still reported.
    check (peak.instrumentedLiveBytes <= baseline.instrumentedLiveBytes + 512 * mib,
           "Sustained live allocations exceeded the fixture regression limit");
   #elif JUCE_WINDOWS
    // A regression guard for this four-instance fixture, not a process-wide
    // product guarantee. Includes codec/analysis/threads/JUCE, not just rings.
    check (peak.peakCommit <= baseline.privateBytes + 512 * mib, "Sustained process memory exceeded the fixture regression limit");
   #endif
    engines.clear();
    waitFor ([&] { return RecordingEngine::getProcessCaptureBytes() == initialCaptureBytes && BackgroundRetirement::pending() == 0; },
             15000, "Sustained capture resources did not retire");
    const auto after = memory();
   #if defined(__SANITIZE_ADDRESS__)
    check (after.instrumentedLiveBytes <= baseline.instrumentedLiveBytes + 8 * mib,
           "Sustained live allocations did not return after native retirement");
   #endif
    std::sort (times.begin(), times.end());
    std::cout << "STRESS seconds=" << seconds << " rate=" << rate << " block=" << blockSize << " instances=" << instances
        << " captureWallMs=" << capturedAt - start << " saveAfterCaptureMs=" << savedAt - capturedAt
        << " callbacks=" << times.size() << " p99Ms=" << times[(size_t) (times.size() * 0.99)]
        << " p999Ms=" << times[(size_t) (times.size() * 0.999)] << " maxCallbackMs=" << times.back()
        << " maxFixtureDeliveryLatenessMs=" << maxDeliveryLateness << " captureBytes=" << captureBytes
        << " baselinePrivateBytes=" << baseline.privateBytes << " sampledPeakPrivateBytes=" << peak.privateBytes
        << " osPeakCommitBytes=" << peak.peakCommit << " osPeakWorkingSetBytes=" << peak.peakWorkingSet
        << " afterPrivateBytes=" << after.privateBytes << " memorySamples=" << samples
        << " asanBaselineLiveBytes=" << baseline.instrumentedLiveBytes << " asanPeakLiveBytes=" << peak.instrumentedLiveBytes
        << " asanAfterLiveBytes=" << after.instrumentedLiveBytes << " asanRetainedHeapBytes=" << after.instrumentedHeapBytes
        << " pcmBytes=" << pcmBytes << " oggBytes=" << oggBytes << std::endl;
    std::cout << "PASS: 120 seconds of four concurrent 192 kHz mono/stereo captures, complete tail/receipts, no drops and retired capture reservations\n";
    testLongCaptureLifecycle (root);
}

void testLongCaptureLifecycle (const juce::File& root)
{
    enablePhaseLogging();
    const auto lifecycleStarted = juce::Time::getMillisecondCounterHiRes();
    struct Gate
    {
        juce::WaitableEvent entered, release { true };
        std::atomic<bool> hold { false };
    };
    struct Release
    {
        std::shared_ptr<Gate> gate;
        ~Release() { gate->release.signal(); }
    };
    const auto fixture = root.getChildFile ("long-capture-lifecycle");
    const auto a = fixture.getChildFile ("library-A"), b = fixture.getChildFile ("library-B");
    check (a.createDirectory().wasOk() && b.createDirectory().wasOk(), "Cannot create long-capture libraries");
    const auto project = fixture.getChildFile ("LongCapture.rpp");
    check (project.replaceWithText ("long capture lifecycle project"), "Cannot create long-capture project");
    auto discovery = std::make_shared<Gate>(); Release releaseDiscovery { discovery };
    auto settings = std::make_shared<Gate>(); Release releaseSettings { settings };
    const auto initialBytes = RecordingEngine::getProcessCaptureBytes();
    const auto baseline = memory(); auto peak = baseline;
    size_t maximumReservation = 0;
    auto sampleMemory = [&]
    {
        const auto current = memory();
        peak.privateBytes = juce::jmax (peak.privateBytes, current.privateBytes);
        peak.peakCommit = juce::jmax (peak.peakCommit, current.peakCommit);
        peak.peakWorkingSet = juce::jmax (peak.peakWorkingSet, current.peakWorkingSet);
        peak.instrumentedLiveBytes = juce::jmax (peak.instrumentedLiveBytes, current.instrumentedLiveBytes);
        maximumReservation = juce::jmax (maximumReservation, RecordingEngine::getProcessCaptureBytes() - initialBytes);
    };
    auto engine = std::make_unique<RecordingEngine> ([project, discovery]
    {
        if (discovery->hold.load()) { discovery->entered.signal(); discovery->release.wait (300000); }
        return HostProject { true, "LongCapture", project.getFullPathName(), "REAPER" };
    });
    SettingsManager::Snapshot older; older.previews = a; older.seconds = 120;
    older.quality = 4; older.revision = 50; older.fingerprint = "long-lifecycle-A";
    auto newer = older; newer.previews = b; newer.mono = true; newer.quality = 8;
    newer.revision = 51; newer.fingerprint = "long-lifecycle-B";
    engine->prepareAsync (192000, 512, [older] { return older; });
    waitFor ([&] { return engine->isProjectConfirmed(); }, 15000, "Long-capture association did not finish");
    const auto id = engine->getAssociationInfo().id;
    const auto folderA = a.getChildFile (id), folderB = b.getChildFile (id);
    auto render = [&] (int rate, int seconds, float left, float right, bool stop)
    {
        Head head; juce::AudioBuffer<float> audio (2, 512);
        for (int position = 0; position < rate * seconds; position += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto tone = (float) std::cos (juce::MathConstants<double>::twoPi * 400 * (position + i) / rate);
                audio.setSample (0, i, left * tone); audio.setSample (1, i, right * tone);
            }
            head.sample = position; engine->processBlock (audio, &head, true);
            if (position % (512 * 128) == 0) sampleMemory();
            if (engine->getDroppedCaptures() != 0 || engine->getState() == RecordingEngine::State::Error)
                check (false, "Long capture failed: " + engine->getLastError());
        }
        if (stop) { head.sample = rate * seconds; head.playing = false; engine->processBlock (audio, &head); }
    };
    auto saved = [&]
    {
        waitFor ([&]
        {
            sampleMemory();
            if (engine->getState() == RecordingEngine::State::Error)
                check (false, "Long-capture commit failed: " + engine->getLastError());
            return engine->getState() == RecordingEngine::State::Done;
        }, 120000, "Long-capture commit did not finish");
    };
    auto revision = [] (const juce::File& folder)
    { return LibraryStore::readJson (folder.getChildFile ("metadata.json")).getProperty ("audioRevision", "").toString(); };
    render (192000, 120, 0.2f, 0.2f, true); saved();
    std::cout << "LONG_LIFECYCLE firstPublicationMs=" << juce::Time::getMillisecondCounterHiRes() - lifecycleStarted << std::endl;
    const auto original = revision (folderA);
    render (192000, 75, 0.4f, 0.4f, false);
    check (engine->getState() == RecordingEngine::State::Recording, "Long partial capture ended before preparation");
    const auto preparing = juce::Time::getMillisecondCounterHiRes();
    engine->prepareAsync (96000, 512, [newer, settings]
    { settings->entered.signal(); settings->release.wait (300000); return newer; });
    const auto prepareMs = juce::Time::getMillisecondCounterHiRes() - preparing;
    check (prepareMs < 200 && settings->entered.wait (1000), "Long prepare waited for the older bank/settings");
    sampleMemory();
    render (96000, 60, 0.7f, 0.1f, true);
    waitFor ([&]
    {
        sampleMemory();
        if (engine->getState() == RecordingEngine::State::Error)
            check (false, "Retired long bank failed: " + engine->getLastError());
        return revision (folderA) != original;
    }, 120000, "An older bank could not commit while newer settings were held");
    std::cout << "LONG_LIFECYCLE retiredPublicationMs=" << juce::Time::getMillisecondCounterHiRes() - lifecycleStarted << std::endl;
    check (engine->isPreparingSettings() && revision (folderB).isEmpty(), "A long capture published unconfirmed new settings");
    settings->release.signal(); saved();
    std::cout << "LONG_LIFECYCLE newPublicationMs=" << juce::Time::getMillisecondCounterHiRes() - lifecycleStarted << std::endl;
    auto probe = [] (const juce::File& folder, int rate, int channels, int seconds, int offset, float level)
    {
        juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
        juce::AudioBuffer<float> sample (channels, 1);
        check (reader && reader->sampleRate == rate && reader->numChannels == (unsigned) channels
               && reader->lengthInSamples == (juce::int64) rate * seconds && reader->read (&sample, 0, 1, offset, true, true)
               && std::abs (sample.getSample (0, 0) - level) < 0.025f, "Long preparation changed rate/channels/prefix/tail");
    };
    probe (folderA, 192000, 2, 120, 96000, 0.4f);
    probe (folderA, 192000, 2, 120, 192000 * 100, 0.2f);
    probe (folderA, 192000, 2, 120, 192000 * 120 - 96000, 0.2f);
    probe (folderB, 96000, 1, 60, 48000, 0.4f);
    probe (folderB, 96000, 1, 60, 96000 * 60 - 48000, 0.4f);
    const auto metaA = LibraryStore::readJson (folderA.getChildFile ("metadata.json"));
    const auto metaB = LibraryStore::readJson (folderB.getChildFile ("metadata.json"));
    check (metaA.getProperty ("settingsFingerprint", "").toString() == older.fingerprint
           && metaB.getProperty ("settingsFingerprint", "").toString() == newer.fingerprint
           && (int) metaA.getProperty ("oggQuality", 0) == 4 && (int) metaB.getProperty ("oggQuality", 0) == 8,
           "Long banks mixed configuration versions");
    check (LibraryStore::recoverEntry (folderA).result.wasOk() && LibraryStore::recoverEntry (folderB).result.wasOk(),
           "Long preparation published an invalid audio pair");
    const auto beforeClose = revision (folderB);
    discovery->hold.store (true);
    check (discovery->entered.wait (3000), "Cannot hold discovery for long-capture closure");
    render (96000, 90, 0.8f, 0.2f, false);
    check (engine->getState() == RecordingEngine::State::Recording, "Long capture ended before closing");
    const auto closing = juce::Time::getMillisecondCounterHiRes(); engine.reset();
    const auto closeMs = juce::Time::getMillisecondCounterHiRes() - closing;
    check (closeMs < 100 && BackgroundRetirement::pending() > 0 && RecordingEngine::getProcessCaptureBytes() > initialBytes,
           "Closing a long capture waited for or released a retained worker/bank");
    discovery->release.signal();
    waitFor ([&] { return BackgroundRetirement::pending() == 0 && RecordingEngine::getProcessCaptureBytes() == initialBytes; },
             15000, "Closed long-capture resources did not retire");
    check (revision (folderB) == beforeClose && LibraryStore::recoverEntry (folderB).result.wasOk(),
           "Closing a long capture changed the previous confirmed audio");
    probe (folderB, 96000, 1, 60, 0, 0.4f);
    probe (folderB, 96000, 1, 60, 96000 * 60 - 1, 0.4f);
    sampleMemory(); const auto after = memory();
    check (maximumReservation <= 64 * mib, "Long reprepare exceeded the instance reservation budget");
    std::cout << "LONG_LIFECYCLE preparedDuringSamples=" << 192000 * 75 << " closedDuringSamples=" << 96000 * 90
        << " prepareMs=" << prepareMs << " closeMs=" << closeMs << " maxReservedBytes=" << maximumReservation
        << " baselinePrivateBytes=" << baseline.privateBytes << " peakPrivateBytes=" << peak.privateBytes
        << " osPeakCommitBytes=" << peak.peakCommit << " osPeakWorkingSetBytes=" << peak.peakWorkingSet
        << " asanBaselineLiveBytes=" << baseline.instrumentedLiveBytes << " asanPeakLiveBytes=" << peak.instrumentedLiveBytes
        << " asanAfterLiveBytes=" << after.instrumentedLiveBytes << " asanRetainedHeapBytes=" << after.instrumentedHeapBytes << std::endl;
   #if defined(__SANITIZE_ADDRESS__)
    check (peak.instrumentedLiveBytes <= baseline.instrumentedLiveBytes + 512 * mib,
           "Long-capture live allocations exceeded the fixture limit");
    check (after.instrumentedLiveBytes <= baseline.instrumentedLiveBytes + 8 * mib,
           "Long-capture live allocations did not return after native retirement");
   #elif JUCE_WINDOWS
    check (peak.peakCommit <= juce::jmax (baseline.peakCommit, baseline.privateBytes + 512 * mib),
           "Long-capture process memory exceeded the fixture limit");
   #endif
    std::cout << "PASS: reprepare during a 75-second capture retains a 120-second tail; held new settings, 192/96 kHz libraries/mono, close during 90-second capture and native retirement\n";
}
