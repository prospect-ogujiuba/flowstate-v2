#include "FocusRelease.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace flowstate::spike
{

void releaseKeyboardFocusToHost (void* nativeHandle)
{
    auto hwnd = static_cast<HWND> (nativeHandle);

    if (hwnd == nullptr)
        return;

    // WebView2 swallows keys it doesn't handle, so give Win32 focus to the host's window that
    // contains our editor. The host then sees subsequent key presses (e.g. Space).
    if (auto parent = ::GetParent (hwnd))
        ::SetFocus (parent);
    else if (auto root = ::GetAncestor (hwnd, GA_ROOTOWNER))
        ::SetFocus (root);
}

} // namespace flowstate::spike
