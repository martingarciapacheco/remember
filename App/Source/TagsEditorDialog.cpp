#include "TagsEditorDialog.h"
#include "ProjectScanner.h"
#include "ProjectListComponent.h"   // for Pal::
#include <unordered_map>
#include <unordered_set>

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
        ga.addLineOfText (RememberUI::font (fontHeight),
                          text, 0.0f, 0.0f);
        // Curtailed shaping compares fractional glyph advances with the
        // available width. Rounding down can ellipsise a fitting short tag.
        return (int) std::ceil (ga.getBoundingBox (0, -1, true).getRight()) + 1;
    }

    class TagButton final : public RememberUI::TextButton
    {
    public:
        TagButton (const juce::String& value, bool canRemove, int width)
            : RememberUI::TextButton ("#" + value + (canRemove ? "  x" : "")), tag (value), removable (canRemove),
              fontHeight (canRemove ? 12.0f : 11.5f),
              preferredWidth (width) {}
        const juce::String tag;
        const bool removable;
        const float fontHeight;
        const int preferredWidth;
    private:
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), over, down);
            g.setColour (findColour (textColourOffId));
            g.setFont (RememberUI::font (fontHeight));
            auto label = getLocalBounds().reduced (8, 0);
            if (removable)
                g.drawText ("x", label.removeFromRight (10), juce::Justification::centred, false);
            // Keep a readable font for long names. Ellipsis affects only the
            // painted label; tooltip, accessibility and callbacks retain ID.
            g.drawText ("#" + tag, label, juce::Justification::centredLeft, true);
        }
    };
}

// ── TagsEditorDialog ─────────────────────────────────────────────────────────

class TagsEditorDialog::TagFlow final : public juce::Component
{
public:
    TagFlow (TagsEditorDialog& dialog, juce::Viewport& view, bool canRemove)
        : owner (dialog), viewport (view), removable (canRemove) {}
    ~TagFlow() override { generation->fetch_add (1); }

    void setTags (const juce::StringArray& tags)
    {
        const auto version = generation->fetch_add (1) + 1;
        auto wanted = std::make_shared<std::vector<Item>>(); wanted->reserve ((size_t) tags.size());
        size_t missing = 0;
        for (const auto& tag : tags)
        {
            const auto key = tag.toStdString();
            const auto found = widths.find (key);
            const auto width = found != widths.end() ? found->second : 0;
            if (width == 0) ++missing;
            wanted->push_back ({ tag, width, {} });
        }
        const auto measure = [wanted, token = generation, version, remove = removable]
        {
            for (auto& item : *wanted)
            {
                if (token->load() != version) return juce::Result::ok();
                if (item.width == 0)
                    item.width = measureTextWidth ("#" + item.tag + (remove ? "  x" : ""), remove ? 12.0f : 11.5f) + (remove ? 22 : 16);
            }
            return juce::Result::ok();
        };
        if (missing <= 128)
        {
            measure(); publish (std::move (*wanted)); return;
        }
        auto& status = removable ? owner.currentEmpty : owner.suggestEmpty;
        status.setText (RememberUI::tr ("Loading tags..."), juce::dontSendNotification); status.setVisible (true);
        ProjectScanner::queryAsync (measure,
            [safe = juce::Component::SafePointer<TagFlow> (this), wanted, version] (juce::Result result)
            {
                if (safe == nullptr || safe->generation->load() != version) return;
                if (result.wasOk()) safe->publish (std::move (*wanted));
                else
                {
                    auto& message = safe->removable ? safe->owner.currentEmpty : safe->owner.suggestEmpty;
                    message.setText (RememberUI::tr ("Tags could not be loaded. Close and reopen this editor."), juce::dontSendNotification);
                    message.setTooltip (RememberUI::diagnostic (result.getErrorMessage())); message.setVisible (true);
                }
            });
    }

    void layout()
    {
        const auto width = juce::jmax (1, viewport.getWidth() - viewport.getScrollBarThickness());
        const auto height = removable ? 26 : 24;
        int x = 0, y = 0;
        for (auto& item : items)
        {
            const auto itemWidth = juce::jmin (width, item.width);
            if (x + itemWidth > width && x > 0) { x = 0; y += height + 6; }
            item.bounds = { x, y, itemWidth, height }; x += itemWidth + 6;
        }
        changingSize = true;
        setSize (width, juce::jmax (viewport.getHeight(), items.empty() ? 0 : y + height));
        changingSize = false;
        refreshVisible (viewport.getViewArea());
    }

    void refreshVisible (const juce::Rectangle<int>& area)
    {
        if (changingSize) return;
        // Row bounds are monotonic: scrolling skips all offscreen controls.
        const auto first = std::lower_bound (items.begin(), items.end(), area.getY(),
            [] (const Item& item, int top) { return item.bounds.getBottom() <= top; });
        std::unordered_multimap<std::string, std::unique_ptr<TagButton>> retained;
        for (auto& button : buttons) retained.emplace (button->tag.toStdString(), std::move (button));
        buttons.clear();
        for (auto item = first; item != items.end() && item->bounds.getY() < area.getBottom(); ++item)
        {
            const auto found = retained.find (item->tag.toStdString());
            std::unique_ptr<TagButton> button;
            if (found != retained.end()) { button = std::move (found->second); retained.erase (found); }
            else
            {
                button = std::make_unique<TagButton> (item->tag, removable, item->width);
                RememberUI::bindColour (*button, RememberUI::TextButton::buttonColourId, removable ? Pal::Accent : Pal::Elevated, removable ? 0.25f : 1.0f);
                RememberUI::bindColour (*button, RememberUI::TextButton::textColourOffId, removable ? Pal::Text : Pal::Subtext);
                button->setWantsKeyboardFocus (false);
                button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
                button->setTooltip (button->getButtonText());
                button->onClick = [safe = juce::Component::SafePointer<TagsEditorDialog> (&owner), tag = item->tag, remove = removable]
                {
                    juce::MessageManager::callAsync ([safe, tag, remove]
                    {
                        if (safe == nullptr) return;
                        if (remove) safe->removeTag (tag); else safe->addTag (tag);
                    });
                };
                addAndMakeVisible (*button);
            }
            button->setBounds (item->bounds); buttons.push_back (std::move (button));
        }
    }

private:
    struct Item { juce::String tag; int width; juce::Rectangle<int> bounds; };
    void publish (std::vector<Item> next)
    {
        widths.clear();
        for (const auto& item : next) widths.emplace (item.tag.toStdString(), item.width);
        items = std::move (next);
        auto& status = removable ? owner.currentEmpty : owner.suggestEmpty;
        status.setText (removable ? RememberUI::tr ("(no tags yet)") : RememberUI::tr ("(no other tags in your library)"), juce::dontSendNotification);
        status.setTooltip ({}); status.setVisible (items.empty());
        layout();
    }
    TagsEditorDialog& owner;
    juce::Viewport& viewport;
    bool removable, changingSize = false;
    std::unordered_map<std::string, int> widths;
    std::vector<Item> items;
    std::vector<std::unique_ptr<TagButton>> buttons;
    std::shared_ptr<std::atomic<uint64_t>> generation { std::make_shared<std::atomic<uint64_t>> (0) };
};

TagsEditorDialog::~TagsEditorDialog() = default;

TagsEditorDialog::TagsEditorDialog (const juce::String& en,
                                    const juce::StringArray& initial,
                                    std::function<void (juce::StringArray)> commit)
    : entryName (en), currentTags (initial), onCommit (std::move (commit))
{
    chipsContent = std::make_unique<TagFlow> (*this, chipsViewport, true);
    suggestionsContent = std::make_unique<TagFlow> (*this, suggestionsViewport, false);
    chipsViewport.setViewedComponent (chipsContent.get(), false);
    suggestionsViewport.setViewedComponent (suggestionsContent.get(), false);
    chipsViewport.onAreaChanged = [this] (const juce::Rectangle<int>& area) { chipsContent->refreshVisible (area); };
    suggestionsViewport.onAreaChanged = [this] (const juce::Rectangle<int>& area) { suggestionsContent->refreshVisible (area); };
    chipsViewport.setScrollBarsShown (true, false);
    suggestionsViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (chipsViewport);
    addAndMakeVisible (suggestionsViewport);
    setSize (560, 460);

    allKnownTags = ProjectScanner::getAllUniqueTags();

    auto styleLabel = [] (RememberUI::Label& l, float h, uint32_t col, bool bold = false)
    {
        l.setFont (RememberUI::font (h, bold));
        RememberUI::bindColour (l, RememberUI::Label::textColourId, col);
    };

    header.setText (RememberUI::tr ("Edit tags - \"") + entryName + "\"", juce::dontSendNotification);
    styleLabel (header, 20.0f, Pal::Text, true);
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

    input.setFont (RememberUI::font (14.0f));
    RememberUI::bindColour (input, RememberUI::TextEditor::backgroundColourId, Pal::Elevated);
    RememberUI::bindColour (input, RememberUI::TextEditor::textColourId, Pal::Text);
    input.setColour (RememberUI::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
    RememberUI::bindColour (input, RememberUI::TextEditor::focusedOutlineColourId, Pal::Accent);
    RememberUI::placeholder (input, RememberUI::tr ("  type tag + Enter..."), Pal::Subtext);
    input.onReturnKey = [this]
    {
        auto t = normaliseTag (input.getText());
        if (t.isNotEmpty()) addTag (t);
        input.clear();
    };
    addAndMakeVisible (input);

    auto styleBtn = [] (RememberUI::TextButton& b, uint32_t bg, uint32_t fg)
    {
        RememberUI::bindColour (b, RememberUI::TextButton::buttonColourId, bg);
        RememberUI::bindColour (b, RememberUI::TextButton::textColourOffId, fg);
    };

    styleBtn (okBtn, Pal::Action,   Pal::OnAction);
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
    resized();
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
    chipsContent->setTags (currentTags);
}

void TagsEditorDialog::rebuildSuggestions()
{
    juce::StringArray remaining;
    std::unordered_set<std::string> current;
    for (const auto& tag : currentTags) current.insert (tag.toUpperCase().toStdString());
    for (auto& t : allKnownTags)
        if (current.find (t.toUpperCase().toStdString()) == current.end()) remaining.add (t);

    suggestionsContent->setTags (remaining);
}

void TagsEditorDialog::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));

    // Header strip
    g.setColour (RememberUI::colour (Pal::Surface));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (RememberUI::colour (Pal::Border));
    g.fillRect (0, 48, getWidth(), 1);

    // Separator between Current and Quick add sections
    if (! suggestArea.isEmpty())
    {
        g.setColour (RememberUI::colour (Pal::Border));
        g.fillRect (suggestArea.getX(), suggestCaption.getY() - 7, suggestArea.getWidth(), 1);
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

    chipsViewport.setBounds (chipsArea);
    suggestionsViewport.setBounds (suggestArea);
    chipsContent->layout();
    suggestionsContent->layout();
}

void TagsEditorDialog::launch (const juce::String& entryName,
                               const juce::StringArray& initialTags,
                               std::function<void (juce::StringArray)> onCommit)
{
    auto dialog = create (entryName, initialTags, std::move (onCommit));

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle               = RememberUI::tr ("Edit tags");
    opts.content.setOwned          (dialog.release());
    opts.componentToCentreAround   = nullptr;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar         = true;
    opts.resizable                 = false;
    opts.dialogBackgroundColour    = RememberUI::colour (Pal::BgDeep);
    RememberUI::launchDialog (opts);
}

std::unique_ptr<TagsEditorDialog> TagsEditorDialog::create (const juce::String& entryName,
                                                          const juce::StringArray& initialTags,
                                                          std::function<void (juce::StringArray)> onCommit)
{
    return std::unique_ptr<TagsEditorDialog> (new TagsEditorDialog (entryName, initialTags, std::move (onCommit)));
}
