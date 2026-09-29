#pragma once

namespace flowstate::plugin
{

/** Hands OS keyboard focus from the plugin's native view back to the host's view, so the next
    key presses (Space for transport) reach the DAW instead of the WebView.

    nativeHandle is ComponentPeer::getNativeHandle() of the editor: an NSView* on macOS, an HWND
    on Windows. Platform code lives in FocusRelease_mac.mm / FocusRelease_windows.cpp and uses
    no JUCE headers. Message thread only. */
void releaseKeyboardFocusToHost (void* nativeHandle);

} // namespace flowstate::plugin
