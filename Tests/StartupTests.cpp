#include "StartupTests.h"
#include "../App/Source/MainComponent.h"
#include "../Shared/BackgroundRetirement.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void check (bool value, const juce::String& reason)
{ if (! value) throw std::runtime_error (reason.toStdString()); }
template<class Type> Type* childOf (juce::Component& component)
{
    for (auto* child : component.getChildren())
        if (auto* typed = dynamic_cast<Type*> (child)) return typed;
    return nullptr;
}
void writeFixtureText (const juce::File& file, const juce::String& text)
{
    // These are fresh test markers, not committed recordings. Avoid the
    // temporary replacement in replaceWithText for every marker;
    // durable publication is covered by the dedicated storage suites.
    check (! file.exists(), "Startup fixture marker already exists: " + file.getFullPathName());
    juce::FileOutputStream output (file, 0);
    check (output.openedOk()
           && output.writeText (text, false, false, "\n") && output.getStatus().wasOk(), "Cannot write startup fixture: " + file.getFullPathName());
}
juce::File artifact (int count, int missing, const juce::String& mode, const juce::String& suffix)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts/cold-startup");
    check (folder.createDirectory().wasOk(), "Cannot create cold-startup artifact directory");
    return folder.getChildFile (juce::String (count) + "-" + juce::String (missing) + "-" + mode + suffix);
}
void saveImage (juce::Component& scene, const juce::File& file)
{
    juce::FileOutputStream output (file);
    juce::PNGImageFormat png;
    check (output.openedOk() && output.setPosition (0) && output.truncate().wasOk()
        && png.writeImageToStream (scene.createComponentSnapshot (scene.getLocalBounds(), true, 1.0f,
                                                                 juce::SoftwareImageType{}), output),
        "Cannot write cold-startup image");
}
juce::String fixtureMarker (int count) { return "Remember cold-startup fixture " + juce::String (count); }
juce::File parkedProjects (const juce::File& projects) { return projects.getSiblingFile ("parked-" + projects.getFileName()); }
void prepareFixture (const juce::File& library, const juce::File& projects, int count, int missingPercent)
{
    check (library.createDirectory().wasOk() && projects.createDirectory().wasOk(), "Cannot create cold library");
    const auto expectedMissing = count * missingPercent / 100;
    for (int i = 0; i < count; ++i)
    {
        const auto name = "Cold " + juce::String (i).paddedLeft ('0', 5);
        const auto project = projects.getChildFile (name + ".flp");
        if (i >= expectedMissing) writeFixtureText (project, "cold startup project");
        const auto directory = library.getChildFile (juce::String (i).paddedLeft ('0', 5));
        check (directory.createDirectory().wasOk(), "Cannot create cold entry");
        juce::var metadata (new juce::DynamicObject()); auto* object = metadata.getDynamicObject();
        object->setProperty ("projectId", juce::Uuid().toString());
        object->setProperty ("projectName", name);
        object->setProperty ("projectPath", project.getFullPathName());
        if (i >= expectedMissing) object->setProperty ("projectFileId", ProjectIdentity::fileId (project));
        object->setProperty ("daw", "FL Studio");
        // Fixed UTC fixtures need ordering, not a local timezone conversion
        // for every field/entry. Preserve one distinct valid second per row.
        object->setProperty ("recordedAt", "2026-10-09T" + juce::String (i / 3600).paddedLeft ('0', 2)
            + ":" + juce::String ((i / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (i % 60).paddedLeft ('0', 2) + "Z");
        object->setProperty ("tags", juce::Array<juce::var> { "Group " + juce::String (i % 8) });
        object->setProperty ("folders", juce::Array<juce::var> { "Groups/" + juce::String (i % 10) });
        // Fixture creation is outside the startup measurement; these marker
        // files exercise indexing, never decoding or a physical audio driver.
        juce::FileOutputStream output (directory.getChildFile ("metadata.json"), 0);
        check (output.openedOk() && output.writeText (juce::JSON::toString (metadata), false, false, "\n")
               && output.getStatus().wasOk(),
               "Cannot write cold metadata");
        writeFixtureText (directory.getChildFile ("preview.ogg"), "index marker");
    }
    writeFixtureText (library.getChildFile (".cold-startup-fixture"), fixtureMarker (count));
}
}

void testColdStartupCase (const juce::File& root, int count, int missingPercent, const juce::String& mode,
                          const juce::File& sharedLibrary, const juce::File& sharedProjects)
{
    const auto preparing = juce::Time::getMillisecondCounterHiRes();
    std::cout << "COLD_FIXTURE_BEGIN root=" << root.getFullPathName() << " count=" << count << " missing=" << missingPercent << " mode=" << mode << std::endl;
    check (count >= 0 && count <= 10000 && (missingPercent == 0 || missingPercent == 10 || missingPercent == 50)
           && (mode == "ready" || mode == "close" || mode == "error" || mode == "offline"), "Invalid cold-startup fixture arguments");
    const bool shared = sharedLibrary != juce::File{} || sharedProjects != juce::File{};
    const auto library = shared ? sharedLibrary : root.getChildFile ("library");
    const auto projects = shared ? sharedProjects : root.getChildFile ("projects");
    if (shared)
        check (library.isAChildOf (juce::File::getCurrentWorkingDirectory()) && projects.isAChildOf (juce::File::getCurrentWorkingDirectory())
               && library.getParentDirectory() == projects.getParentDirectory()
               && library.getChildFile (".cold-startup-fixture").loadFileAsString() == fixtureMarker (count), "Unmarked or unbounded shared startup fixture");
    else prepareFixture (library, projects, count, missingPercent);
    const auto expectedMissing = count * missingPercent / 100;
    juce::var config (new juce::DynamicObject()); auto* settings = config.getDynamicObject();
    settings->setProperty ("previewsFolder", library.getFullPathName());
    settings->setProperty ("projectsRootFolders", juce::Array<juce::var> { projects.getFullPathName() });
    // Do not call ProjectScanner here: saveSettings/refresh/snapshot would
    // initialise the singleton before the measured MainComponent constructor.
    check (LibraryStore::writeJsonAtomically (LibraryStore::configFile(), config).wasOk(), "Cannot configure cold fixture");
    if (mode == "error") check (LibraryStore::configFile().replaceWithText ("{ incomplete configuration"), "Cannot corrupt config fixture");
    if (mode == "offline")
    {
        auto unavailable = config.clone();
        unavailable.getDynamicObject()->setProperty ("previewsFolder", root.getChildFile ("unavailable-library").getFullPathName());
        check (LibraryStore::writeJsonAtomically (LibraryStore::configFile(), unavailable).wasOk(), "Cannot configure unavailable library");
    }
    const auto originalConfig = LibraryStore::configFile().loadFileAsString();
    const auto setupMs = juce::Time::getMillisecondCounterHiRes() - preparing;
    std::cout << "COLD_FIXTURE_READY setupMs=" << setupMs << std::endl;

    const auto begun = juce::Time::getMillisecondCounterHiRes();
    auto scene = std::make_unique<MainComponent>();
    const auto constructedMs = juce::Time::getMillisecondCounterHiRes() - begun;
    scene->setSize (860, 640);
    auto* list = childOf<ProjectListComponent> (*scene);
    auto* player = childOf<WaveformPlayerComponent> (*scene);
    check (list && player, "Cold scene lacks its list/player");
    auto* status = childOf<juce::Label> (*list);
    auto* rows = childOf<juce::ListBox> (*list);
    auto* search = childOf<juce::TextEditor> (*list);
    check (status && rows && search, "Cold scene lacks list controls");
    const auto first = ProjectScanner::librarySnapshot();
    const bool pendingAtConstruction = first->loading;
    if (pendingAtConstruction)
        check (status->isVisible() && status->getText().startsWith ("Loading library"),
               "Cold library is blank while the first index scan is pending");
    if (mode == "close") check (pendingAtConstruction, "Early-close fixture did not interrupt the first scan");
    check (constructedMs < 2000, "Cold scene constructor waited for library I/O");
    if (count == 10000 && missingPercent == 50)
        saveImage (*scene, artifact (count, missingPercent, mode, "-initial.png"));

    std::vector<double> dispatch;
    auto pump = [&]
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        dispatch.push_back (juce::Time::getMillisecondCounterHiRes() - start);
        check (juce::Component::getNumCurrentlyModalComponents() == 0,
               "Cold startup opened an unsolicited settings/missing-project dialog");
        check (! player->isAudioDeviceOpen(), "Cold startup opened a physical audio device");
        check (juce::Time::getMillisecondCounterHiRes() - begun < 90000, "Cold startup timed out");
    };
    double readyMs = 0, closeMs = 0, shutdownMs = 0, searchMs = 0;
    int missing = 0, liveRows = 0;
    bool recovered = false;
    const bool initialFailure = mode == "error" || mode == "offline";
    const bool searchEnteredDuringLoading = count > 0 && mode == "ready" && ProjectScanner::librarySnapshot()->loading;
    if (count == 10000 && mode == "ready") check (searchEnteredDuringLoading, "Large-library fixture missed the initial loading interval");
    if (count > 0 && mode == "ready") search->setText ("Cold 00000");
    if (mode != "close")
    {
        for (;;)
        {
            const auto snapshot = ProjectScanner::librarySnapshot();
            const bool ready = ! snapshot->loading && (initialFailure ? snapshot->error.isNotEmpty()
                : snapshot->sourceRoot == library && snapshot->entries.size() == count
                    && list->getNumRows() == (count > 0 ? 1 : 0));
            if (ready && ! status->getText().startsWith ("Loading library")) break;
            pump();
        }
        readyMs = juce::Time::getMillisecondCounterHiRes() - begun;
        const auto snapshot = ProjectScanner::librarySnapshot();
        if (initialFailure)
        {
            check (status->isVisible() && status->getText().contains ("unavailable") && status->getTooltip().isNotEmpty(),
                   "Initial config error lost its actionable status");
            check (LibraryStore::configFile().loadFileAsString() == originalConfig, "Startup silently rewrote an unavailable config/library");
            saveImage (*scene, artifact (count, missingPercent, mode, "-error.png"));
            check (LibraryStore::writeJsonAtomically (LibraryStore::configFile(), config).wasOk(), "Cannot restore cold config");
            list->refresh();
            while (ProjectScanner::librarySnapshot()->error.isNotEmpty()
                   || ProjectScanner::librarySnapshot()->sourceRoot != library
                   || status->getText().contains ("unavailable")) pump();
            recovered = true;
        }
        else
        {
            for (const auto& entry : snapshot->entries) if (! entry.projectAvailable && ! entry.pending) ++missing;
            check (missing == expectedMissing, "Cold index lost or accepted missing projects");
            check (snapshot->missingProjects == expectedMissing, "Cold publication reported a wrong missing count");
            if (missing > 0)
                check (status->isVisible() && status->getText().contains (juce::String (missing) + " projects not located")
                       && status->getTooltip().contains ("Assign path"), "Missing project summary was skipped at cold startup");
            else if (count > 0) check (! status->isVisible(), "Healthy library retained its loading/error status");
            else check (status->isVisible() && status->getText().contains ("No recordings"), "Empty library is indistinguishable from loading");
        }
        if (count > 0)
        {
            check (search->getText() == "Cold 00000" && list->getNumRows() == 1, "First publication discarded the search entered during loading");
            search->setText (""); while (list->getNumRows() != count) pump();
            rows->scrollToEnsureRowIsOnscreen (count - 1); pump();
            auto* last = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (count - 1));
            check (last && last->isVisible(), "Cold publication did not populate the last row");
            std::function<void (juce::Component&)> visit = [&] (juce::Component& component)
            {
                if (dynamic_cast<ProjectRowComponent*> (&component)) ++liveRows;
                for (auto* child : component.getChildren()) visit (*child);
            };
            visit (*list);
            check (liveRows > 0 && liveRows <= 16, "Cold publication materialised the entire library as row widgets");
            const auto searching = juce::Time::getMillisecondCounterHiRes();
            search->setText ("Cold 00000");
            while (list->getNumRows() != 1) pump();
            searchMs = juce::Time::getMillisecondCounterHiRes() - searching;
            search->setText (""); while (list->getNumRows() != count) pump();
        }
        for (int i = 0; i < 20; ++i) pump(); // deliver deferred first-run callbacks too
        if ((count == 10000 && missingPercent == 50) || count == 0)
            saveImage (*scene, artifact (count, missingPercent, mode, "-ready.png"));
        if (count == 100 && missingPercent == 10)
        {
            for (int i = 0; i < expectedMissing; ++i)
            {
                const auto name = "Cold " + juce::String (i).paddedLeft ('0', 5) + ".flp";
                const auto parked = parkedProjects (projects).getChildFile (name), project = projects.getChildFile (name);
                if (shared) check (parked.moveFileTo (project), "Cannot restore parked project fixture");
                else writeFixtureText (project, "restored project");
            }
            list->refresh();
            while (ProjectScanner::librarySnapshot()->missingProjects != 0 || status->isVisible()) pump();
            check (list->getNumRows() == count, "Restoring projects removed preview entries");
            recovered = true;
        }
    }
    const auto closing = juce::Time::getMillisecondCounterHiRes(); scene.reset();
    closeMs = juce::Time::getMillisecondCounterHiRes() - closing;
    check (closeMs < 500, "Closing the cold scene waited for its index worker");
    const auto shutdown = juce::Time::getMillisecondCounterHiRes();
    ProjectScanner::beginShutdown();
    while (! ProjectScanner::shutdownReady() || WaveformPlayerComponent::pendingLoads() != 0 || BackgroundRetirement::pending() != 0)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        check (juce::Time::getMillisecondCounterHiRes() - shutdown < 10000, "Cold shutdown did not retire accepted work");
    }
    shutdownMs = juce::Time::getMillisecondCounterHiRes() - shutdown;
    check (juce::Component::getNumCurrentlyModalComponents() == 0, "A cold-start callback outlived its scene");
    std::sort (dispatch.begin(), dispatch.end());
    const auto percentile = [&] (double p) { return dispatch.empty() ? 0.0 : dispatch[(size_t) ((dispatch.size() - 1) * p)]; };
    juce::var report (new juce::DynamicObject()); auto* result = report.getDynamicObject();
    result->setProperty ("passed", true); result->setProperty ("count", count); result->setProperty ("missingPercent", missingPercent);
    result->setProperty ("mode", mode); result->setProperty ("pendingAtConstruction", pendingAtConstruction);
    result->setProperty ("constructorMs", constructedMs); result->setProperty ("readyMs", readyMs);
    result->setProperty ("fixtureSetupMs", setupMs);
    result->setProperty ("sharedFixture", shared);
    result->setProperty ("missingProjects", missing); result->setProperty ("liveRows", liveRows);
    result->setProperty ("searchWithDebounceMs", searchMs); result->setProperty ("closeMs", closeMs);
    result->setProperty ("shutdownMs", shutdownMs); result->setProperty ("dispatchSamples", (int) dispatch.size());
    result->setProperty ("dispatchP99Ms", percentile (0.99)); result->setProperty ("dispatchMaxMs", percentile (1));
    result->setProperty ("configRecovered", recovered);
    result->setProperty ("searchEnteredDuringLoading", searchEnteredDuringLoading);
    check (LibraryStore::writeJsonAtomically (artifact (count, missingPercent, mode, ".json"), report).wasOk(), "Cannot write cold-startup report");
    std::cout << "COLD_STARTUP " << juce::JSON::toString (report, true) << '\n';
}

void testColdStartupMatrix (const juce::File& root)
{
    auto run = [] (int count, int missing, const juce::String& mode, const juce::File& library = juce::File{}, const juce::File& projects = juce::File{})
    {
        const auto report = artifact (count, missing, mode, ".json");
        check (! report.existsAsFile() || report.deleteFile(), "Cannot remove previous cold-start report");
        juce::ChildProcess child;
        juce::StringArray arguments { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                     "--cold-startup-case", juce::String (count), juce::String (missing), mode };
        if (library != juce::File{}) { arguments.add (library.getFullPathName()); arguments.add (projects.getFullPathName()); }
        check (child.start (arguments),
               "Cannot launch fresh cold-startup process");
        juce::String output;
        std::thread reader ([&] { output = child.readAllProcessOutput(); });
        struct RetireChild
        {
            juce::ChildProcess& child; std::thread& reader;
            ~RetireChild() { if (child.isRunning()) child.kill(); if (reader.joinable()) reader.join(); }
        } retire { child, reader };
        const auto begun = juce::Time::getMillisecondCounterHiRes();
        while (child.isRunning())
        {
            if (juce::Time::getMillisecondCounterHiRes() - begun >= 150000)
            {
                child.kill(); reader.join();
                std::cout << output << std::flush;
                check (false, "Fresh cold-startup process timed out");
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        reader.join();
        std::cout << output << std::flush;
        std::cout << "COLD_PROCESS_WALL count=" << count << " missing=" << missing << " mode=" << mode
                  << " wallMs=" << juce::Time::getMillisecondCounterHiRes() - begun << '\n';
        check (child.getExitCode() == 0, "Fresh cold-startup process failed: " + juce::String (count) + "/" + juce::String (missing) + "/" + mode);
        const auto result = LibraryStore::readJson (report);
        check ((bool) result.getProperty ("passed", false) && (int) result["count"] == count
               && (int) result["missingPercent"] == missing && result["mode"].toString() == mode,
               "Fresh process returned no matching successful report");
    };
    for (const auto count : { 100, 1000, 10000 })
    {
        const auto library = root.getChildFile ("library-" + juce::String (count));
        const auto projects = root.getChildFile ("projects-" + juce::String (count)), parked = parkedProjects (projects);
        const auto preparing = juce::Time::getMillisecondCounterHiRes();
        // Only files are shared. No ProjectScanner API runs in this process;
        // each child initialises its own index/caches against its own profile.
        prepareFixture (library, projects, count, 0);
        check (parked.createDirectory().wasOk(), "Cannot create project parking fixture");
        std::cout << "COLD_SHARED_FIXTURE count=" << count << " setupMs=" << juce::Time::getMillisecondCounterHiRes() - preparing << std::endl;
        for (const auto missing : { 0, 10, 50 })
        {
            for (int i = 0; i < count * missing / 100; ++i)
            {
                const auto name = "Cold " + juce::String (i).paddedLeft ('0', 5) + ".flp";
                const auto project = projects.getChildFile (name), destination = parked.getChildFile (name);
                if (project.existsAsFile())
                    check (! destination.exists() && project.moveFileTo (destination), "Cannot park missing project fixture");
                else check (destination.existsAsFile(), "Missing fixture was lost outside its search root");
            }
            run (count, missing, "ready", library, projects);
        }
        if (count == 10000) run (count, 50, "close", library, projects);
    }
    run (0, 0, "ready"); run (0, 0, "error"); run (0, 0, "offline");
    std::cout << "PASS: cold MainComponent startup in 13 fresh processes, loading/missing/empty/error/offline recovery and early shutdown\n";
}
