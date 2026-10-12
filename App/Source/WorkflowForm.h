#pragma once
#include "../../Shared/RememberLookAndFeel.h"
#include <map>

// Native, scrollable forms keep essential actions visible at small window sizes.
class WorkflowForm : public juce::Component
{
public:
    explicit WorkflowForm (juce::String heading) : title (std::move (heading))
    {
        viewport.setViewedComponent (&body, false); viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport); addAndMakeVisible (save); addAndMakeVisible (cancel); addAndMakeVisible (error);
        error.setFont (RememberUI::font (12)); RememberUI::bindColour (error, juce::Label::textColourId, Pal::Error);
        RememberUI::bindColour (save, juce::TextButton::buttonColourId, Pal::Action);
        RememberUI::bindColour (save, juce::TextButton::textColourOffId, Pal::OnAction);
        save.onClick = [this]
        {
            const auto result = onAccept ? onAccept (*this) : juce::Result::ok();
            error.setText (result.getErrorMessage(), juce::dontSendNotification);
            if (result.wasOk()) close (1);
        };
        cancel.onClick = [this] { close (0); };
        setSize (580, 540);
    }
    juce::TextEditor& text (const juce::String& id, const juce::String& label, const juce::String& value, bool multiline = false)
    {
        auto control = std::make_unique<juce::TextEditor>(); auto& result = *control;
        result.setFont (RememberUI::font()); result.setText (value, false); result.setMultiLine (multiline);
        result.setReturnKeyStartsNewLine (multiline); result.setTitle (label); result.setIndents (8, 6);
        RememberUI::bindColour (result, juce::TextEditor::backgroundColourId, Pal::Content);
        RememberUI::bindColour (result, juce::TextEditor::textColourId, Pal::Text);
        texts[id] = &result; add (label, std::move (control), multiline ? 96 : 34); return result;
    }
    juce::ComboBox& choice (const juce::String& id, const juce::String& label, juce::StringArray values, const juce::String& value)
    {
        auto control = std::make_unique<juce::ComboBox>(); auto& result = *control;
        result.addItemList (values, 1); result.setSelectedId (juce::jmax (1, values.indexOf (value) + 1), juce::dontSendNotification);
        result.setTitle (label); choices[id] = &result; add (label, std::move (control), 34); return result;
    }
    juce::ToggleButton& toggle (const juce::String& id, const juce::String& label, bool value)
    {
        auto control = std::make_unique<juce::ToggleButton> (label); auto& result = *control;
        result.setToggleState (value, juce::dontSendNotification); result.setTitle (label);
        toggles[id] = &result; add ({}, std::move (control), 34); return result;
    }
    void information (const juce::String& value)
    {
        auto label = std::make_unique<juce::Label> (juce::String{}, value);
        label->setFont (RememberUI::font (12)); RememberUI::bindColour (*label, juce::Label::textColourId, Pal::Subtext);
        label->setTooltip (value); add ({}, std::move (label), 46);
    }
    juce::String textValue (const juce::String& id) const { return texts.at (id)->getText().trim(); }
    juce::String choiceValue (const juce::String& id) const { return choices.at (id)->getText(); }
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
        options.resizable = true; options.launchAsync();
    }
private:
    RememberUI::Style style { *this };
    struct Row { std::unique_ptr<juce::Label> label; std::unique_ptr<juce::Component> control; int height; };
    void add (juce::String caption, std::unique_ptr<juce::Component> control, int height)
    {
        auto label = std::make_unique<juce::Label> (juce::String{}, caption); label->setFont (RememberUI::font());
        body.addAndMakeVisible (*label); body.addAndMakeVisible (*control);
        rows.push_back ({ std::move (label), std::move (control), height }); contentHeight += height + 8; resized();
    }
    void close (int result) { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (result); }
    juce::String title;
    juce::Viewport viewport;
    juce::Component body;
    juce::TextButton save { "Apply" }, cancel { "Cancel" };
    juce::Label error;
    std::vector<Row> rows;
    std::map<juce::String, juce::TextEditor*> texts;
    std::map<juce::String, juce::ComboBox*> choices;
    std::map<juce::String, juce::ToggleButton*> toggles;
    int contentHeight = 0;
};
