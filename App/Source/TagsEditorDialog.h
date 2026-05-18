#pragma once
#include <JuceHeader.h>

// Modal dialog for editing the tags of a single ProjectEntry.
//
// UX:
//   1. Top text field: type a tag, press Enter to add it
//   2. "Current tags" row: chips of the entry's existing tags, each with an X
//      to remove
//   3. "Quick add" row: buttons for tags that exist in OTHER entries but not
//      in this one — click to add (alphabetical order)
//   4. OK saves, Cancel discards
class TagsEditorDialog : public juce::Component
{
public:
    // `entryName` is shown in the header. `initialTags` seeds the editor.
    // The dialog calls `onCommit(newTags)` once on OK.
    static void launch (const juce::String& entryName,
                        const juce::StringArray& initialTags,
                        std::function<void (juce::StringArray)> onCommit);

private:
    TagsEditorDialog (const juce::String& entryName,
                      const juce::StringArray& initialTags,
                      std::function<void (juce::StringArray)> onCommit);

    void paint  (juce::Graphics& g) override;
    void resized() override;

    // Internal model
    void addTag    (const juce::String& tag);
    void removeTag (const juce::String& tag);
    void rebuildChips();
    void rebuildSuggestions();

    juce::String                          entryName;
    juce::StringArray                     currentTags;       // tags on this entry
    juce::StringArray                     allKnownTags;      // every tag across the DB
    std::function<void (juce::StringArray)> onCommit;

    juce::Label      header;
    juce::Label      inputCaption    { {}, "Type a tag and press Enter" };
    juce::TextEditor input;

    juce::Label      currentCaption  { {}, "Current tags" };
    juce::Label      currentEmpty    { {}, "(no tags yet)" };

    juce::Label      suggestCaption  { {}, "Quick add" };
    juce::Label      suggestEmpty    { {}, "(no other tags in your library)" };

    // Dynamically built chip / suggestion buttons
    juce::OwnedArray<juce::TextButton> chipButtons;
    juce::OwnedArray<juce::TextButton> suggestionButtons;

    juce::TextButton okBtn     { "OK"     };
    juce::TextButton cancelBtn { "Cancel" };

    // Cached layout rects for the two flow areas (set in resized)
    juce::Rectangle<int> chipsArea;
    juce::Rectangle<int> suggestArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TagsEditorDialog)
};
