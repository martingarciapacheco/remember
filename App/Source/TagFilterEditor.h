#pragma once
#include "../../Shared/RememberLookAndFeel.h"

// One recycled list paints every suggestion; typing keeps focus in the editor.
class TagFilterEditor final : public juce::TextEditor,
                              private juce::ListBoxModel,
                              private juce::FocusChangeListener
{
public:
    explicit TagFilterEditor (juce::StringArray tags) : known (std::move (tags))
    {
        known.removeDuplicates (true); known.sort (true);
        suggestions.setModel (this); suggestions.setRowHeight (32);
        suggestions.setTitle ("Existing tags");
        RememberUI::bindColour (suggestions, juce::ListBox::backgroundColourId, Pal::Content);
        suggestions.setOutlineThickness (0);
        suggestions.getProperties().set ("rememberListRadius", RememberUI::surfaceRadius);
        suggestions.getProperties().set ("rememberListOutline", true);
        RememberUI::bindColour (suggestions, juce::ListBox::outlineColourId, Pal::Border);
        onTextChange = [this] { showSuggestions(); };
        juce::Desktop::getInstance().addFocusChangeListener (this);
    }
    ~TagFilterEditor() override
    {
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        suggestions.setModel (nullptr);
    }
    void showSuggestions()
    {
        auto* parent = getParentComponent(); if (! parent) return;
        if (suggestions.getParentComponent() != parent) parent->addChildComponent (suggestions);
        matches.clear();
        const auto value = getText();
        const auto query = value.fromLastOccurrenceOf (",", false, false).trim();
        auto selected = juce::StringArray::fromTokens (value.substring (0, juce::jmax (0, value.lastIndexOfChar (','))), ",", {});
        selected.trim(); selected.removeEmptyStrings();
        for (const auto& tag : known)
            if (tag.containsIgnoreCase (query) && ! selected.contains (tag, true)) matches.add (tag);
        suggestions.updateContent(); suggestions.deselectAllRows();
        auto visible = parent->getLocalBounds();
        if (auto* formViewport = findParentComponentOfClass<juce::Viewport>())
            visible = formViewport->getViewArea();
        const int wanted = juce::jmin (160, juce::jmax (32, matches.size() * 32)) + 2;
        const int below = juce::jmax (0, visible.getBottom() - getBottom() - 4);
        const int above = juce::jmax (0, getY() - visible.getY() - 4);
        const bool openBelow = below >= wanted || below >= above;
        const int height = juce::jmin (wanted, openBelow ? below : above);
        if (height < 32) { suggestions.setVisible (false); return; }
        suggestions.setBounds (getX(), openBelow ? getBottom() + 4 : getY() - height - 4, getWidth(), height);
        suggestions.setVisible (true); suggestions.toFront (false); suggestions.repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override { juce::TextEditor::mouseDown (e); showSuggestions(); }
    void focusGained (FocusChangeType type) override { juce::TextEditor::focusGained (type); showSuggestions(); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && suggestions.isVisible()) { suggestions.setVisible (false); return true; }
        if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
        {
            if (! suggestions.isVisible()) showSuggestions();
            const int delta = key == juce::KeyPress::downKey ? 1 : -1;
            suggestions.selectRow (juce::jlimit (0, juce::jmax (0, matches.size() - 1), suggestions.getSelectedRow() + delta));
            return true;
        }
        if (key == juce::KeyPress::returnKey && suggestions.isVisible() && suggestions.getSelectedRow() >= 0)
        { choose (suggestions.getSelectedRow()); return true; }
        return juce::TextEditor::keyPressed (key);
    }
private:
    void globalFocusChanged (juce::Component* focused) override
    {
        if (focused != this && ! isParentOf (focused) && focused != &suggestions && ! suggestions.isParentOf (focused))
            suggestions.setVisible (false);
    }
    int getNumRows() override { return juce::jmax (1, matches.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected) { g.setColour (RememberUI::colour (Pal::Selected)); g.fillRect (0, 0, w, h); }
        g.setFont (RememberUI::font()); g.setColour (RememberUI::colour (matches.isEmpty() ? Pal::Subtext : Pal::Text));
        g.drawText (matches.isEmpty() ? (known.isEmpty() ? "No tags in your library yet" : "No matching tags") : matches[row],
                    8, 0, w - 16, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { choose (row); }
    void returnKeyPressed (int row) override { choose (row); }
    void choose (int row)
    {
        if (! juce::isPositiveAndBelow (row, matches.size())) return;
        const auto tag = matches[row];
        auto selected = juce::StringArray::fromTokens (getText().substring (0, juce::jmax (0, getText().lastIndexOfChar (','))), ",", {});
        selected.trim(); selected.removeEmptyStrings(); selected.addIfNotAlreadyThere (tag, true);
        setText (selected.joinIntoString (", ") + ", ", false);
        setCaretPosition (getTotalNumChars()); grabKeyboardFocus(); showSuggestions();
    }
    juce::StringArray known, matches;
    RememberUI::RecordingListBox suggestions;
};
