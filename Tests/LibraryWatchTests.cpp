#include "LibraryTests.h"
#include "../App/Source/LibraryWatch.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

void testLibraryWatch (const juce::File& fixtureRoot)
{
   #if JUCE_WINDOWS
    const auto check = [](bool value, const char* reason) { if (! value) throw std::runtime_error (reason); };
    const auto base = fixtureRoot.getChildFile ("native-watch");
    const auto config = base.getChildFile ("profile/config.json");
    const auto library = base.getChildFile ("library");
    const auto projects = base.getChildFile ("projects");
    check (config.getParentDirectory().createDirectory().wasOk() && library.getChildFile ("entry").createDirectory().wasOk()
           && projects.createDirectory().wasOk() && config.replaceWithText ("{}"), "Cannot create native watch fixture");
    LibraryWatch watch;
    watch.configure (config, library, { projects });
    check (watch.hasCompleteCoverage(), "Native directory watch did not cover the local fixture");
    auto expectChange = [&](unsigned expected, const char* stage)
    {
        const auto until = juce::Time::getMillisecondCounter() + 2000;
        unsigned observed = LibraryWatch::None;
        while ((observed & expected) != expected && juce::Time::getMillisecondCounter() < until)
        { observed |= watch.takeChanges(); juce::Thread::sleep (1); }
        auto quietUntil = juce::Time::getMillisecondCounter() + 100;
        while (juce::Time::getMillisecondCounter() < quietUntil && juce::Time::getMillisecondCounter() < until)
        {
            const auto next = watch.takeChanges(); observed |= next;
            if (next != LibraryWatch::None) quietUntil = juce::Time::getMillisecondCounter() + 100;
            juce::Thread::sleep (1);
        }
        if (observed != expected) throw std::runtime_error (std::string (stage) + ": expected changes=" + std::to_string (expected) + ", observed=" + std::to_string (observed));
    };
    check (config.getSiblingFile ("plugin.log").replaceWithText ("unrelated log"), "Cannot write unrelated profile log");
    for (int i = 0; i < 50; ++i)
    { check (watch.takeChanges() == LibraryWatch::None, "Logger activity created a configuration refresh feedback loop"); juce::Thread::sleep (2); }
    check (config.replaceWithText ("{\"changed\":true}"), "Cannot modify watched configuration");
    expectChange (LibraryWatch::Library, "configuration write");
    check (library.getChildFile ("entry/metadata.json").replaceWithText ("{}"), "Cannot modify nested library manifest");
    expectChange (LibraryWatch::Library, "nested manifest write");
    check (projects.getChildFile ("New.flp").replaceWithText ("candidate"), "Cannot add watched candidate");
    expectChange (LibraryWatch::Candidates, "candidate creation");
    check (projects.getChildFile ("New.flp").moveFileTo (projects.getChildFile ("Renamed.flp")), "Cannot rename watched candidate");
    expectChange (LibraryWatch::Candidates, "candidate rename");
    // Many events before the worker consumes a completion must remain an
    // inventory invalidation, even when individual notifications overflow.
    for (int i = 0; i < 900; ++i)
        check (library.getChildFile ("entry/burst-" + juce::String (i) + ".json").replaceWithText ("{}"), "Cannot create watcher burst");
    expectChange (LibraryWatch::Library, "notification burst");
    const auto other = base.getChildFile ("other-library");
    check (other.createDirectory().wasOk(), "Cannot create replacement watched library");
    watch.configure (config, other, { projects });
    check (watch.hasCompleteCoverage(), "Watcher did not reconfigure after replacing its roots");
    check (library.getChildFile ("entry/metadata.json").replaceWithText ("{\"old\":true}"), "Cannot modify retired root");
    for (int i = 0; i < 50; ++i)
    { check (watch.takeChanges() == LibraryWatch::None, "Retired root still triggered its previous watcher"); juce::Thread::sleep (2); }
    check (other.getChildFile ("metadata.json").replaceWithText ("{}"), "Cannot modify new watched root");
    expectChange (LibraryWatch::Library, "new root write");
    check (other.moveFileTo (base.getChildFile ("offline-library")), "Cannot rename watched root");
    expectChange (LibraryWatch::Library, "root rename");
    watch.configure (config, other, {});
    check (! watch.hasCompleteCoverage(), "A disappeared root incorrectly disabled polling fallback");
    watch.clear();
    DWORD handlesBefore = 0, handlesAfter = 0;
    check (GetProcessHandleCount (GetCurrentProcess(), &handlesBefore), "Cannot measure native handles");
    for (int i = 0; i < 100; ++i)
    {
        LibraryWatch closing; closing.configure (config, library, { projects });
        check (closing.hasCompleteCoverage(), "Repeated native watcher startup failed");
    }
    check (GetProcessHandleCount (GetCurrentProcess(), &handlesAfter) && handlesAfter <= handlesBefore + 8,
           "Closing pending native watch requests leaked their handles");
    juce::Array<juce::File> manyRoots;
    for (int i = 0; i < 20; ++i)
    {
        const auto path = projects.getChildFile (juce::String (i));
        check (path.createDirectory().wasOk(), "Cannot create watcher budget fixture"); manyRoots.add (path);
    }
    watch.configure (config, library, manyRoots);
    check (! watch.hasCompleteCoverage(), "Watcher overflow did not retain polling fallback");
    std::cout << "PASS: native configuration/nested-library/candidate notifications, log exclusion, burst, root replacement/deletion, polling fallback and 100 pending-request closes; handles="
              << handlesBefore << " -> " << handlesAfter << '\n';
   #else
    juce::ignoreUnused (fixtureRoot);
   #endif
}
