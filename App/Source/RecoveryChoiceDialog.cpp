#include "RecoveryChoiceDialog.h"

std::unique_ptr<RecoveryChoiceDialog> RecoveryChoiceDialog::create (juce::Array<LibraryStore::RecoveryChoice> choices,
                                                                    Action preview, Action restore, bool keep)
{
    return std::unique_ptr<RecoveryChoiceDialog> (new RecoveryChoiceDialog (std::move (choices), std::move (preview), std::move (restore), keep));
}
RecoveryChoiceDialog::RecoveryChoiceDialog (juce::Array<LibraryStore::RecoveryChoice> options, Action previewCallback,
                                              Action restoreCallback, bool keep)
    : choices (std::move (options)), previewAction (std::move (previewCallback)), restoreAction (std::move (restoreCallback)),
      restore (keep ? RememberUI::tr ("Restore and keep") : RememberUI::tr ("Restore selected"))
{
    setSize (620, 330);
    explanation.setFont (RememberUI::font (14.0f));
    details.setFont (RememberUI::font (13.0f));
    explanation.setText (RememberUI::tr ("Several complete revisions survived. Choose one and listen before restoring it. Cancel keeps every file.") +
        RememberUI::tr ("\nRestoring rechecks the files and the DAW owner; a changed recording requires a new review."), juce::dontSendNotification);
    RememberUI::bindColour (explanation, RememberUI::Label::textColourId, Pal::Text);
    RememberUI::bindColour (details, RememberUI::Label::textColourId, Pal::Subtext);
    for (int i = 0; i < choices.size(); ++i)
    {
        const auto& choice = choices.getReference (i);
        revisions.addItem ((choice.recordedAt.isEmpty() ? RememberUI::tr ("Date unavailable") : choice.recordedAt)
            + "  |  " + juce::String (choice.durationSeconds, 2) + " s  |  " + choice.revision.substring (0, 12), i + 1);
    }
    revisions.setTextWhenNothingSelected (RememberUI::tr ("Choose a complete revision..."));
    revisions.setName (RememberUI::tr ("Recovery revision"));
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
    RememberUI::bindColour (restore, RememberUI::TextButton::buttonColourId, Pal::Action);
    for (auto* component : std::initializer_list<juce::Component*> { &explanation, &details, &revisions, &preview, &restore, &cancel })
        addAndMakeVisible (*component);
    restore.setColour (RememberUI::TextButton::textColourOffId, juce::Colours::white);
    updateSelection();
    resized();
}
void RecoveryChoiceDialog::updateSelection()
{
    const int index = revisions.getSelectedId() - 1;
    const bool selected = juce::isPositiveAndBelow (index, choices.size());
    restore.setEnabled (selected && (bool) restoreAction);
    preview.setEnabled (selected && (bool) previewAction);
    if (! selected) { details.setText (RememberUI::tr ("No revision selected. Files have not been changed."), juce::dontSendNotification); return; }
    const auto& choice = choices.getReference (index);
    const auto text = RememberUI::tr ("Revision: ") + choice.revision + RememberUI::tr ("\nRecorded: ") + choice.recordedAt
        + RememberUI::tr ("\nAudio: ") + juce::String (choice.durationSeconds, 2) + " s, " + juce::String (choice.channels) + RememberUI::tr (" channel(s)")
        + RememberUI::tr ("\nSaved analysis: ") + (choice.bpm > 0 ? juce::String (choice.bpm, 1) + RememberUI::tr (" BPM") : RememberUI::tr ("BPM unavailable"))
        + ", " + (choice.key.isEmpty() ? RememberUI::tr ("key unavailable") : choice.key);
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
