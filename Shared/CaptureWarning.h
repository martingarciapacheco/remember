#pragma once
#include "RememberDialogs.h"

namespace RememberUI
{
inline void showCaptureFromAnyPositionWarning (juce::Component* parent)
{
    showMessageBox (
        juce::MessageBoxOptions().withAssociatedComponent (parent).withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle ("Capture from any position")
            .withMessage ("Starting a recording from any position, including after seeks or loops, may cause small audio artifacts or capture sound tails incorrectly.")
            .withButton ("OK"), [] (int) {});
}
}
