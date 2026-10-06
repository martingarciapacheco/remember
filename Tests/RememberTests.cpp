#include "../Plugin/Source/RecordingEngine.h"
#include "../App/Source/ProjectScanner.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
void require (bool ok, const char* message)
{
    if (! ok) throw std::runtime_error (message);
}

template <typename Condition>
void await (Condition condition, const char* message)
{
    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (! condition())
    {
        require (juce::Time::getMillisecondCounter() < end, message);
        juce::Thread::sleep (10);
    }
}

struct Observation
{
    juce::CriticalSection lock;
    HostProject host { true, {}, {}, "FL Studio" };
    HostProject get() { juce::ScopedLock guard (lock); return host; }
    void set (const juce::File& file)
    {
        juce::ScopedLock guard (lock);
        host.name = file.getFileNameWithoutExtension();
        host.path = file.getFullPathName();
    }
};

struct PlayHead : juce::AudioPlayHead
{
    bool playing = true;
    double time = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setIsPlaying (playing);
        position.setTimeInSeconds (time);
        position.setBpm (120.0);
        return position;
    }
};

void capture (RecordingEngine& engine, double seconds, float level, bool offline = false,
              double start = 0, bool sendStop = true, double rate = 8000)
{
    PlayHead head;
    head.playing = ! offline; // exercise export even with the host's playing bit off
    int samples = (int) std::llround (seconds * rate);
    for (int done = 0; done < samples;)
    {
        int count = juce::jmin (256, samples - done);
        juce::AudioBuffer<float> buffer (2, count);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < count; ++i) buffer.setSample (ch, i, level);
        head.time = start + (double) done / rate;
        engine.processBlock (buffer, &head, offline);
        require (buffer.getSample (0, 0) == level, "Plugin changed the input audio");
        done += count;
    }
    if (sendStop)
    {
        juce::AudioBuffer<float> stopped (2, 256);
        stopped.clear();
        head.playing = false;
        head.time = start + seconds;
        engine.processBlock (stopped, &head);
    }
}

juce::Array<juce::File> entries (const juce::File& previews)
{
    juce::Array<juce::File> result;
    for (auto& folder : previews.findChildFiles (juce::File::findDirectories, false))
        if (! folder.getFileName().startsWithChar ('.') && folder.getChildFile ("metadata.json").existsAsFile()) result.add (folder);
    return result;
}
juce::var meta (const juce::File& folder)
{
    return juce::JSON::parse (folder.getChildFile ("metadata.json").loadFileAsString());
}
juce::File entryNamed (const juce::File& previews, const juce::String& name)
{
    for (auto& folder : entries (previews))
        if (meta (folder).getProperty ("projectName", "").toString() == name) return folder;
    return {};
}
juce::AudioBuffer<float> readAudio (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader) throw std::runtime_error ("Audio is unreadable: " + file.getFullPathName().toStdString());
    juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
    require (reader->read (&audio, 0, audio.getNumSamples(), 0, true, true), "Couldn't read audio");
    return audio;
}

#if JUCE_WINDOWS
std::atomic<int> titleMessages { 0 };
LRESULT CALLBACK hostWindowProcedure (HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_GETTEXT) ++titleMessages;
    return DefWindowProcW (window, message, wParam, lParam);
}

void testHostLoading (const juce::File& root)
{
    auto project = root.getChildFile (juce::String::fromUTF8 ("Carga canción.flp"));
    project.replaceWithText ("host loading fixture");
    const auto module = GetModuleHandleW (nullptr);
    WNDCLASSW windowClass {};
    windowClass.hInstance = module;
    windowClass.lpfnWndProc = hostWindowProcedure;
    windowClass.lpszClassName = L"RememberHostLoadingFixture";
    require (RegisterClassW (&windowClass) != 0, "Couldn't register host window fixture");
    auto title = project.getFullPathName() + " - FL Studio 2026";
    const auto window = CreateWindowExW (0, windowClass.lpszClassName, title.toWideCharPointer(),
        WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, module, nullptr);
    require (window != nullptr, "Couldn't create host window fixture");
    struct WindowCleanup
    {
        HWND window;
        HINSTANCE module;
        ~WindowCleanup()
        {
            DestroyWindow (window);
            UnregisterClassW (L"RememberHostLoadingFixture", module);
        }
    } cleanup { window, module };

    HostProject detected;
    juce::WaitableEvent done (true);
    titleMessages = 0;
    std::thread worker ([&] { detected = ProjectIdentity::detectHostProject(); done.signal(); });
    // A DAW loading plugin state does not dispatch window messages while it
    // waits for prepareToPlay or an editor constructor to finish.
    const bool finishedDuringLoad = done.wait (750);
    if (! finishedDuringLoad)
    {
        // Unblock the old implementation so the regression fails cleanly.
        while (! done.wait (1))
        {
            MSG message {};
            while (PeekMessageW (&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage (&message);
                DispatchMessageW (&message);
            }
        }
    }
    worker.join();
    require (finishedDuringLoad, "Host title detection waited for the DAW's blocked UI thread");
    require (titleMessages.load() == 0, "Host detection sent WM_GETTEXT to the DAW");
    require (detected.available && detected.path == project.getFullPathName(), "Nonblocking host detection lost the project path");
    require (detected.name == project.getFileNameWithoutExtension(), "Nonblocking host detection lost Unicode");

    auto previews = root.getChildFile ("loading-previews");
    previews.createDirectory();
    juce::XmlElement saved ("RememberState");
    saved.setAttribute ("projectId", juce::Uuid().toString());
    saved.setAttribute ("projectPath", project.getFullPathName());
    saved.setAttribute ("projectName", "stale template name");
    for (int i = 0; i < 20; ++i)
    {
        // Exercise native host discovery while the host's window thread stays
        // inside plugin construction, state restore, preparation and teardown.
        RecordingEngine engine;
        engine.setOutputFolder (previews);
        engine.restoreState (saved);
        engine.prepare (8000, 256);
        await ([&] { return engine.getProjectName() == project.getFileNameWithoutExtension(); }, "Project loading didn't finish");
        require (engine.isProjectConfirmed(), "Saved project wasn't confirmed during loading");
        engine.prepare (16000, 256);
        engine.saveState (saved);
    }
    require (titleMessages.load() == 0, "Plugin lifecycle queried the DAW's UI thread");
    std::cout << "PASS: native host discovery and 20 project reloads without a DAW message loop\n";
}
#endif
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    const auto root = juce::File::getCurrentWorkingDirectory().getChildFile ("remember-regression-" + juce::Uuid().toString());
    root.createDirectory();
    int status = 0;
    try
    {
#if JUCE_WINDOWS
        if (argc > 1 && juce::String (argv[1]) == "--host-loading")
        {
            testHostLoading (root);
            root.deleteRecursively();
            return 0;
        }
#else
        juce::ignoreUnused (argc, argv);
#endif
        require (ProjectIdentity::parseTitle ("*Tema.flp - FL Studio 2026", "FL Studio").name == "Tema", "FL filename parsing");
        require (ProjectIdentity::parseTitle ("FL Studio 2026 - Tema.flp", "FL Studio").name == "Tema", "FL prefix parsing");
        require (ProjectIdentity::parseTitle ("FL Studio 2026", "FL Studio").name.isEmpty(), "Unsaved FL must be empty");
        require (ProjectIdentity::parseTitle ("Untitled - Ableton Live 12 Suite", "Ableton Live").name.isEmpty(), "Unsaved Live must be empty");
        require (ProjectIdentity::parseTitle ("Tema - Ableton Live 12 Suite", "Ableton Live").name == "Tema", "Ableton title parsing");
        auto projects = root.getChildFile ("projects");
        auto previews = root.getChildFile ("previews");
        projects.createDirectory(); previews.createDirectory();
        auto song = projects.getChildFile (juce::String::fromUTF8 ("Canción.flp"));
        song.replaceWithText ("project A");
        require (ProjectIdentity::parseTitle (song.getFullPathName() + " - FL Studio 2026", "FL Studio").path == song.getFullPathName(), "Unicode absolute path parsing");
        auto observer = std::make_shared<Observation>();
        observer->set (song);
        juce::XmlElement saved ("RememberState");
        juce::File originalFolder;
        int fullLength = 0;
        {
            RecordingEngine engine ([observer] { return observer->get(); });
            engine.setOutputFolder (previews);
            engine.prepare (8000, 256);
            await ([&] { return engine.getProjectName() == song.getFileNameWithoutExtension(); }, "DAW name wasn't detected without an editor");
            capture (engine, engine.getTotalSeconds(), 0.2f);
            await ([&] { return engine.getState() == RecordingEngine::State::Done; }, "Full capture wasn't saved");
            originalFolder = entryNamed (previews, song.getFileNameWithoutExtension());
            require (originalFolder.isDirectory(), "Missing saved entry");
            auto full = readAudio (originalFolder.getChildFile ("preview.wav"));
            fullLength = full.getNumSamples();
            auto metadata = meta (originalFolder);
            metadata.getDynamicObject()->setProperty ("tags", juce::Array<juce::var> { "keep" });
            metadata.getDynamicObject()->setProperty ("folders", juce::Array<juce::var> { "Tracks" });
            metadata.getDynamicObject()->setProperty ("favorite", true);
            originalFolder.getChildFile ("metadata.json").replaceWithText (juce::JSON::toString (metadata));
            capture (engine, 10.0, -0.4f);
            await ([&] { return engine.getState() == RecordingEngine::State::Done; }, "Partial capture wasn't saved");
            auto partial = readAudio (originalFolder.getChildFile ("preview.wav"));
            require (partial.getNumSamples() == fullLength, "Short re-record truncated the preview");
            require (std::abs (partial.getSample (0, 100) + 0.4f) < 0.00001f, "New beginning not replaced");
            for (int i = 80000; i < fullLength; ++i)
                require (partial.getSample (0, i) == full.getSample (0, i), "Untouched tail changed");
            auto compressed = readAudio (originalFolder.getChildFile ("preview.ogg"));
            require (compressed.getNumSamples() == fullLength, "OGG duration was truncated");
            require ((bool) meta (originalFolder).getProperty ("favorite", false), "Lost favorite on re-record");
            require (meta (originalFolder).getProperty ("folders", juce::var{}).isArray(), "Lost folder membership");
            auto before = ProjectIdentity::fingerprint (originalFolder.getChildFile ("preview.wav"));
            capture (engine, 0.75, 0.8f);
            require (ProjectIdentity::fingerprint (originalFolder.getChildFile ("preview.wav")) == before, "Subsecond capture overwrote audio");
            capture (engine, 1.0, 0.6f);
            await ([&] { return engine.getState() == RecordingEngine::State::Done; }, "Exactly one second was discarded");
            auto oneSecond = readAudio (originalFolder.getChildFile ("preview.wav"));
            require (std::abs (oneSecond.getSample (0, 100) - 0.6f) < 0.00001f, "One second wasn't saved");
            engine.setProjectName ("manual override");
            require (engine.getProjectName() == song.getFileNameWithoutExtension(), "Manual name overrode the DAW");
            auto renamed = projects.getChildFile ("Renamed.flp");
            require (song.moveFileTo (renamed), "Couldn't rename fixture");
            observer->set (renamed);
            await ([&] { return engine.getProjectName() == "Renamed"; }, "Rename not reflected automatically");
            require (entryNamed (previews, "Renamed") == originalFolder, "Rename changed identity");
            auto copy = projects.getChildFile ("Version2.flp");
            require (renamed.copyFileTo (copy), "Couldn't create Save As fixture");
            observer->set (copy);
            await ([&] { return engine.getProjectName() == "Version2"; }, "Save As not detected");
            require (entries (previews).size() == 2, "Save As didn't create an independent entry");
            require (entryNamed (previews, "Version2").getChildFile ("preview.wav").existsAsFile(), "Save As lost the full preview");
            engine.saveState (saved);
        }
        {
            RecordingEngine restored ([observer] { return observer->get(); });
            restored.setOutputFolder (previews);
            restored.restoreState (saved);
            restored.prepare (8000, 256);
            await ([&] { return restored.getProjectName() == "Version2"; }, "Reopen restored a wrong name");
            capture (restored, 1.2, -0.1f, true, 0, false);
            restored.finishCapture();
            await ([&] { return restored.getState() == RecordingEngine::State::Done; }, "Offline export not captured");
            require (entries (previews).size() == 2, "Reopen duplicated a project");
            auto second = readAudio (entryNamed (previews, "Version2").getChildFile ("preview.wav"));
            auto first = readAudio (originalFolder.getChildFile ("preview.wav"));
            require (std::abs (second.getSample (0, 100) + 0.1f) < 0.00001f, "Offline export audio wasn't saved");
            require (std::abs (first.getSample (0, 100) - 0.6f) < 0.00001f, "Save As changed the original audio");
            // prepareToPlay can happen again without an intervening stopped block.
            restored.prepare (8000, 256);
            capture (restored, 1.2, 0.3f, false, 0, false);
            restored.finishCapture();
            await ([&] { return restored.getState() == RecordingEngine::State::Done; }, "Prepare didn't rearm the trigger");
            restored.prepare (8000, 256);
            capture (restored, 1.2, 0.9f, false, 20);
            require (restored.getState() == RecordingEngine::State::Idle, "Playback from the middle recorded a preview");
        }
        std::cout << "PASS: headless recording, partial merge, one-second minimum, rename, Save As, reopen, export, rearm\n";
        auto pendingObserver = std::make_shared<Observation>();
        juce::String pendingId;
        {
            RecordingEngine pending ([pendingObserver] { return pendingObserver->get(); });
            pending.setOutputFolder (previews);
            juce::XmlElement legacy ("RememberState");
            legacy.setAttribute ("projectName", "INSP");
            pending.restoreState (legacy);
            pending.prepare (8000, 256);
            require (pending.getProjectName().isEmpty(), "Template leaked INSP");
            capture (pending, 1.2, 0.2f);
            await ([&] { return pending.getState() == RecordingEngine::State::Pending; }, "Unnamed audio wasn't held pending");
            juce::XmlElement xml ("state"); pending.saveState (xml);
            pendingId = xml.getStringAttribute ("projectId");
            require (previews.getChildFile (".pending").getChildFile (pendingId).getChildFile ("preview.ogg").existsAsFile(), "Pending audio wasn't stored");
            pending.setProjectName ("My idea");
            require (pending.getProjectName() == "My idea", "Can't name pending audio");
            auto named = ProjectScanner::scan (previews, { projects });
            bool associated = false;
            for (auto& entry : named)
                if (entry.name == "My idea")
                {
                    require (entry.pending && entry.projectPath.isEmpty(), "Named pending audio was linked to another DAW file");
                    associated = true;
                }
            require (associated, "Naming didn't associate the pending audio in the browser");
        }
        require (! previews.getChildFile (".pending").getChildFile (pendingId).exists(), "Unsaved audio survived closing the DAW");
        for (auto& entry : ProjectScanner::scan (previews, { projects }))
            require (entry.name != "My idea", "Discarded pending entry remained in the browser");
        {
            RecordingEngine pending ([pendingObserver] { return pendingObserver->get(); });
            pending.setOutputFolder (previews);
            pending.prepare (8000, 256);
            capture (pending, 1.2, 0.35f);
            await ([&] { return pending.getState() == RecordingEngine::State::Pending; }, "Pending capture failed");
            auto savedProject = projects.getChildFile ("NewlySaved.flp"); savedProject.replaceWithText ("newly saved project");
            pendingObserver->set (savedProject);
            await ([&] { return entryNamed (previews, "NewlySaved").isDirectory(); }, "Saving didn't associate pending audio");
            require (entryNamed (previews, "NewlySaved").getChildFile ("preview.ogg").existsAsFile(), "Pending audio lost during association");
        }
        require (entryNamed (previews, "NewlySaved").exists(), "Saved pending audio was deleted on close");
        std::cout << "PASS: stale INSP template, pending capture, discard unsaved, associate on first save\n";
        {
            auto templateObserver = std::make_shared<Observation>();
            auto templateCopy = projects.getChildFile ("FromTemplate.flp"); templateCopy.replaceWithText ("independent template copy");
            templateObserver->set (templateCopy);
            RecordingEngine engine ([templateObserver] { return templateObserver->get(); });
            engine.setOutputFolder (previews);
            engine.restoreState (saved);
            engine.prepare (8000, 256);
            await ([&] { return engine.getProjectName() == "FromTemplate"; }, "Saved template name was reused");
            require (! entryNamed (previews, "FromTemplate").exists(), "Template inherited another project's audio");
            capture (engine, 1.2, 0.7f);
            await ([&] { return engine.getState() == RecordingEngine::State::Done; }, "Template capture failed");
            require (entryNamed (previews, "FromTemplate") != entryNamed (previews, "Version2"), "Template reused another project's identity");
        }
        {
            // Discovery can be delayed: merge the pending prefix into its entry.
            auto delayed = std::make_shared<Observation>();
            RecordingEngine engine ([delayed] { return delayed->get(); });
            engine.setOutputFolder (previews);
            engine.prepare (8000, 256);
            capture (engine, 1.2, -0.25f);
            await ([&] { return engine.getState() == RecordingEngine::State::Pending; }, "Delayed discovery didn't record pending audio");
            delayed->set (projects.getChildFile ("Version2.flp"));
            await ([&] { return engine.getState() == RecordingEngine::State::Done; }, "Pending audio couldn't merge into an existing entry");
            auto merged = readAudio (entryNamed (previews, "Version2").getChildFile ("preview.wav"));
            require (merged.getNumSamples() == fullLength, "Pending association lost the existing tail");
            require (std::abs (merged.getSample (0, 100) + 0.25f) < 0.00001f, "Pending prefix wasn't associated");
            require (entries (previews).size() == 4, "Pending association duplicated an existing entry");
            engine.prepare (16000, 256);
            capture (engine, 1.2, 0.45f, false, 0, true, 16000);
            await ([&] { return engine.getState() == RecordingEngine::State::Done; }, "Sample rate change failed");
            auto resampled = readAudio (entryNamed (previews, "Version2").getChildFile ("preview.wav"));
            require (resampled.getNumSamples() == 2 * fullLength, "Sample rate change lost preview duration");
            require (std::abs (resampled.getSample (0, 32000) - merged.getSample (0, 16000)) < 0.0001f, "Sample rate change changed the tail");
            capture (engine, 1.2, 0.15f, false, 0, true, 16000);
            capture (engine, 1.2, -0.55f, false, 0, true, 16000);
            await ([&] { return engine.getState() == RecordingEngine::State::Done; }, "Queued re-records weren't saved");
            auto queued = readAudio (entryNamed (previews, "Version2").getChildFile ("preview.wav"));
            require (std::abs (queued.getSample (0, 100) + 0.55f) < 0.0001f, "Older capture overwrote a newer queued capture");
        }
        std::cout << "PASS: saved template isolation, pending association to existing preview, sample rate change\n";
        auto scanned = ProjectScanner::scan (previews, { projects });
        require (scanned.size() == 4, "Browser didn't find all saved entries");
        auto old = projects.getChildFile ("Renamed.flp");
        auto movedRoot = root.getChildFile ("moved"); movedRoot.createDirectory();
        auto moved = movedRoot.getChildFile ("DifferentName.flp");
        require (old.moveFileTo (moved), "Couldn't move fixture");
        scanned = ProjectScanner::scan (previews, { movedRoot, projects });
        bool located = false;
        for (auto& entry : scanned)
            if (entry.metadataFile.getParentDirectory() == originalFolder)
            {
                require (entry.projectPath == moved.getFullPathName(), "Moved/renamed project wasn't relocated by identity");
                require (entry.name == "DifferentName", "Browser didn't follow file rename");
                entry.tags.add ("edited"); entry.save();
                require (meta (originalFolder).getProperty ("projectId", "").toString() == originalFolder.getFileName(), "Browser edit erased project identity");
                located = true;
            }
        require (located, "Original entry disappeared");
        auto duplicateRoot = root.getChildFile ("duplicates"); duplicateRoot.createDirectory();
        auto duplicate = duplicateRoot.getChildFile ("Version2.flp"); duplicate.replaceWithText ("unrelated project");
        require (ProjectIdentity::resolve ("Version2", "FL Studio", { projects, duplicateRoot }).isEmpty(), "Ambiguous names were guessed");
        auto decoy = duplicateRoot.getChildFile ("Missing.flp"); decoy.replaceWithText ("another project");
        require (ProjectIdentity::resolve ("Missing", "FL Studio", { duplicateRoot }, {}, "known-other-id", "known-other-hash").isEmpty(), "Identity fell back to the wrong same-name file");
        std::cout << "PASS: moved file, browser cache invalidation, metadata preservation, ambiguous names\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << "\n";
        status = 1;
    }
    require (root.isAChildOf (juce::File::getCurrentWorkingDirectory()), "Unsafe test cleanup path");
    root.deleteRecursively();
    return status;
}
