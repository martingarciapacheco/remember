#include "RecoveryChoiceDialog.h"

std::unique_ptr<RecoveryChoiceDialog> RecoveryChoiceDialog::create (juce::Array<LibraryStore::RecoveryChoice> choices,
                                                                    Action preview, Action restore, bool keep)
{
    return std::unique_ptr<RecoveryChoiceDialog> (new RecoveryChoiceDialog (std::move (choices), std::move (preview), std::move (restore), keep));
}
RecoveryChoiceDialog::RecoveryChoiceDialog (juce::Array<LibraryStore::RecoveryChoice> options, Action previewCallback,
                                              Action restoreCallback, bool keep)
    : choices (std::move (options)), previewAction (std::move (previewCallback)), restoreAction (std::move (restoreCallback)),
      restore (keep ? "Restore and keep" : "Restore selected")
{
    setSize (620, 330);
    explanation.setFont (RememberUI::font (14.0f));
    details.setFont (RememberUI::font (13.0f));
    explanation.setText ("Several complete revisions survived. Choose one and listen before restoring it. Cancel keeps every file."
                          "\nRestoring rechecks the files and the DAW owner; a changed recording requires a new review.", juce::dontSendNotification);
    RememberUI::bindColour (explanation, juce::Label::textColourId, Pal::Text);
    RememberUI::bindColour (details, juce::Label::textColourId, Pal::Subtext);
    for (int i = 0; i < choices.size(); ++i)
    {
        const auto& choice = choices.getReference (i);
        revisions.addItem ((choice.recordedAt.isEmpty() ? "Date unavailable" : choice.recordedAt)
            + "  |  " + juce::String (choice.durationSeconds, 2) + " s  |  " + choice.revision.substring (0, 12), i + 1);
    }
    revisions.setTextWhenNothingSelected ("Choose a complete revision...");
    revisions.setName ("Recovery revision");
    revisions.onChange = [this] { updateSelection(); };
    preview.onClick = [this]
    {
        const int index = revisions.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (index, choices.size()) && previewAction) previewAction (choices[index]);
    };
    restore.onClick = [this]
    {
        const int index = revisions.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (index, choices.size())) return;
        const auto choice = choices[index];
        auto action = restoreAction;
        close();
        if (action) action (choice);
    };
    cancel.onClick = [this] { close(); };
    RememberUI::bindColour (restore, juce::TextButton::buttonColourId, Pal::Action);
    for (auto* component : std::initializer_list<juce::Component*> { &explanation, &details, &revisions, &preview, &restore, &cancel })
        addAndMakeVisible (*component);
    restore.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    updateSelection();
    resized();
}
void RecoveryChoiceDialog::updateSelection()
{
    const int index = revisions.getSelectedId() - 1;
    const bool selected = juce::isPositiveAndBelow (index, choices.size());
    restore.setEnabled (selected && (bool) restoreAction);
    preview.setEnabled (selected && (bool) previewAction);
    if (! selected) { details.setText ("No revision selected. Files have not been changed.", juce::dontSendNotification); return; }
    const auto& choice = choices.getReference (index);
    const auto text = "Revision: " + choice.revision + "\nRecorded: " + choice.recordedAt
        + "\nAudio: " + juce::String (choice.durationSeconds, 2) + " s, " + juce::String (choice.channels) + " channel(s)"
        + "\nSaved analysis: " + (choice.bpm > 0 ? juce::String (choice.bpm, 1) + " BPM" : "BPM unavailable")
        + ", " + (choice.key.isEmpty() ? "key unavailable" : choice.key);
    details.setText (text, juce::dontSendNotification);
    details.setTooltip (text + "\n" + choice.previewOgg.getFullPathName());
}
void RecoveryChoiceDialog::close()
{
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) dialog->exitModalState (0);
}
void RecoveryChoiceDialog::paint (juce::Graphics& g) { g.fillAll (RememberUI::colour (Pal::BgDeep)); }
void RecoveryChoiceDialog::resized()
{
    auto bounds = getLocalBounds().reduced (18);
    auto footer = bounds.removeFromBottom (30);
    restore.setBounds (footer.removeFromRight (156)); footer.removeFromRight (8);
    cancel.setBounds (footer.removeFromRight (90));
    preview.setBounds (footer.removeFromLeft (158));
    explanation.setBounds (bounds.removeFromTop (80)); bounds.removeFromTop (8);
    revisions.setBounds (bounds.removeFromTop (30)); bounds.removeFromTop (10);
    details.setBounds (bounds);
}
