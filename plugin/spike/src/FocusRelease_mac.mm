#include "FocusRelease.h"

#import <AppKit/AppKit.h>

namespace flowstate::spike
{

void releaseKeyboardFocusToHost (void* nativeHandle)
{
    auto* view = (NSView*) nativeHandle;

    if (view == nil)
        return;

    NSWindow* window = [view window];

    if (window == nil)
        return;

    // Prefer the host's container view; if it refuses first responder, fall back to the window
    // itself, whose keyDown: reaches the host's window/application handlers.
    NSView* hostView = [view superview];

    if (hostView != nil && [hostView acceptsFirstResponder] && [window makeFirstResponder: hostView])
        return;

    [window makeFirstResponder: nil];
}

} // namespace flowstate::spike
