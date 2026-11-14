#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>
#include "../Shared/MacIntegration.h"
#include "../App/Source/MacMediaTransport.h"
#include "../App/Source/AppShell.h"
#include "../App/Source/ProjectScanner.h"
#include <iostream>
#include <stdexcept>
#include <thread>

@interface RememberDockTestDelegate : NSObject <NSApplicationDelegate>
@end
@implementation RememberDockTestDelegate
@end

namespace
{
struct StandaloneDelegate
{
    StandaloneDelegate()
    {
        previous = [NSApp.delegate retain];
        current = [RememberDockTestDelegate new];
        NSApp.delegate = current;
    }
    ~StandaloneDelegate() { NSApp.delegate = previous; [current release]; [previous release]; }
    id previous = nil;
    RememberDockTestDelegate* current = nil;
};
void check (bool condition, const char* why) { if (! condition) throw std::runtime_error (why); }
template<class Predicate> void awaitUi (Predicate condition)
{
    const auto until = juce::Time::getMillisecondCounter() + 5000;
    while (! condition())
    {
        check (juce::Time::getMillisecondCounter() < until, "Mac native integration timed out");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
}
}
void testMacPlatform (const juce::File& root)
{
    using namespace RememberMac;
    const auto flp = root.getChildFile (juce::String::fromUTF8 ("Canción & ensayo.flp"));
    check (flp.replaceWithText ("isolated document fixture"), "Cannot write Mac document fixture");
    auto result = projectFromWindows ({ { "Unrelated caption", flp.getFullPathName() } }, "FL Studio");
    check (result.available && result.path == flp.getFullPathName() && result.name == flp.getFileNameWithoutExtension(),
        "Mac document URL did not take precedence over its title");
    result = projectFromWindows ({ { "Tema [Carpeta Project]", {} } }, "Ableton Live");
    check (result.available && result.name == "Tema [Carpeta Project]", "Mac discovery guessed an unverified title format");
    check (! projectFromWindows ({ { "Tema", {} }, { "Otro", {} } }, "Ableton Live").available,
        "Mac guessed which of two documents belongs to a plugin");
    check (! projectFromWindows ({ { "Tema", flp.getFullPathName() } }, "Ableton Live").available,
        "Mac accepted a foreign DAW document");
    check (! projectFromWindows ({ { "Tema", {} } }, "Unknown").available,
        "An unknown host gained unverified automatic association");
    result = projectFromWindows ({ { "Untitled", {} } }, "FL Studio");
    check (result.available && result.name.isEmpty() && result.path.isEmpty(), "Unsaved Mac document became a saved project");
    ProjectIdentity::CandidateIndex candidates;
    check (candidates.rebuild ({ root }) && candidates.resolve (juce::String::fromUTF8 ("Cancio\xcc\x81n & ensayo"), "FL Studio") == flp.getFullPathName(),
        "Decomposed Mac project names did not resolve to their composed filename");
    @autoreleasepool
    {
        NSWindow* window = [[NSWindow alloc] initWithContentRect: NSMakeRect (40, 40, 640, 480)
            styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing: NSBackingStoreBuffered defer: NO];
        struct Cleanup { NSWindow* window; ~Cleanup() { [window orderOut: nil]; [window release]; } } cleanup { window };
        [window setTitle: @"Remember native host document fixture"];
        [window setRepresentedURL: [NSURL fileURLWithPath: [NSString stringWithUTF8String: flp.getFullPathName().toRawUTF8()]]];
        [window orderFront: nil];
        const auto began = juce::Time::getMillisecondCounterHiRes();
        detectHostProject ("FL Studio");
        check (juce::Time::getMillisecondCounterHiRes() - began < 50, "Mac discovery waited for the host UI");
        awaitUi ([&] { return detectHostProject ("FL Studio").path == flp.getFullPathName(); });
        std::atomic<bool> rejectedStale { false };
        std::thread worker ([&]
        {
            juce::Thread::sleep (2100);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const auto stale = detectHostProject ("FL Studio");
            rejectedStale.store (! stale.available && juce::Time::getMillisecondCounterHiRes() - start < 50);
        });
        worker.join(); // Deliberately withhold host dispatch, as during plugin loading.
        check (rejectedStale.load(), "A blocked host UI left a stale Mac project eligible for capture");
        awaitUi ([&] { return detectHostProject ("FL Studio").path == flp.getFullPathName(); });
    }
    std::cout << "PASS: native Cocoa document URL, title fallback, ambiguity, unsaved/foreign hosts, nonblocking discovery and stale-cache rejection\n";
    {
        MacMediaTransport::State state;
        MacMediaTransport media ([&] { return state; }, [] (int) {});
        auto* centre = [MPRemoteCommandCenter sharedCommandCenter];
        check (! centre.playCommand.enabled, "An idle Mac browser claimed media controls");
        state = { true, true, true, juce::String::fromUTF8 ("Canción sin traducir"), 45, 3 };
        awaitUi ([&] { return centre.playCommand.enabled; });
        auto* info = [MPNowPlayingInfoCenter defaultCenter];
        check ([info.nowPlayingInfo[MPMediaItemPropertyTitle] isEqualToString: @"Canción sin traducir"]
            && [info.nowPlayingInfo[MPMediaItemPropertyPlaybackDuration] doubleValue] == 45
            && [info.nowPlayingInfo[MPNowPlayingInfoPropertyElapsedPlaybackTime] doubleValue] == 3,
            "Mac Now Playing lost the preview metadata");
        state.playing = false; state.hasQueue = false;
        awaitUi ([&] { return info.playbackState == MPNowPlayingPlaybackStatePaused && ! centre.nextTrackCommand.enabled; });
        state.canPlay = false;
        awaitUi ([&] { return info.nowPlayingInfo == nil && ! centre.playCommand.enabled; });
    }
    check (! [MPRemoteCommandCenter sharedCommandCenter].togglePlayPauseCommand.enabled,
        "Closed Mac media transport retained commands");
    std::cout << "PASS: native media registration, preview metadata, pause, queue availability and teardown\n";
    const auto previews = root.getChildFile ("shell-previews");
    check (previews.createDirectory().wasOk() && ProjectScanner::saveSettings (previews, { root }, 60, 4, false).wasOk(),
        "Cannot configure isolated native Mac shell");
    bool quit = false;
    // The console test runner has no JUCEApplication and therefore no app
    // delegate. Supply one for AppKit to route the real reopen AppleEvent.
    StandaloneDelegate standaloneDelegate;
    AppShell shell ([&] { quit = true; });
    const auto mainWindow = [] () -> juce::DocumentWindow*
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*> (desktop.getComponent (i)); window && window->getName() == "Remember")
                return window;
        return nullptr;
    };
    awaitUi ([&] { const auto* window = mainWindow(); return window && window->isVisible(); });
    auto* window = mainWindow();
    check (window->isUsingNativeTitleBar(), "The Mac standalone lost its native window controls");
    window->closeButtonPressed();
    check (! window->isVisible(), "Closing the Mac window did not retain the tray process");
    auto* delegate = [NSApp delegate];
    check ([delegate respondsToSelector: @selector (applicationShouldHandleReopen:hasVisibleWindows:)],
        "Mac delegate did not register Dock reopen");
    // Exercise the native Dock delegate entry point rather than shell.open().
    // The console runner is not a Launch Services AppleEvent target.
    check (! [delegate applicationShouldHandleReopen: NSApp hasVisibleWindows: NO],
        "Dock reopen requested default document creation");
    awaitUi ([&] { return window->isVisible(); });
    window->closeButtonPressed();
    [delegate applicationShouldHandleReopen: NSApp hasVisibleWindows: YES];
    awaitUi ([&] { return window->isVisible(); });
    window->setMinimised (true);
    [delegate applicationShouldHandleReopen: NSApp hasVisibleWindows: YES];
    awaitUi ([&] { return window->isVisible() && ! window->isMinimised(); });
    shell.requestQuit();
    awaitUi ([&] { return quit; });
    check (mainWindow() == nullptr, "Mac quit retained its main window");
    std::cout << "PASS: native Mac window chrome, close-to-tray, Dock delegate reopen, restore and asynchronous shutdown\n";
}
