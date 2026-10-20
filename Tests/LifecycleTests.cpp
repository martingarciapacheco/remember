#include "../Shared/PreviewAudio.h"
#include "LifecycleTests.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Plugin/Source/PluginProcessor.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/RememberLogger.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
void check (bool value, const char* reason) { if (! value) throw std::runtime_error (reason); }
template <typename Condition> void await (Condition condition, const char* reason,
                                         const std::function<std::string()>& diagnostics = {})
{
    const auto end = juce::Time::getMillisecondCounter() + 5000;
    while (! condition())
    {
        if (juce::Time::getMillisecondCounter() >= end)
            throw std::runtime_error (std::string (reason) + (diagnostics ? diagnostics() : ""));
        juce::Thread::sleep (2);
    }
}
// Hold a real metadata transaction so queue saturation doesn't depend on the
// speed of a decoder, disk, or thread scheduling.
struct BlockedCommit
{
    explicit BlockedCommit (juce::File file) : worker ([this, file]
    {
        result = LibraryStore::updateJson (file, [this] (juce::DynamicObject&)
        {
            entered.signal(); release.wait (10000); return juce::Result::ok();
        });
    }) {}
    ~BlockedCommit() { release.signal(); if (worker.joinable()) worker.join(); }
    juce::WaitableEvent entered, release { true };
    juce::Result result { juce::Result::ok() };
    std::thread worker;
};
struct Head : juce::AudioPlayHead
{
    bool playing = true, known = true;
    double time = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setIsPlaying (playing);
        if (known) position.setTimeInSeconds (time);
        return position;
    }
};
void capture (RecordingEngine& engine, double rate, float value, double start = 0)
{
    Head head;
    for (int done = 0, total = (int) (rate * 1.2); done < total;)
    {
        const auto count = juce::jmin (128, total - done);
        juce::AudioBuffer<float> audio (2, count);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < count; ++i)
            audio.setSample (ch, i, value * (float) std::cos (juce::MathConstants<double>::twoPi * (done + i) / 100));
        head.time = start + done / rate;
        engine.processBlock (audio, &head);
        done += count;
    }
    head.playing = false;
    juce::AudioBuffer<float> stopped (2, 128); stopped.clear();
    engine.processBlock (stopped, &head);
}
}

void testLifecycle (const juce::File& root)
{
   #if JUCE_WINDOWS
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const juce::File fixture (REMEMBER_LIFETIME_FIXTURE_PATH);
        auto module = LoadLibraryW (fixture.getFullPathName().toWideCharPointer());
        check (module != nullptr, "Cannot load retirement DLL fixture");
        using Start = void (__cdecl*) (HANDLE, HANDLE);
        const auto start = reinterpret_cast<Start> (GetProcAddress (module, "retireBlockedJob"));
        check (start != nullptr, "Retirement fixture export is missing");
        auto gate = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        auto cleaned = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        check (gate && cleaned, "Cannot create retirement fixture events");
        start (gate, cleaned);
        check (FreeLibrary (module) != FALSE, "Cannot release simulated host DLL reference");
        check (GetModuleHandleW (fixture.getFileName().toWideCharPointer()) != nullptr,
               "DLL unloaded while its blocked worker was executing");
        SetEvent (gate);
        check (WaitForSingleObject (cleaned, 5000) == WAIT_OBJECT_0, "Retirement DLL did not destroy its completed job");
        const auto end = juce::Time::getMillisecondCounter() + 5000;
        while (GetModuleHandleW (fixture.getFileName().toWideCharPointer()) != nullptr)
        {
            check (juce::Time::getMillisecondCounter() < end, "Retirement kept the DLL permanently loaded");
            juce::Thread::sleep (2);
        }
        CloseHandle (gate); CloseHandle (cleaned);
    }
    std::cout << "PASS: eight actual DLL unloads retain blocked workers and release the module after native completion\n";
   #endif
    {
        struct Gate { juce::WaitableEvent entered, release { true }; };
        auto gate = std::make_shared<Gate>();
        const auto baseline = RecordingEngine::getProcessCaptureBytes();
        auto closing = std::make_unique<RecordingEngine> ([gate]
        {
            gate->entered.signal(); gate->release.wait (10000);
            return HostProject{};
        });
        struct Unblock { std::shared_ptr<Gate> gate; ~Unblock() { gate->release.signal(); } } unblock { gate };
        check (gate->entered.wait (1000), "Close fixture did not enter blocked discovery");
        SettingsManager::Snapshot settings;
        closing->prepare (8000, 128, settings);
        capture (*closing, 8000, 0.2f);
        const auto begin = juce::Time::getMillisecondCounterHiRes();
        closing.reset();
        const auto closeMs = juce::Time::getMillisecondCounterHiRes() - begin;
        check (closeMs < 200, "Plugin destruction waited for blocked background I/O");
        check (BackgroundRetirement::pending() > 0 && RecordingEngine::getProcessCaptureBytes() > baseline,
               "Blocked worker buffers were freed before the worker stopped");
        gate->release.signal();
        const auto end = juce::Time::getMillisecondCounter() + 5000;
        while (RecordingEngine::getProcessCaptureBytes() != baseline || BackgroundRetirement::pending() > 0)
        {
            check (juce::Time::getMillisecondCounter() < end, "Cancelled engine leaked capture resources");
            juce::Thread::sleep (2);
        }
        std::cout << "PASS: plugin destruction during blocked discovery in " << closeMs << " ms; deferred resources released\n";
    }
    {
        struct Gate { juce::WaitableEvent entered, release { true }; std::atomic<int> calls { 0 }; };
        auto gate = std::make_shared<Gate>();
        auto oldProject = root.getChildFile ("Obsolete.flp"); oldProject.replaceWithText ("old project");
        auto currentProject = root.getChildFile ("Current.flp"); currentProject.replaceWithText ("current project");
        RecordingEngine restoring ([gate, oldProject, currentProject]
        {
            if (gate->calls.fetch_add (1) == 0)
            {
                gate->entered.signal(); gate->release.wait (10000);
                return HostProject { true, "Obsolete", oldProject.getFullPathName(), "Test host" };
            }
            return HostProject { true, "Current", currentProject.getFullPathName(), "Test host" };
        });
        struct Unblock { std::shared_ptr<Gate> gate; ~Unblock() { gate->release.signal(); } } unblock { gate };
        check (gate->entered.wait (1000), "Restore fixture did not block discovery");
        const auto library = root.getChildFile ("restore-generation-library"); library.createDirectory();
        restoring.setOutputFolder (library);
        juce::XmlElement state ("RememberState");
        const auto id = juce::Uuid().toString();
        state.setAttribute ("projectId", id);
        state.setAttribute ("projectPath", currentProject.getFullPathName());
        restoring.restoreState (state);
        restoring.prepare (8000, 128, SettingsManager::Snapshot{});
        gate->release.signal();
        await ([&] { return restoring.isProjectConfirmed() && restoring.getProjectName() == "Current"; },
               "An obsolete discovery result replaced restored context");
        restoring.saveState (state);
        check (state.getStringAttribute ("projectId") == id
               && state.getStringAttribute ("projectPath") == currentProject.getFullPathName(),
               "Discovery restored an obsolete identity/path");
        std::cout << "PASS: generation rejects delayed discovery results after state restoration\n";
    }
    {
        const auto library = root.getChildFile ("concurrent-pipelines"); library.createDirectory();
        std::vector<std::unique_ptr<RecordingEngine>> engines;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 4; ++i)
        {
            auto engine = std::make_unique<RecordingEngine> ([] { return HostProject{}; });
            engine->setOutputFolder (library);
            engine->setProjectName ("Concurrent " + juce::String (i));
            engine->prepare (8000, 128, SettingsManager::Snapshot{});
            capture (*engine, 8000, 0.2f * (i + 1));
            engines.push_back (std::move (engine));
        }
        await ([&]
        {
            for (const auto& engine : engines) if (engine->getState() != RecordingEngine::State::Pending) return false;
            return true;
        }, "Concurrent pipelines did not finish within five seconds", [&]
        {
            juce::String detail;
            for (size_t i = 0; i < engines.size(); ++i)
                detail += "\nengine=" + juce::String ((int) i) + " state=" + juce::String ((int) engines[i]->getState())
                    + " dropped=" + juce::String ((juce::int64) engines[i]->getDroppedCaptures())
                    + " warning=" + engines[i]->getTransportWarning() + " error=" + engines[i]->getLastError();
            return detail.toStdString();
        });
        for (int i = 0; i < 4; ++i)
        {
            juce::XmlElement state ("RememberState"); engines[(size_t) i]->saveState (state);
            const auto folder = library.getChildFile (".pending").getChildFile (state.getStringAttribute ("projectId"));
            const auto metadata = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
            check (metadata.getProperty ("projectName", "").toString() == "Concurrent " + juce::String (i),
                   "Concurrent pipelines mixed project identities");
            juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
            juce::AudioBuffer<float> audio (2, 1);
            // Probe the settled tone, away from Vorbis startup transients.
            check (reader && reader->lengthInSamples == 9600 && reader->read (&audio, 0, 1, 4000, true, true)
                   && std::abs (audio.getSample (0, 0) - 0.2f * (i + 1)) < 0.025f,
                   "Concurrent pipelines committed another instance's audio");
        }
        std::cout << "PASS: four concurrent encoding/analysis pipelines complete with isolated identities/audio in "
                  << juce::Time::getMillisecondCounterHiRes() - started << " ms\n";
    }
    const auto log = root.getChildFile ("rotation.log");
    {
        RememberLogger::RotatingLogger logger (log);
        for (int i = 0; i < 600; ++i) logger.logMessage (juce::String (i) + " " + juce::String::repeatedString ("x", 8000));
    }
    check (log.loadFileAsString().contains ("599 "), "Rotated logger lost the final message");
    const auto logs = root.findChildFiles (juce::File::findFiles, false, "rotation.log*");
    check (logs.size() == 4, "Logger didn't bound retention to four files");
    for (const auto& file : logs) check (file.getSize() <= RememberLogger::RotatingLogger::limit, "Log exceeded its session limit");
    struct Gate { juce::WaitableEvent entered, release { true }; };
    auto gate = std::make_shared<Gate>();
    RecordingEngine engine ([gate]
    {
        gate->entered.signal(); gate->release.wait (10000);
        return HostProject { false, {}, {}, "Test host" };
    });
    // Exceptions must release fixture gates before deferred cleanup is awaited.
    struct Unblock { std::shared_ptr<Gate> gate; ~Unblock() { gate->release.signal(); } } unblock { gate };
    check (gate->entered.wait (1000), "Worker didn't enter the slow detector fixture");
    const auto library = root.getChildFile ("lifecycle-library"); library.createDirectory();
    engine.setOutputFolder (library);
    auto begin = juce::Time::getMillisecondCounterHiRes();
    engine.prepare (8000, 128, SettingsManager::Snapshot{});
    check (juce::Time::getMillisecondCounterHiRes() - begin < 200, "prepare joined the discovery worker");
    engine.setProjectName ("Queued");
    check (engine.getProjectName() == "Queued", "UI snapshot didn't publish a requested name");
    capture (engine, 8000, 0.1f);
    await ([&] { return engine.getState() == RecordingEngine::State::Pending; },
           "A blocked discovery prevented encoding/saving pending audio");
    juce::XmlElement saved ("RememberState"); engine.saveState (saved);
    const auto folder = library.getChildFile (".pending").getChildFile (saved.getStringAttribute ("projectId"));
    check (LibraryStore::audioFile (folder, true).existsAsFile(), "Pending audio was not committed during blocked discovery");
    std::cout << "PASS: capture commits while discovery is blocked\n";
    BlockedCommit blocked (folder.getChildFile ("metadata.json"));
    check (blocked.entered.wait (1000), "Queue fixture did not acquire the metadata transaction");
    capture (engine, 8000, 0.2f); capture (engine, 8000, 0.4f); capture (engine, 8000, 0.8f);
    check (engine.getDroppedCaptures() == 1, "Queue saturation wasn't observable");
    begin = juce::Time::getMillisecondCounterHiRes();
    engine.prepare (16000, 128, SettingsManager::Snapshot{});
    check (juce::Time::getMillisecondCounterHiRes() - begin < 200, "prepare waited for an older capture");
    capture (engine, 16000, 1.25f);
    blocked.release.signal();
    gate->release.signal();
    const auto end = juce::Time::getMillisecondCounter() + 10000;
    double maximumQueryMs = 0;
    while (engine.getState() != RecordingEngine::State::Pending)
    {
        check (juce::Time::getMillisecondCounter() < end, "Retired capture banks weren't drained");
        begin = juce::Time::getMillisecondCounterHiRes();
        engine.getProjectName(); engine.isProjectConfirmed();
        juce::XmlElement state ("RememberState"); engine.saveState (state);
        maximumQueryMs = juce::jmax (maximumQueryMs, juce::Time::getMillisecondCounterHiRes() - begin);
        juce::Thread::sleep (1);
    }
    check (maximumQueryMs < 200, "UI query waited for background disk/encoding work");
    engine.saveState (saved);
    juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
    check (reader && reader->sampleRate == 16000 && reader->lengthInSamples == 19200,
           "New prepare changed the format of an older job or reordered captures");
    juce::AudioBuffer<float> sample (2, 1);
    const auto metadata = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
    check ((double) metadata["pcmPeak"] > 1 && (double) metadata["previewGain"] < 1
        && reader->read (&sample, 0, 1, 4000, true, true)
        && std::abs (sample.getSample (0, 0) - 1.25f * (float) (double) metadata["previewGain"]) < 0.035f,
           "Preview gain or a newer queued capture was overwritten");
    reader.reset();
    engine.prepare (8000, 128, SettingsManager::Snapshot{});
    Head unknown; unknown.known = false;
    juce::AudioBuffer<float> audio (2, 128); audio.clear();
    for (int i = 0; i < 100; ++i) engine.processBlock (audio, &unknown);
    check (engine.getUnknownPositions() == 1 && engine.getState() == RecordingEngine::State::Idle,
           "Unknown position triggered a zero-aligned capture");
    unknown.playing = false; engine.processBlock (audio, &unknown);
    capture (engine, 8000, 0.5f, 0.1);
    check (engine.getState() == RecordingEngine::State::Idle, "Late start shifted the preview prefix");
    RememberProcessor processor ([] { return HostProject{}; });
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    check (! processor.isBusesLayoutSupported (layout), "Asymmetric buses were accepted");
    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    check (processor.isBusesLayoutSupported (layout), "Matching mono buses were rejected");
    layout.inputBuses.set (0, juce::AudioChannelSet::create5point1());
    layout.outputBuses.set (0, juce::AudioChannelSet::create5point1());
    check (! processor.isBusesLayoutSupported (layout), "Unimplemented multichannel buses were accepted");
    std::cout << "PASS: prepare without joins, retired banks, queue counters, float headroom, unknown/late transport, buses; max UI query "
              << maximumQueryMs << " ms\n";
}
