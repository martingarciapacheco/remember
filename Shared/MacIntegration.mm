#import <AppKit/AppKit.h>
#include <libproc.h>
#include <unistd.h>
#include "MacIntegration.h"
#include "BackgroundRetirement.h"

namespace RememberMac
{
namespace
{
juce::String string (NSString* value)
{ return value ? juce::String::fromUTF8 ([value UTF8String]).convertToPrecomposedUnicode() : juce::String{}; }
struct WindowCache
{
    BackgroundRetirement::ModuleReference module;
    juce::CriticalSection lock;
    std::vector<DocumentWindow> windows;
    juce::uint32 sampledAt = 0;
    bool scheduled = false, sampled = false;
};
}
HostProject detectHostProject (const juce::String& daw)
{
    if (! daw.containsIgnoreCase ("FL Studio") && ! daw.containsIgnoreCase ("Ableton")) return { false, {}, {}, daw };
    static const auto cache = std::make_shared<WindowCache>();
    std::vector<DocumentWindow> windows;
    bool request = false;
    {
        const juce::ScopedLock guard (cache->lock);
        const auto elapsed = juce::Time::getMillisecondCounter() - cache->sampledAt;
        if (cache->sampled && elapsed <= 2000) windows = cache->windows;
        if (! cache->scheduled && (! cache->sampled || elapsed >= 250))
        { cache->scheduled = true; request = true; }
    }
    if (request && ! juce::MessageManager::callAsync ([owner = cache]
    {
        // AppKit is main-thread-only. Discovery never waits for the host UI,
        // including during plugin loading/unloading. No other process is read.
        @autoreleasepool
        {
            std::vector<DocumentWindow> next;
            if ([NSApp modalWindow] == nil)
                for (NSWindow* window in [NSApp windows])
                {
                    if ([window isKindOfClass: [NSPanel class]] || [window parentWindow] != nil
                        || [window level] != NSNormalWindowLevel
                        || (! [window isVisible] && ! [window isMiniaturized])
                        || ([window styleMask] & NSWindowStyleMaskMiniaturizable) == 0) continue;
                    const auto title = string ([window title]);
                    NSURL* document = [window representedURL];
                    auto path = [document isFileURL] ? string ([document path]) : juce::String{};
                    if (path.isEmpty() && (title == "Remember" || title.startsWith ("Remember -"))) continue;
                    if (title.isNotEmpty() || path.isNotEmpty()) next.push_back ({ title, path });
                }
            const juce::ScopedLock guard (owner->lock);
            owner->windows = std::move (next);
            owner->sampledAt = juce::Time::getMillisecondCounter();
            owner->sampled = true; owner->scheduled = false;
        }
    }))
    {
        const juce::ScopedLock guard (cache->lock); cache->scheduled = false;
    }
    return projectFromWindows (windows, daw);
}
juce::File applicationExecutable (const juce::File& bundle)
{
    @autoreleasepool
    {
        NSBundle* app = [NSBundle bundleWithPath: [NSString stringWithUTF8String: bundle.getFullPathName().toRawUTF8()]];
        const auto path = string ([app executablePath]);
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File{};
    }
}
juce::Image applicationIcon (const juce::File& bundle)
{
    @autoreleasepool
    {
        NSImage* source = [[NSWorkspace sharedWorkspace] iconForFile:
            [NSString stringWithUTF8String: bundle.getFullPathName().toRawUTF8()]];
        NSBitmapImageRep* bitmap = [NSBitmapImageRep imageRepWithData: [source TIFFRepresentation]];
        NSData* png = [bitmap representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
        const auto image = png ? juce::ImageFileFormat::loadFrom ([png bytes], (size_t) [png length]) : juce::Image{};
        return image.isValid() ? image.rescaled (64, 64) : juce::Image{};
    }
}
juce::Result requireFlStudioClosed()
{
    @autoreleasepool
    {
        // NSWorkspace knows bundle identity even when the executable is renamed.
        for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
            if (string ([app bundleIdentifier]).equalsIgnoreCase ("com.image-line.flstudio"))
                return juce::Result::fail ("Save your work and close FL Studio before starting offline rendering.");
        // Direct CLI launches need not register with Launch Services.
        std::vector<pid_t> processes (4096);
        const auto bytes = proc_listpids (PROC_ALL_PIDS, 0, processes.data(), (int) (processes.size() * sizeof (pid_t)));
        if (bytes <= 0 || bytes >= (int) (processes.size() * sizeof (pid_t)))
            return juce::Result::fail ("Cannot check whether FL Studio is running.");
        for (int i = 0; i < bytes / (int) sizeof (pid_t); ++i)
        {
            char path[PROC_PIDPATHINFO_MAXSIZE] {};
            if (processes[(size_t) i] <= 0 || proc_pidpath (processes[(size_t) i], path, sizeof (path)) <= 0) continue;
            if (juce::File (juce::String::fromUTF8 (path)).getFileName() == "OsxFL")
                return juce::Result::fail ("Save your work and close FL Studio before starting offline rendering.");
        }
        return juce::Result::ok();
    }
}
}
