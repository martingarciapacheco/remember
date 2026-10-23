#pragma once
#include "../../Shared/RememberLookAndFeel.h"
#include <map>
#include "TagFilterEditor.h"

// Native, scrollable forms keep essential actions visible at small window sizes.
class WorkflowForm : public juce::Component
{
public:
    explicit WorkflowForm (juce::String heading) : title (std::move (heading))
    {
        viewport.setViewedComponent (&body, false); viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport); addAndMakeVisible (save); addAndMakeVisible (cancel); addAndMakeVisible (error);
        error.setFont (RememberUI::font (12)); RememberUI::bindColour (error, RememberUI::Label::textColourId, Pal::Error);
        RememberUI::bindColour (save, RememberUI::TextButton::buttonColourId, Pal::Action);
        RememberUI::bindColour (save, RememberUI::TextButton::textColourOffId, Pal::OnAction);
        save.onClick = [this]
        {
            const auto result = onAccept ? onAccept (*this) : juce::Result::ok();
            error.setText (RememberUI::diagnostic (result.getErrorMessage()), juce::dontSendNotification);
            if (result.wasOk()) close (1);
        };
        cancel.onClick = [this] { close (0); };
        setSize (580, 540);
    }
    RememberUI::TextEditor& text (const juce::String& id, const juce::String& label, const juce::String& value, bool multiline = false)
    {
        auto control = std::make_unique<RememberUI::TextEditor>(); auto& result = *control;
        result.setFont (RememberUI::font()); result.setText (value, false); result.setMultiLine (multiline);
        result.setReturnKeyStartsNewLine (multiline); result.setTitle (label); result.setIndents (8, 6);
        RememberUI::bindColour (result, RememberUI::TextEditor::backgroundColourId, Pal::Content);
        RememberUI::bindColour (result, RememberUI::TextEditor::textColourId, Pal::Text);
        texts[id] = &result; add (label, std::move (control), multiline ? 96 : 34); return result;
    }
    RememberUI::ComboBox& choice (const juce::String& id, const juce::String& label, juce::StringArray values, const juce::String& value)
    {
        auto control = std::make_unique<RememberUI::ComboBox>(); auto& result = *control;
        choiceSources[id] = values;
        for (int i = 0; i < values.size(); ++i) result.addItem (RememberUI::tr (values[i]), i + 1); result.setSelectedId (juce::jmax (1, values.indexOf (value) + 1), juce::dontSendNotification);
        result.setTitle (label); choices[id] = &result; add (label, std::move (control), 34); return result;
    }
    RememberUI::TextEditor& tagText (const juce::String& id, const juce::String& label,
                               const juce::String& value, juce::StringArray tags)
    {
        auto control = std::make_unique<TagFilterEditor> (std::move (tags)); auto& result = *control;
        result.setFont (RememberUI::font()); result.setText (value, false);
        result.setTitle (label); result.setIndents (8, 6);
        RememberUI::bindColour (result, RememberUI::TextEditor::backgroundColourId, Pal::Content);
        RememberUI::bindColour (result, RememberUI::TextEditor::textColourId, Pal::Text);
        texts[id] = &result; add (label, std::move (control), 34); return result;
    }
    RememberUI::ToggleButton& toggle (const juce::String& id, const juce::String& label, bool value)
    {
        auto control = std::make_unique<RememberUI::ToggleButton> (label); auto& result = *control;
        result.setToggleState (value, juce::dontSendNotification); result.setTitle (label);
        toggles[id] = &result; add ({}, std::move (control), 34); return result;
    }
    void information (const juce::String& value)
    {
        auto label = std::make_unique<RememberUI::Label> (juce::String{}, value);
        label->setFont (RememberUI::font (12)); RememberUI::bindColour (*label, RememberUI::Label::textColourId, Pal::Subtext);
        label->setTooltip (value); add ({}, std::move (label), 46);
    }
    void musicalKey (const juce::String& value)
    {
        auto control = std::make_unique<KeyControls> (value);
        keyControls = control.get();
        add (RememberUI::tr ("Manual key"), std::move (control), 36);
    }
    juce::String manualKeyValue() const
    {
        if (keyControls == nullptr || keyControls->automatic.getToggleState()) return {};
        return keyControls->note.getText() + " " + keyControls->scale.getText().toLowerCase();
    }
    juce::String textValue (const juce::String& id) const { return texts.at (id)->getText().trim(); }
    juce::String choiceValue (const juce::String& id) const { return choiceSources.at (id)[choices.at (id)->getSelectedId() - 1]; }
    bool toggleValue (const juce::String& id) const { return toggles.at (id)->getToggleState(); }
    std::function<juce::Result (WorkflowForm&)> onAccept;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (RememberUI::colour (Pal::BgDeep)); g.setColour (RememberUI::colour (Pal::Text));
        g.setFont (RememberUI::font (22, true)); g.drawText (title, 20, 12, getWidth() - 40, 32, juce::Justification::centredLeft);
    }
    void resized() override
    {
        viewport.setBounds (16, 56, getWidth() - 32, juce::jmax (0, getHeight() - 140));
        body.setSize (viewport.getWidth() - 16, contentHeight);
        int y = 0;
        for (auto& row : rows)
        {
            const bool full = row.label->getText().isEmpty();
            row.label->setBounds (0, y, 150, row.height);
            row.control->setBounds (full ? 0 : 154, y + 2, body.getWidth() - (full ? 0 : 154), row.height - 4);
            y += row.height + 8;
        }
        error.setBounds (20, getHeight() - 80, getWidth() - 40, 32);
        save.setBounds (getWidth() - 116, getHeight() - 46, 96, 32);
        cancel.setBounds (getWidth() - 220, getHeight() - 46, 96, 32);
    }
    static void launch (std::unique_ptr<WorkflowForm> form, juce::Component* parent)
    {
        juce::DialogWindow::LaunchOptions options; options.dialogTitle = form->title;
        options.content.setOwned (form.release()); options.componentToCentreAround = parent;
        options.dialogBackgroundColour = RememberUI::colour (Pal::BgDeep);
        options.useNativeTitleBar = true; options.escapeKeyTriggersCloseButton = true;
        options.resizable = true; RememberUI::launchDialog (options);
    }
private:
    struct KeyControls : juce::Component
    {
        explicit KeyControls (const juce::String& value)
        {
            automatic.getProperties().set ("rememberCheckbox", true);
            automatic.setTitle (RememberUI::tr ("Automatic key recognition"));
            automatic.setTooltip (RememberUI::tr ("Recognize key automatically. Uncheck to choose a manual key."));
            automatic.setToggleState (value.isEmpty(), juce::dontSendNotification);
            note.setTitle (RememberUI::tr ("Key note")); scale.setTitle (RememberUI::tr ("Key scale"));
            note.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
            juce::StringArray scales { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
                "Harmonic minor", "Melodic minor", "Major pentatonic", "Minor pentatonic", "Blues", "Whole tone", "Chromatic" };
            auto savedScale = value.fromFirstOccurrenceOf (" ", false, false);
            int scaleIndex = scales.indexOf (savedScale, true);
            if (savedScale.isNotEmpty() && scaleIndex < 0) { scales.add (savedScale); scaleIndex = scales.size() - 1; }
            scale.addItemList (scales, 1);
            note.setText (value.isEmpty() ? "C" : value.upToFirstOccurrenceOf (" ", false, false), juce::dontSendNotification);
            scale.setSelectedId (scaleIndex < 0 ? 2 : scaleIndex + 1, juce::dontSendNotification);
            addAndMakeVisible (automatic); addAndMakeVisible (note); addAndMakeVisible (scale);
            automatic.onClick = [this] { update(); }; update();
        }
        void update() { note.setEnabled (! automatic.getToggleState()); scale.setEnabled (! automatic.getToggleState()); }
        void resized() override
        {
            auto area = getLocalBounds(); automatic.setBounds (area.removeFromLeft (32)); area.removeFromLeft (4);
            note.setBounds (area.removeFromLeft (64)); area.removeFromLeft (8); scale.setBounds (area);
        }
        RememberUI::ToggleButton automatic;
        RememberUI::ComboBox note, scale;
    };
    KeyControls* keyControls = nullptr;
    RememberUI::Style style { *this };
    struct Row { std::unique_ptr<RememberUI::Label> label; std::unique_ptr<juce::Component> control; int height; };
    void add (juce::String caption, std::unique_ptr<juce::Component> control, int height)
    {
        auto label = std::make_unique<RememberUI::Label> (juce::String{}, caption); label->setFont (RememberUI::font());
        body.addAndMakeVisible (*label); body.addAndMakeVisible (*control);
        rows.push_back ({ std::move (label), std::move (control), height }); contentHeight += height + 8; resized();
    }
    void close (int result) { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (result); }
    juce::String title;
    juce::Viewport viewport;
    juce::Component body;
    RememberUI::TextButton save { RememberUI::tr ("Apply") }, cancel { RememberUI::tr ("Cancel") };
    RememberUI::Label error;
    std::vector<Row> rows;
    std::map<juce::String, RememberUI::TextEditor*> texts;
    std::map<juce::String, RememberUI::ComboBox*> choices;
    std::map<juce::String, juce::StringArray> choiceSources;
    std::map<juce::String, RememberUI::ToggleButton*> toggles;
    int contentHeight = 0;
};
