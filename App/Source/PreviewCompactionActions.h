#pragma once
#include "ProjectScanner.h"
#include "../../Shared/RememberLookAndFeel.h"

namespace PreviewCompactionActions
{
inline void show (const juce::MessageBoxOptions& options, std::function<void (int)> callback = {})
{
    class Dialog final : public juce::AlertWindow
    {
    public:
        explicit Dialog (const juce::MessageBoxOptions& opts)
            : juce::AlertWindow (opts.getTitle(), opts.getMessage(), juce::MessageBoxIconType::NoIcon,
                                 opts.getAssociatedComponent())
        {
            for (int i = 0; i < opts.getNumButtons(); ++i)
                addButton (opts.getButtonText (i), i == opts.getNumButtons() - 1 ? 0 : i + 1,
                    juce::KeyPress (i == 0 ? juce::KeyPress::returnKey : juce::KeyPress::escapeKey));
        }
    private:
        RememberUI::Style style { *this };
    };
    auto* dialog = new Dialog (options);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([callback = std::move (callback)] (int result)
        { if (callback) callback (result); }), true);
}

struct Summary
{
    int compacted = 0, skipped = 0, failed = 0, incomplete = 0;
    juce::int64 before = 0, after = 0;
    juce::StringArray errors;
    juce::String text() const
    {
        return juce::String (compacted) + " compacted; " + juce::String (skipped) + " skipped; "
            + juce::String (failed + incomplete) + " need attention. "
            + juce::String (std::abs (before - after) / 1000000.0, 2) + (after <= before ? " MB freed" : " MB added");
    }
};
inline bool eligible (const ProjectEntry& entry)
{
    return ! entry.trashed && ! entry.previewLocked && entry.storageError.isEmpty()
        && entry.projectId.isNotEmpty() && (! entry.pending || entry.pendingOwner == LibraryStore::OwnerState::Orphaned);
}
inline void run (const juce::File& root, const juce::Array<ProjectEntry>& entries, int quality,
                 juce::int64 minimumBytes, Summary& summary)
{
    for (const auto& entry : entries)
    {
        if (! eligible (entry)) { ++summary.skipped; continue; }
        const auto folder = entry.metadataFile.getParentDirectory();
        if (LibraryStore::measureEntryAudio (folder) <= minimumBytes) { ++summary.skipped; continue; }
        const auto result = LibraryStore::compactPreview (root, folder, entry.projectId,
            entry.baseline.getProperty ("audioRevision", "").toString(), quality);
        if (result.changed)
        {
            summary.before += result.beforeBytes; summary.after += result.afterBytes;
            if (result.result.wasOk()) ++summary.compacted; else ++summary.incomplete;
        }
        else if (result.result.wasOk()) ++summary.skipped;
        else ++summary.failed;
        if (result.result.failed()) summary.errors.add (entry.name + ": " + result.result.getErrorMessage());
    }
}
inline juce::MessageBoxOptions confirmation (const juce::String& target)
{
    return juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
        .withTitle ("Compact preview audio?")
        .withMessage (target + "\n\nRe-encode at the quality selected in Settings into one OGG per preview. "
            "This adds a lossy encoding generation and removes the old fragments and recovery audio. It cannot be undone. "
            "Choosing a higher quality than the source may increase its size. "
            "A later partial capture converts the compacted tail to fragments once; subsequent captures reuse them.")
        .withButton ("Compact").withButton ("Cancel");
}
}
