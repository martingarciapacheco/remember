#include "ShellTests.h"
#include "../App/Source/AppShell.h"
#include "../App/Source/MainComponent.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <atomic>
#include <stdexcept>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <shellapi.h>
#endif

namespace
{
void check (bool value, const juce::String& reason)
{ if (! value) throw std::runtime_error (reason.toStdString()); }
template<class Condition> void awaitUi (Condition condition)
{
    const auto begun = juce::Time::getMillisecondCounterHiRes();
    while (! condition())
    {
        check (juce::Time::getMillisecondCounterHiRes() - begun < 10000, "Native shell callback timed out");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    }
}
template<class Type> Type* desktopComponent()
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* typed = dynamic_cast<Type*> (desktop.getComponent (i))) return typed;
    return nullptr;
}
template<class Type> Type* childOf (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        if (auto* typed = dynamic_cast<Type*> (child)) return typed;
    return nullptr;
}
juce::File artifact (const juce::String& mode)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts/shell");
    check (folder.createDirectory().wasOk(), "Cannot create shell artifacts");
    return folder.getChildFile (mode + ".json");
}
struct QueryGate
{
    juce::WaitableEvent entered, release;
    std::atomic<bool> releasedBeforeDeadline { false };
};
struct ReleaseGate
{
    std::shared_ptr<QueryGate> gate;
    ~ReleaseGate() { gate->release.signal(); }
};
#if JUCE_WINDOWS
void requireOwnWindow (HWND hwnd)
{
    DWORD owner = 0;
    check (hwnd != nullptr && IsWindow (hwnd) && GetWindowThreadProcessId (hwnd, &owner) != 0
           && owner == GetCurrentProcessId(), "Native shell handle is not a window owned by this test");
}
NOTIFYICONIDENTIFIER trayIdentifier (juce::SystemTrayIconComponent& tray)
{
    // Pinned JUCE Windows implementation returns its NOTIFYICONDATA, not HWND.
    const auto* data = static_cast<const NOTIFYICONDATAW*> (tray.getNativeHandle());
    check (data != nullptr && data->cbSize == sizeof (NOTIFYICONDATAW)
           && data->uCallbackMessage == WM_USER + 100, "Unexpected JUCE native tray contract");
    requireOwnWindow (data->hWnd);
    NOTIFYICONIDENTIFIER identifier {};
    identifier.cbSize = sizeof (identifier);
    identifier.hWnd = data->hWnd;
    identifier.uID = data->uID;
    return identifier;
}
bool registeredTray (const NOTIFYICONIDENTIFIER& identifier)
{
    RECT bounds {};
    return SUCCEEDED (Shell_NotifyIconGetRect (&identifier, &bounds))
        && bounds.right > bounds.left && bounds.bottom > bounds.top;
}
void trayEvent (const NOTIFYICONIDENTIFIER& identifier, UINT event)
{
    requireOwnWindow (identifier.hWnd);
    // JUCE merges the notification with physical realtime modifier state.
    // Wait for neutral input before injecting our own window notification;
    // never synthesize or release keys/buttons on the user's desktop.
    awaitUi ([] { return juce::ModifierKeys::getCurrentModifiersRealtime().getRawFlags() == 0; });
    SendMessageW (identifier.hWnd, WM_USER + 100, identifier.uID, event);
}
#endif
}

void testShellCase (const juce::File& root, const juce::String& mode)
{
#if ! JUCE_WINDOWS
    juce::ignoreUnused (root, mode);
    check (false, "Native shell acceptance currently requires Windows");
#else
    check (mode == "early-close" || mode == "restore" || mode == "user-move"
           || mode == "read-failure" || mode == "read-locked" || mode == "early-move"
           || mode == "move-hidden" || mode == "tray" || mode == "late-menu", "Invalid shell case");
    const auto library = root.getChildFile ("library"), projects = root.getChildFile ("projects");
    check (library.createDirectory().wasOk() && projects.createDirectory().wasOk(), "Cannot create shell library");
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    check (display != nullptr && display->userArea.getWidth() >= 700 && display->userArea.getHeight() >= 560,
           "Native shell acceptance needs a usable Windows desktop of at least 700x560");
    const auto savedBounds = juce::Rectangle<int> (display->userArea.getX() + 20, display->userArea.getY() + 30,
        juce::jmin (930, display->userArea.getWidth() - 60), juce::jmin (700, display->userArea.getHeight() - 60));
    const auto savedState = savedBounds.toString();
    juce::var config (new juce::DynamicObject()); auto* object = config.getDynamicObject();
    object->setProperty ("previewsFolder", library.getFullPathName());
    object->setProperty ("projectsRootFolders", juce::Array<juce::var> { projects.getFullPathName() });
    object->setProperty ("windowState", savedState);
    object->setProperty ("unrelatedShellSentinel", "preserve me");
    check (LibraryStore::writeJsonAtomically (LibraryStore::configFile(), config).wasOk(), "Cannot configure shell fixture");

    // The single read queue is held before any production UI is constructed.
    // Indexing has its own worker and remains independent of this slow read.
    auto gate = std::make_shared<QueryGate>(); ReleaseGate releaseGate { gate };
    ProjectScanner::queryAsync ([gate]
    {
        gate->entered.signal(); gate->releasedBeforeDeadline = gate->release.wait (15000);
        return gate->releasedBeforeDeadline ? juce::Result::ok() : juce::Result::fail ("Shell read gate timed out");
    });
    check (gate->entered.wait (1000), "Shell read gate did not enter its worker");
    int quitCallbacks = 0;
    const auto begun = juce::Time::getMillisecondCounterHiRes();
    auto shell = std::make_unique<AppShell> ([&] { ++quitCallbacks; });
    const auto constructorMs = juce::Time::getMillisecondCounterHiRes() - begun;
    auto* window = desktopComponent<juce::DocumentWindow>();
    auto* tray = desktopComponent<juce::SystemTrayIconComponent>();
    check (window && tray && window->getName() == "Remember" && window->isVisible(), "Production native shell was not created");
    const auto windowHandle = static_cast<HWND> (window->getPeer()->getNativeHandle());
    requireOwnWindow (windowHandle);
    const auto identifier = trayIdentifier (*tray);
    const bool trayRegistered = registeredTray (identifier);
    std::cout << "SHELL_NATIVE mode=" << mode << " trayRegistered=" << trayRegistered
              << " constructorMs=" << constructorMs << " initialState=" << window->getWindowStateAsString() << std::endl;
    auto* content = dynamic_cast<MainComponent*> (window->getContentComponent());
    check (content != nullptr, "Native shell does not use production MainComponent");
    auto* player = childOf<WaveformPlayerComponent> (*content);
    check (player != nullptr && ! player->isAudioDeviceOpen(), "Shell startup opened a physical audio device");
    check (constructorMs < 2000, "Native window constructor waited for its settings read");
    juce::String expectedState = savedState;
    double quitRequestMs = 0, shutdownMs = 0;
    auto menuResult = std::make_shared<int> (0);

    if (mode == "early-close" || mode == "early-move")
    {
        if (mode == "early-move")
        {
            window->setBounds (savedBounds.translated (16, 16).withSize (savedBounds.getWidth() - 20, savedBounds.getHeight() - 20));
            expectedState = window->getWindowStateAsString();
        }
        const auto closing = juce::Time::getMillisecondCounterHiRes();
        shell->requestQuit(); quitRequestMs = juce::Time::getMillisecondCounterHiRes() - closing;
        check (quitRequestMs < 500 && ! IsWindow (windowHandle) && ! ProjectScanner::shutdownReady(),
               "Early native close waited for I/O or lost its accepted query");
        // Message dispatch must remain live while the read is still retained.
        auto dispatched = std::make_shared<bool> (false);
        juce::MessageManager::callAsync ([dispatched] { *dispatched = true; });
        awaitUi ([&] { return *dispatched; });
        if (mode == "early-close")
            check (LibraryStore::readJson (LibraryStore::configFile())["windowState"].toString() == savedState,
                   "Closing before the saved bounds read overwrote the previous window state");
        check (quitCallbacks == 0, "Quit callback ran before accepted background work drained");
        gate->release.signal();
    }
    else
    {
        if (mode == "user-move")
        {
            window->setBounds (savedBounds.translated (16, 16).withSize (savedBounds.getWidth() - 20, savedBounds.getHeight() - 20));
            expectedState = window->getWindowStateAsString();
        }
        // A malformed configuration produces a failed strict read without
        // simulating an I/O failure or modifying any installed profile.
        const auto malformed = "{ broken shell configuration";
        if (mode == "read-failure") check (LibraryStore::configFile().replaceWithText (malformed), "Cannot corrupt shell fixture");
        struct CloseHandleOnExit
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
            void close() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle (handle); handle = INVALID_HANDLE_VALUE; } }
            ~CloseHandleOnExit() { close(); }
        } lockedConfig;
        if (mode == "read-locked")
        {
            lockedConfig.handle = CreateFileW (LibraryStore::configFile().getFullPathName().toWideCharPointer(),
                                              GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            check (lockedConfig.handle != INVALID_HANDLE_VALUE, "Cannot exclusively lock native shell config");
        }
        auto readsDrained = std::make_shared<bool> (false);
        ProjectScanner::queryAsync ([] { return juce::Result::ok(); }, [readsDrained] (juce::Result) { *readsDrained = true; });
        gate->release.signal(); awaitUi ([&] { return *readsDrained; });
        check (! player->isAudioDeviceOpen() && juce::Component::getNumCurrentlyModalComponents() == 0,
               "Shell settings completion opened an unsolicited device/dialog");
        if (mode == "read-failure")
        {
            // Repair the test file after the failed read. Untouched default
            // geometry must not replace the previously saved, now readable state.
            check (LibraryStore::configFile().loadFileAsString() == malformed, "Failed shell read rewrote config");
            check (LibraryStore::writeJsonAtomically (LibraryStore::configFile(), config).wasOk(), "Cannot repair shell config");
        }
        else if (mode == "read-locked")
        {
            lockedConfig.close();
            check (LibraryStore::readJson (LibraryStore::configFile())["windowState"].toString() == savedState,
                   "A locked bounds read changed the previous settings");
        }
        else
        {
            const auto actualState = window->getWindowStateAsString();
            std::cout << "SHELL_BOUNDS expected=" << expectedState << " actual=" << actualState << std::endl;
            if (mode == "user-move")
                check (actualState == expectedState, "Late saved bounds read undid a user move");
            else
            {
                // Windows converts native frame/client coordinates to logical
                // pixels. Fractional DPI can round each edge by one pixel.
                const auto actual = juce::Rectangle<int>::fromString (actualState);
                check (std::abs (actual.getX() - savedBounds.getX()) <= 1
                       && std::abs (actual.getY() - savedBounds.getY()) <= 1
                       && std::abs (actual.getWidth() - savedBounds.getWidth()) <= 1
                       && std::abs (actual.getHeight() - savedBounds.getHeight()) <= 1,
                       "Late saved bounds read failed native restoration");
                expectedState = actualState;
            }
        }

        if (mode == "move-hidden")
        {
            window->setTopLeftPosition (savedBounds.getX() + 16, savedBounds.getY() + 16);
            // Do not force JUCE's last-visible-state update through its getter.
            expectedState = window->getBounds().toString();
            SendMessageW (windowHandle, WM_CLOSE, 0, 0);
            check (! window->isVisible(), "Moved native window did not hide");
        }

        if (mode == "tray" || mode == "late-menu")
        {
            check (trayRegistered, "Explorer did not register the real tray icon; native tray acceptance cannot run");
            SendMessageW (windowHandle, WM_CLOSE, 0, 0);
            check (! window->isVisible() && IsWindow (windowHandle) && ! player->isTimerRunning(),
                   "Native close did not hide the existing window and idle UI");
            trayEvent (identifier, WM_LBUTTONDOWN); trayEvent (identifier, WM_LBUTTONUP);
            std::cout << "SHELL_REOPEN visible=" << window->isVisible()
                      << " samePeer=" << (window->getPeer()->getNativeHandle() == windowHandle)
                      << " audioOpen=" << player->isAudioDeviceOpen()
                      << " modals=" << juce::Component::getNumCurrentlyModalComponents()
                      << " modifiers=" << juce::ModifierKeys::getCurrentModifiersRealtime().getRawFlags() << std::endl;
            check (window->isVisible() && window->getPeer()->getNativeHandle() == windowHandle && ! player->isAudioDeviceOpen(),
                   "Native tray click failed to reopen the same window");
            SendMessageW (windowHandle, WM_CLOSE, 0, 0);
            trayEvent (identifier, WM_LBUTTONDBLCLK);
            check (window->isVisible(), "Native tray double-click failed to reopen the window");

            auto selectMenu = [&] (bool exit)
            {
                trayEvent (identifier, WM_RBUTTONDOWN); trayEvent (identifier, WM_RBUTTONUP);
                auto* popup = juce::Component::getCurrentlyModalComponent();
                check (popup != nullptr, "Native tray notification did not open the real menu");
                juce::ModalComponentManager::getInstance()->attachCallback (popup,
                    juce::ModalCallbackFunction::create ([menuResult] (int selected) { *menuResult = selected; }));
                check (popup->keyPressed (juce::KeyPress (exit ? juce::KeyPress::upKey : juce::KeyPress::downKey))
                       && popup->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)), "Native tray menu ignored keyboard selection");
            };
            if (mode == "tray")
            {
                SendMessageW (windowHandle, WM_CLOSE, 0, 0); selectMenu (false);
                awaitUi ([&] { return *menuResult == 1; });
                check (window->isVisible(), "Production Open menu action did not restore the window");
                *menuResult = 0; selectMenu (true);
                awaitUi ([&] { return *menuResult == 2; });
            }
            else
            {
                selectMenu (true);
                // Destroy the owner before the genuine selected menu result is
                // asynchronously delivered. SafePointer must retire the callback.
                shell.reset();
                awaitUi ([&] { return *menuResult == 2; });
                check (quitCallbacks == 0 && desktopComponent<juce::DocumentWindow>() == nullptr,
                       "Late tray callback invoked a destroyed owner or recreated its window");
                ProjectScanner::beginShutdown();
            }
        }
        if (shell)
        {
            const auto closing = juce::Time::getMillisecondCounterHiRes();
            shell->requestQuit(); quitRequestMs = juce::Time::getMillisecondCounterHiRes() - closing;
        }
    }
    const auto shutdown = juce::Time::getMillisecondCounterHiRes();
    if (shell)
    {
        shell->requestQuit(); shell->open();
        check (desktopComponent<juce::DocumentWindow>() == nullptr, "Quit admitted a replacement window");
        awaitUi ([&] { return quitCallbacks == 1; });
    }
    else awaitUi ([] { return ProjectScanner::shutdownReady() && WaveformPlayerComponent::pendingLoads() == 0
                                  && BackgroundRetirement::pending() == 0; });
    shutdownMs = juce::Time::getMillisecondCounterHiRes() - shutdown;
    check (ProjectScanner::shutdownReady() && WaveformPlayerComponent::pendingLoads() == 0 && BackgroundRetirement::pending() == 0
           && gate->releasedBeforeDeadline.load(), "Native quit left accepted work behind");
    shell.reset();
    check (! IsWindow (windowHandle) && ! IsWindow (identifier.hWnd) && ! registeredTray (identifier)
           && juce::Component::getNumCurrentlyModalComponents() == 0, "Native shell left window/tray/modal objects alive");
    const auto finalConfig = LibraryStore::readJson (LibraryStore::configFile());
    check (finalConfig["windowState"].toString() == expectedState, "Native shutdown saved an incorrect window state");
    check (finalConfig["unrelatedShellSentinel"].toString() == "preserve me", "Window persistence lost unrelated settings");
    juce::var report (new juce::DynamicObject()); auto* result = report.getDynamicObject();
    result->setProperty ("passed", true); result->setProperty ("mode", mode);
    result->setProperty ("nativeWindow", true); result->setProperty ("trayRegistered", trayRegistered);
    result->setProperty ("constructorMs", constructorMs); result->setProperty ("quitRequestMs", quitRequestMs);
    result->setProperty ("shutdownMs", shutdownMs); result->setProperty ("quitCallbacks", quitCallbacks);
    result->setProperty ("savedState", savedState); result->setProperty ("finalState", expectedState);
    result->setProperty ("nativeResourcesRetired", true); result->setProperty ("physicalAudioDeviceOpened", false);
    check (LibraryStore::writeJsonAtomically (artifact (mode), report).wasOk(), "Cannot write shell report");
    std::cout << "SHELL_RESULT " << juce::JSON::toString (report, true) << std::endl;
#endif
}

void testShellLifecycle (const juce::File& root, bool trayOnly)
{
    juce::ignoreUnused (root);
    const juce::StringArray modes = trayOnly ? juce::StringArray { "tray", "late-menu" }
        : juce::StringArray { "early-close", "early-move", "restore", "user-move", "read-failure", "read-locked", "move-hidden" };
    for (const auto& mode : modes)
    {
        const auto report = artifact (mode);
        check (! report.existsAsFile() || report.deleteFile(), "Cannot remove stale shell report");
        juce::ChildProcess child;
        check (child.start (juce::StringArray { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                               "--shell-case", mode }), "Cannot start fresh shell process");
        juce::String output; std::thread reader ([&] { output = child.readAllProcessOutput(); });
        struct RetireChild
        {
            juce::ChildProcess& child; std::thread& reader;
            ~RetireChild() { if (child.isRunning()) child.kill(); if (reader.joinable()) reader.join(); }
        } retire { child, reader };
        const auto begun = juce::Time::getMillisecondCounterHiRes();
        while (child.isRunning())
        {
            if (juce::Time::getMillisecondCounterHiRes() - begun >= 30000)
            {
                child.kill(); reader.join(); std::cout << output << std::flush;
                check (false, "Fresh native shell process timed out");
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        reader.join(); std::cout << output << std::flush;
        check (child.getExitCode() == 0, "Fresh native shell process failed: " + mode);
        const auto result = LibraryStore::readJson (report);
        check ((bool) result.getProperty ("passed", false) && result["mode"].toString() == mode,
               "No matching successful native shell report");
    }
    std::cout << "PASS: production native " << (trayOnly ? "tray" : "window") << " lifecycle in " << modes.size() << " fresh processes\n";
}
