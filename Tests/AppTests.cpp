#include "../App/Source/WaveformPlayerComponent.h"
#include "../Shared/LibraryStore.h"
#include "../App/Source/ProjectListComponent.h"
#include "../Shared/BackgroundRetirement.h"
#include "PlaybackTests.h"
#include "DeviceTests.h"
#include "LibraryTests.h"
#include "RecoveryTests.h"
#include "RecoveryUiTests.h"
#include "UiTests.h"
#include "StartupTests.h"
#include "ShellTests.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check (bool value, const char* reason) { if (! value) throw std::runtime_error (reason); }
template <class Condition> void awaitUi (Condition condition)
{
    const auto until = juce::Time::getMillisecondCounter() + 5000;
    while (! condition())
    {
        check (juce::Time::getMillisecondCounter() < until, "Asynchronous player timed out");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
}
void writeOgg (const juce::File& path, float value)
{
    juce::OggVorbisAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (path);
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (48000).withNumChannels (2).withBitsPerSample (16).withQualityOptionIndex (4));
    check (writer != nullptr, "Cannot create OGG fixture");
    juce::AudioBuffer<float> audio (2, 48000);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample (ch, i, value * std::sin (2.0 * juce::MathConstants<double>::pi * 440 * i / 48000));
    check (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "Cannot write OGG fixture");
}
}

int main (int argc, char** argv)
{
#if defined(__SANITIZE_ADDRESS__)
    std::cout << "AddressSanitizer instrumentation enabled\n";
#endif
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc == 4 && juce::String (argv[1]) == "--storage-crash") return storageCrash (juce::File (argv[2]), argv[3]);
    const auto root = juce::File::getCurrentWorkingDirectory().getChildFile ("remember-" + juce::Uuid().toString().removeCharacters ("-"));
    check (root.createDirectory().wasOk(), "Cannot create test directory");
    LibraryStore::setDataDirectoryForTests (root.getChildFile ("profile"));
    struct Cleanup
    {
        juce::File root;
        ~Cleanup() { if (root.isAChildOf (juce::File::getCurrentWorkingDirectory())) root.deleteRecursively(); }
    } cleanup { root };
    try
    {
        if (argc == 2 && juce::String (argv[1]) == "--frontend")
        {
            testFrontendDesign (root);
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        if (argc == 2 && juce::String (argv[1]) == "--shell")
        {
            testShellLifecycle (root);
            return 0;
        }
        if (argc == 2 && juce::String (argv[1]) == "--shell-tray")
        {
            testShellLifecycle (root, true);
            return 0;
        }
        if (argc == 3 && juce::String (argv[1]) == "--shell-case")
        {
            testShellCase (root, argv[2]);
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--cold-startup")
        {
            testColdStartupMatrix (root);
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--cold-startup-case")
        {
            check (argc == 5 || argc == 7, "Invalid cold-startup argument count");
            testColdStartupCase (root, juce::String (argv[2]).getIntValue(), juce::String (argv[3]).getIntValue(), argv[4],
                                 argc == 7 ? juce::File (argv[5]) : juce::File{}, argc == 7 ? juce::File (argv[6]) : juce::File{});
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--storage-recovery")
        {
            testRecovery (root);
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--library-benchmark")
        {
            benchmarkLibrary (root);
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--settings")
        {
            testSettingsLoad (root);
            testSettingsRecovery (root);
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--recovery-ui")
        {
            testRecoveryUi (root);
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--sidebar")
        {
            testSidebarUi (root);
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        if (argc > 1 && juce::String (argv[1]) == "--row-interactions")
        {
            testRowInteractions (root);
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        testLibraryCache (root);
        testLibraryWatch (root);
        testLibraryFailureRecovery (root);
        if (argc > 1 && juce::String (argv[1]) == "--library-index")
        {
            ProjectScanner::beginShutdown();
            awaitUi ([] { return ProjectScanner::shutdownReady(); });
            return 0;
        }
        testSettingsLoad (root);
        testSettingsRecovery (root);
        testRecoveryUi (root);
        testSidebarUi (root);
        testLibraryScene (root);
        testRowInteractions (root);
        testPlayback (root);
        {
            ProjectRowComponent row;
            ProjectEntry original, replacement;
            original.name = "Original"; original.projectId = juce::Uuid().toString();
            original.metadataFile = root.getChildFile ("original.json");
            replacement.name = "Replacement"; replacement.projectId = juce::Uuid().toString();
            replacement.metadataFile = root.getChildFile ("replacement.json");
            juce::String target, committed;
            row.onRenameCommitted = [&] (juce::String name) { target = original.projectId; committed = name; };
            row.setEntry (original, true); row.startInlineRename();
            juce::TextEditor* editor = nullptr;
            for (auto* child : row.getChildren()) if (auto* text = dynamic_cast<juce::TextEditor*> (child)) editor = text;
            check (editor != nullptr, "Rename editor is missing");
            editor->setText ("Edited original", false);
            row.onRenameCommitted = [&] (juce::String name) { target = replacement.projectId; committed = name; };
            row.setEntry (replacement, false);
            check (target == original.projectId && committed == "Edited original", "Row reuse renamed the replacement or lost the active edit");
            row.setEntry (original, true); row.startInlineRename();
            editor->setText ("Still typing", false);
            original.metadataFile = root.getChildFile ("relocated.json");
            row.setEntry (original, true);
            check (editor->getText() == "Still typing" && editor->isVisible(), "Same-ID refresh interrupted an active rename");
            if (editor->onEscapeKey) editor->onEscapeKey();
        }
        ProjectEntry first, second, corrupt;
        first.name = "First"; first.previewOgg = root.getChildFile ("first.ogg"); first.metadataFile = root.getChildFile ("first.json");
        second.name = "Second"; second.previewOgg = root.getChildFile ("second.ogg"); second.metadataFile = root.getChildFile ("second.json");
        corrupt.name = "Corrupt"; corrupt.previewOgg = root.getChildFile ("corrupt.ogg");
        writeOgg (first.previewOgg, 0.2f); writeOgg (second.previewOgg, 0.6f);
        testDevice (first);
        corrupt.previewOgg.replaceWithText ("invalid OGG");
        {
            WaveformPlayerComponent player;
            check (! player.isAudioDeviceOpen(), "Idle player opened an audio device");
            player.loadEntry (first);
            awaitUi ([&] { return player.hasLoadedEntry(); });
            check (! player.isAudioDeviceOpen(), "Loading a preview opened an audio device");
            for (int i = 0; i < 100; ++i) player.loadEntry (i % 2 == 0 ? first : second);
            player.loadEntry (corrupt);
            awaitUi ([&] { return player.getPlaybackError().isNotEmpty(); });
            check (! player.hasLoadedEntry(), "Corrupt preview retained the old loaded state");
            player.loadEntry (second);
            awaitUi ([&] { return player.hasLoadedEntry(); });
            check (player.getPlaybackError().isEmpty(), "Valid preview retained the old error");
            player.releaseEntry (first.metadataFile);
            check (player.hasLoadedEntry(), "Storage action detached another entry's reader");
            player.releaseEntry (second.metadataFile);
            check (! player.hasLoadedEntry(), "Storage action retained the selected reader");
            player.loadEntry (first); player.releaseEntry (first.metadataFile);
            awaitUi ([] { return WaveformPlayerComponent::pendingLoads() == 0; });
            check (! player.hasLoadedEntry(), "Cancelled load reattached a reader after storage release");
            ProjectEntry unavailable = first; unavailable.storageError = "Incomplete recording fixture";
            player.loadEntry (unavailable);
            check (! player.hasLoadedEntry() && player.getPlaybackError() == unavailable.storageError,
                   "Incomplete recording attempted playback or lost its actionable error");
            player.stop();
        }
        for (int i = 0; i < 100; ++i)
        {
            auto player = std::make_unique<WaveformPlayerComponent>();
            player->loadEntry (first);
            player->loadEntry (second);
            player.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        awaitUi ([] { return WaveformPlayerComponent::pendingLoads() == 0 && BackgroundRetirement::pending() == 0; });
        {
            struct ShutdownFixture
            {
                juce::WaitableEvent blocked, release { true };
                std::atomic<bool> completed { false }, rejected { false }, ranAfterClose { false }, dispatched { false };
            };
            auto fixture = std::make_shared<ShutdownFixture>();
            struct ReleaseOnExit
            { std::shared_ptr<ShutdownFixture> fixture; ~ReleaseOnExit() { fixture->release.signal(); } } unblock { fixture };
            ProjectScanner::queryAsync ([fixture]
            { fixture->blocked.signal(); fixture->release.wait (10000); return juce::Result::ok(); },
                [fixture] (juce::Result result) { check (result.wasOk(), "An accepted query was discarded during shutdown"); fixture->completed.store (true); });
            check (fixture->blocked.wait (1000), "Shutdown fixture did not enter its background query");
            const auto begun = juce::Time::getMillisecondCounterHiRes();
            ProjectScanner::beginShutdown();
            const auto closingMs = juce::Time::getMillisecondCounterHiRes() - begun;
            check (closingMs < 200 && ! ProjectScanner::shutdownReady() && ProjectScanner::pendingJobs() > 0,
                   "Shutdown waited for background I/O or lost ownership of an accepted query");
            juce::MessageManager::callAsync ([fixture] { fixture->dispatched.store (true); });
            awaitUi ([&] { return fixture->dispatched.load(); });
            check (! ProjectScanner::shutdownReady(), "Blocked query was reported as finished");
            fixture->release.signal();
            awaitUi ([&] { return fixture->completed.load() && ProjectScanner::shutdownReady(); });
            ProjectScanner::queryAsync ([fixture] { fixture->ranAfterClose.store (true); return juce::Result::ok(); },
                [fixture] (juce::Result result) { fixture->rejected.store (result.failed()); });
            awaitUi ([&] { return fixture->rejected.load() && ProjectScanner::shutdownReady(); });
            check (! fixture->ranAfterClose.load(), "Shutdown admitted a new background query");
            std::cout << "PASS: nonblocking library shutdown, live dispatch, accepted/rejected query ownership; request=" << closingMs << " ms\n";
        }
        std::cout << "PASS: asynchronous OGG loading, stale requests, corrupt preview, lazy device, close during load\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << "\n";
        ProjectScanner::beginShutdown();
        awaitUi ([] { return ProjectScanner::shutdownReady() && WaveformPlayerComponent::pendingLoads() == 0
                          && BackgroundRetirement::pending() == 0; });
        return 1;
    }
}
