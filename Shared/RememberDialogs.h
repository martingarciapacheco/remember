#pragma once
#include "RememberLookAndFeel.h"

namespace RememberUI
{
// A detached alert must own its theme: an associated component only controls
// placement in JUCE and does not make its look-and-feel inherit automatically.
inline void showMessageBox (const juce::MessageBoxOptions& options, std::function<void (int)> callback = {})
{
    class Dialog final : public juce::AlertWindow
    {
    public:
        explicit Dialog (const juce::MessageBoxOptions& opts)
            : AlertWindow (opts.getTitle(), opts.getMessage(), juce::MessageBoxIconType::NoIcon, opts.getAssociatedComponent())
        {
            for (int i = 0; i < opts.getNumButtons(); ++i)
                addButton (opts.getButtonText (i), i == opts.getNumButtons() - 1 ? 0 : i + 1,
                    juce::KeyPress (i == 0 ? juce::KeyPress::returnKey : juce::KeyPress::escapeKey));
        }
    private:
        Style style { *this };
    };
    auto* dialog = new Dialog (options);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([callback = std::move (callback)] (int result)
        { if (callback) callback (result); }), true);
}
}
