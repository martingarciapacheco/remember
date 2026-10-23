#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "../../Shared/LibraryStore.h"

class RecoveryChoiceDialog final : public juce::Component
{
public:
    using Action = std::function<void (LibraryStore::RecoveryChoice)>;
    static std::unique_ptr<RecoveryChoiceDialog> create (juce::Array<LibraryStore::RecoveryChoice>,
                                                        Action preview, Action restore, bool keep);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    RememberUI::Style uiStyle { *this };
    RecoveryChoiceDialog (juce::Array<LibraryStore::RecoveryChoice>, Action, Action, bool);
    void updateSelection();
    void close();
    juce::Array<LibraryStore::RecoveryChoice> choices;
    Action previewAction, restoreAction;
    RememberUI::Label explanation, details;
    RememberUI::ComboBox revisions;
    RememberUI::TextButton preview { RememberUI::tr ("Listen to selected") }, restore, cancel { RememberUI::tr ("Cancel") };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecoveryChoiceDialog)
};
