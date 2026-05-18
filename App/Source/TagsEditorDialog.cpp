#include "TagsEditorDialog.h"
#include "ProjectScanner.h"
#include "ProjectListComponent.h"   // for Pal::

namespace
{
    // Normalises a raw user input string into a tag: trims, collapses internal
    // whitespace, drops a leading '#' if present, and lowercases nothing
    // (tags preserve case for display).
    juce::String normaliseTag (const juce::String& raw)
    {
        auto t = raw.trim();
        if (t.startsWithChar ('#')) t = t.substring (1).trim();
        // Collapse internal whitespace runs to single spaces
        juce::String out;
        bool prevSpace = false;
        for (auto c : t)
        {
            if (juce::CharacterFunctions::isWhitespace (c))
            {
                if (! prevSpace && out.isNotEmpty()) out += ' ';
                prevSpace = true;
            }
            else
            {
                out += juce::String::charToString (c);
                prevSpace = false;
            }
        }
        return out.trim();
    }

    // Approximate pixel width for a label of text at a given font. Used for
    // flow-layout sizing of chip / suggestion buttons.
    int measureTextWidth (const juce::String& text, float fontHeight)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions{}.withHeight (fontHeight)),
                          text, 0.0f, 0.0f);
        return juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth());
    }
}

// ── TagsEditorDialog ─────────────────────────────────────────────────────────

TagsEditorDialog::TagsEditorDialog (const juce::String& en,
                                    const juce::StringArray& initial,
                                    std::function<void (juce::StringArray)> commit)
    : entryName (en), currentTags (initial), onCommit (std::move (commit))
{
    setSize (560, 460);

    allKnownTags = ProjectScanner::getAllUniqueTags();

    auto styleLabel = [] (juce::Label& l, float h, uint32_t col, bool bold = false)
    {
        l.setFont (juce::Font (juce::FontOptions{}.withHeight (h)
                               .withStyle (bold ? "Bold" : "Regular")));
        l.setColour (juce::Label::textColourId, juce::Colour (col));
    };

    header.setText ("Edit tags - \"" + entryName + "\"", juce::dontSendNotification);
    styleLabel (header, 16.0f, Pal::Text, true);
    addAndMakeVisible (header);

    styleLabel (inputCaption,   12.0f, Pal::Subtext);
    styleLabel (currentCaption, 12.0f, Pal::Subtext, true);
    styleLabel (suggestCaption, 12.0f, Pal::Subtext, true);
    styleLabel (currentEmpty,   11.0f, Pal::Subtext);
    styleLabel (suggestEmpty,   11.0f, Pal::Subtext);
    addAndMakeVisible (inputCaption);
    addAndMakeVisible (currentCaption);
    addAndMakeVisible (suggestCaption);
    addAndMakeVisible (currentEmpty);
    addAndMakeVisible (suggestEmpty);

    input.setFont (juce::Font (juce::FontOptions{}.withHeight (14.0f)));
    input.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (Pal::Elevated));
    input.setColour (juce::TextEditor::textColourId,           juce::Colour (Pal::Text));
    input.setColour (juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (Pal::Accent));
    input.setTextToShowWhenEmpty ("  type tag + Enter...", juce::Colour (Pal::Subtext));
    input.onReturnKey = [this]
    {
        auto t = normaliseTag (input.getText());
        if (t.isNotEmpty()) addTag (t);
        input.clear();
    };
    addAndMakeVisible (input);

    auto styleBtn = [] (juce::TextButton& b, uint32_t bg, uint32_t fg)
    {
        b.setColour (juce::TextButton::buttonColourId,  juce::Colour (bg));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (fg));
    };

    styleBtn (okBtn,     Pal::Accent,   0xffffffff);
    styleBtn (cancelBtn, Pal::Elevated, Pal::Subtext);

    okBtn.onClick = [this]
    {
        // Commit any text the user typed but didn't press Enter on
        auto t = normaliseTag (input.getText());
        if (t.isNotEmpty()) addTag (t);

        if (onCommit) onCommit (currentTags);
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (1);
    };
    cancelBtn.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };
    addAndMakeVisible (okBtn);
    addAndMakeVisible (cancelBtn);

    rebuildChips();
    rebuildSuggestions();
}

void TagsEditorDialog::addTag (const juce::String& tag)
{
    // Case-insensitive de-dup: keep the existing capitalisation if already there
    for (auto& t : currentTags)
        if (t.equalsIgnoreCase (tag))
            return;

    currentTags.add (tag);
    rebuildChips();
    rebuildSuggestions();
    resized();
}

void TagsEditorDialog::removeTag (const juce::String& tag)
{
    for (int i = currentTags.size(); --i >= 0;)
        if (currentTags[i].equalsIgnoreCase (tag))
            currentTags.remove (i);
    rebuildChips();
    rebuildSuggestions();
    resized();
}

void TagsEditorDialog::rebuildChips()
{
    chipButtons.clear();
    for (auto& t : currentTags)
    {
        auto* b = new juce::TextButton ("#" + t + "  x");
        b->setColour (juce::TextButton::buttonColourId,  juce::Colour (Pal::Accent).withAlpha (0.25f));
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (Pal::Text));
        b->setWantsKeyboardFocus (false);
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        juce::String capture = t;
        b->onClick = [this, capture] { removeTag (capture); };
        addAndMakeVisible (b);
        chipButtons.add (b);
    }
    currentEmpty.setVisible (chipButtons.isEmpty());
}

void TagsEditorDialog::rebuildSuggestions()
{
    suggestionButtons.clear();
    juce::StringArray remaining;
    for (auto& t : allKnownTags)
    {
        bool already = false;
        for (auto& c : currentTags)
            if (c.equalsIgnoreCase (t)) { already = true; break; }
        if (! already) remaining.add (t);
    }

    for (auto& t : remaining)
    {
        auto* b = new juce::TextButton ("#" + t);
        b->setColour (juce::TextButton::buttonColourId,  juce::Colour (Pal::Elevated));
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (Pal::Subtext));
        b->setWantsKeyboardFocus (false);
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        juce::String capture = t;
        b->onClick = [this, capture] { addTag (capture); };
        addAndMakeVisible (b);
        suggestionButtons.add (b);
    }
    suggestEmpty.setVisible (suggestionButtons.isEmpty());
}

void TagsEditorDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Pal::BgDeep));

    // Header strip
    g.setColour (juce::Colour (Pal::Surface));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (juce::Colour (Pal::Border));
    g.fillRect (0, 48, getWidth(), 1);

    // Separator between Current and Quick add sections
    if (! suggestArea.isEmpty())
    {
        g.setColour (juce::Colour (Pal::Border));
        g.fillRect (suggestArea.getX(), suggestArea.getY() - 14, suggestArea.getWidth(), 1);
    }
}

void TagsEditorDialog::resized()
{
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop (48).reduced (16, 0);
    header.setBounds (headerArea);

    area.removeFromTop (12);
    area = area.reduced (16, 0);

    // Input section
    inputCaption.setBounds (area.removeFromTop (18));
    input       .setBounds (area.removeFromTop (28));

    area.removeFromTop (16);

    // Reserve bottom for OK/Cancel
    auto bottom = area.removeFromBottom (44).reduced (0, 8);
    okBtn    .setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (8);
    cancelBtn.setBounds (bottom.removeFromRight (90));

    area.removeFromBottom (8);

    // Split remaining vertical space ~ 45% chips / 55% suggestions
    auto totalH = area.getHeight();
    auto chipsBlockH    = juce::jmax (60, (int) (totalH * 0.45f));
    auto suggestBlockH  = totalH - chipsBlockH - 30;   // 30 = both captions + spacing

    auto chipsBlock = area.removeFromTop (chipsBlockH);
    currentCaption.setBounds (chipsBlock.removeFromTop (18));
    chipsBlock.removeFromTop (4);
    chipsArea = chipsBlock;
    currentEmpty.setBounds (chipsArea.withHeight (20));

    area.removeFromTop (14);

    auto suggestBlock = area.removeFromTop (suggestBlockH + 20);
    suggestCaption.setBounds (suggestBlock.removeFromTop (18));
    suggestBlock.removeFromTop (4);
    suggestArea = suggestBlock;
    suggestEmpty.setBounds (suggestArea.withHeight (20));

    // ── Flow-layout the chip buttons inside chipsArea ─────────────────────
    auto flowLayout = [] (juce::OwnedArray<juce::TextButton>& btns,
                          juce::Rectangle<int> bounds, int chipH, float fontH,
                          int extraPad)
    {
        if (btns.isEmpty()) return;

        int x = bounds.getX();
        int y = bounds.getY();
        const int gap = 6;

        for (auto* b : btns)
        {
            int textW = measureTextWidth (b->getButtonText(), fontH);
            int w = textW + extraPad;
            if (x + w > bounds.getRight() && x > bounds.getX())
            {
                x = bounds.getX();
                y += chipH + gap;
            }
            b->setBounds (x, y, w, chipH);
            x += w + gap;
        }
    };

    // chips: bigger pad (room for the X)
    flowLayout (chipButtons,       chipsArea,   26, 12.0f, 22);
    flowLayout (suggestionButtons, suggestArea, 24, 11.5f, 16);
}

void TagsEditorDialog::launch (const juce::String& entryName,
                               const juce::StringArray& initialTags,
                               std::function<void (juce::StringArray)> onCommit)
{
    auto* dialog = new TagsEditorDialog (entryName, initialTags, std::move (onCommit));

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle               = "Edit tags";
    opts.content.setOwned          (dialog);
    opts.componentToCentreAround   = nullptr;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar         = true;
    opts.resizable                 = false;
    opts.dialogBackgroundColour    = juce::Colour (Pal::BgDeep);
    opts.launchAsync();
}
