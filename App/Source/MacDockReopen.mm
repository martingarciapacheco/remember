#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include "MacDockReopen.h"

@interface RememberDockReopenAction : NSObject
{
@public
    std::function<void()> action;
}
@end
@implementation RememberDockReopenAction
@end

namespace
{
char actionKey;
BOOL reopen (id delegate, SEL, NSApplication*, BOOL)
{
    auto* owner = (RememberDockReopenAction*) objc_getAssociatedObject (delegate, &actionKey);
    if (owner == nil) return YES;
    // Dock reopen must also restore the main window when a dialog is visible,
    // or when macOS counts a miniaturized window as visible.
    if (owner->action) owner->action();
    return NO; // We handled the event; don't create an untitled document.
}
}
struct MacDockReopen::Impl
{
    explicit Impl (std::function<void()> action)
    {
        jassert ([NSThread isMainThread]);
        delegate = [NSApp.delegate retain];
        const auto selector = @selector (applicationShouldHandleReopen:hasVisibleWindows:);
        const auto cls = object_getClass (delegate);
        const auto signature = juce::String (@encode(BOOL)) + "@:@" + @encode(BOOL);
        // Pinned JUCE does not implement this optional delegate method. Never
        // overwrite another implementation if that changes in a future update.
        const auto installed = cls != Nil && (class_addMethod (cls, selector, (IMP) reopen, signature.toRawUTF8())
            || class_getMethodImplementation (cls, selector) == (IMP) reopen);
        if (! installed) { juce::Logger::writeToLog ("Cannot register Mac Dock reopen handler."); return; }
        owner = [RememberDockReopenAction new];
        owner->action = std::move (action);
        jassert (objc_getAssociatedObject (delegate, &actionKey) == nil);
        objc_setAssociatedObject (delegate, &actionKey, owner, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    ~Impl()
    {
        jassert ([NSThread isMainThread]);
        if (owner != nil && objc_getAssociatedObject (delegate, &actionKey) == owner)
            objc_setAssociatedObject (delegate, &actionKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [owner release];
        [delegate release];
    }
    id delegate = nil;
    RememberDockReopenAction* owner = nil;
};
MacDockReopen::MacDockReopen (std::function<void()> action) : impl (std::make_unique<Impl> (std::move (action))) {}
MacDockReopen::~MacDockReopen() = default;
