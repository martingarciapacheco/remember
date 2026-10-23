#pragma once
#include "RememberLookAndFeel.h"
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace RememberUI
{
// The tray popup has no parent window. Give its native window the same contour
// as the shared surface, so the compositor cannot expose rectangular corners.
class TrayMenuLookAndFeel final : public LookAndFeel
{
public:
    void preparePopupMenuWindow (juce::Component& window) override
    {
        LookAndFeel::preparePopupMenuWindow (window);
       #if JUCE_WINDOWS
        if (auto* peer = window.getPeer())
        {
            const auto hwnd = static_cast<HWND> (peer->getNativeHandle());
            RECT bounds {};
            if (GetWindowRect (hwnd, &bounds) != FALSE && window.getWidth() > 0)
            {
                const auto width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
                const auto diameter = juce::roundToInt (2.0f * surfaceRadius * (float) width / (float) window.getWidth());
                if (const auto region = CreateRoundRectRgn (0, 0, width + 1, height + 1, diameter, diameter))
                    if (SetWindowRgn (hwnd, region, TRUE) == 0) DeleteObject (region);
                // On success Windows owns the region, including its destruction.
            }
        }
       #endif
    }
};
}
