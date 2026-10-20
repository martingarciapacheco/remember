#pragma once
#include "LibraryStore.h"
#include "OrganizationUndoContext.h"
#include <algorithm>
#include <iterator>
#include <mutex>
#include <vector>

// Bounded, session-local inverse array deltas. Does not roll back audio/identity
// or overwrite unrelated edits made after the original organization action.
namespace OrganizationUndo
{
struct Edit { juce::File file; juce::String identity; juce::Identifier field; juce::StringArray before, after; };
struct Group
{
    std::vector<Edit> edits;
    std::vector<LibraryStore::TrashReceipt> deletions;
    size_t bytes = 0;
    Context context = Context::Previews;
};
inline std::mutex lock;
inline std::vector<Group> history;
inline size_t retainedBytes = 0;
inline thread_local Group* current = nullptr;
inline thread_local bool replaying = false;
inline void push (Group group)
{
    if ((group.edits.empty() && group.deletions.empty()) || group.bytes > 16 * 1024 * 1024) return;
    std::lock_guard<std::mutex> guard (lock);
    // Each context has its own budget: editing previews cannot evict folder undo.
    size_t count = 0, bytes = 0;
    for (const auto& item : history) if (item.context == group.context) { ++count; bytes += item.bytes; }
    while (count >= 32 || bytes + group.bytes > 16 * 1024 * 1024)
    {
        const auto first = std::find_if (history.begin(), history.end(), [&] (const auto& item) { return item.context == group.context; });
        bytes -= first->bytes; --count; retainedBytes -= first->bytes; history.erase (first);
    }
    retainedBytes += group.bytes; history.push_back (std::move (group));
}
struct Batch
{
    Group group; Group* parent = current;
    explicit Batch (Context context = Context::Any)
    {
        if (! parent) current = &group;
        if (context != Context::Any) current->context = context;
    }
    ~Batch() { if (! parent) { current = nullptr; push (std::move (group)); } }
};
inline juce::StringArray strings (const juce::var& v)
{ juce::StringArray result; if (auto* array = v.getArray()) for (const auto& item : *array) result.add (item.toString()); return result; }
inline void record (const juce::File& file, const juce::String& identity, juce::Identifier field, const juce::var& before, const juce::var& after)
{
    if (replaying) return;
    auto old = strings (before), next = strings (after); if (old == next) return;
    Group singleton; auto& group = current ? *current : singleton;
    group.bytes += sizeof (Edit) + (size_t) old.joinIntoString ("\n").getNumBytesAsUTF8() + (size_t) next.joinIntoString ("\n").getNumBytesAsUTF8();
    group.edits.push_back ({ file, identity, field, std::move (old), std::move (next) });
    if (! current) push (std::move (singleton));
}
inline void recordDeletion (LibraryStore::TrashReceipt receipt)
{
    if (replaying) return;
    Group singleton; auto& group = current ? *current : singleton;
    group.bytes += sizeof (receipt) + (size_t) (receipt.root.getFullPathName() + receipt.folder.getFullPathName()
                                               + receipt.identity + receipt.deletionId).getNumBytesAsUTF8();
    group.deletions.push_back (std::move (receipt));
    if (! current) push (std::move (singleton));
}
inline bool available (Context context = Context::Any)
{
    std::lock_guard<std::mutex> guard (lock);
    return std::any_of (history.begin(), history.end(), [context] (const auto& group)
                       { return context == Context::Any || context == group.context; });
}
inline juce::Result undo (Context context = Context::Any, const juce::File& activeRoot = {})
{
    Group group;
    {
        std::lock_guard<std::mutex> guard (lock);
        const auto item = std::find_if (history.rbegin(), history.rend(), [context] (const auto& candidate)
                                       { return context == Context::Any || context == candidate.context; });
        if (item == history.rend()) return context == Context::Any ? juce::Result::fail ("No organization changes to undo") : juce::Result::ok();
        group = std::move (*item); history.erase (std::next (item).base()); retainedBytes -= group.bytes;
    }
    const juce::ScopedValueSetter<bool> replay (replaying, true); juce::StringArray failures;
    std::vector<LibraryStore::TrashReceipt> retryDeletions;
    for (const auto& deletion : group.deletions)
    {
        if (activeRoot != juce::File{} && activeRoot != deletion.root)
        {
            failures.add ("The deleted preview belongs to a different library"); retryDeletions.push_back (deletion); continue;
        }
        if (! deletion.root.isDirectory())
        {
            failures.add ("The preview library is unavailable"); retryDeletions.push_back (deletion); continue;
        }
        if (! deletion.folder.exists()) continue;
        const auto marker = LibraryStore::readJson (deletion.root.getChildFile (".deleted").getChildFile (deletion.folder.getFileName() + ".json"));
        if (marker.getDynamicObject() == nullptr)
        {
            failures.add ("Cannot read the preview deletion record"); retryDeletions.push_back (deletion); continue;
        }
        // Manual restore, expiry or a later deletion retires this receipt.
        // Never let the old command restore the new deletion at the same path.
        if (marker.getProperty ("deletionId", "").toString() != deletion.deletionId) continue;
        const auto result = LibraryStore::restoreEntry (deletion.root, deletion.folder, deletion.identity, deletion.deletionId);
        if (result.failed()) { failures.add (result.getErrorMessage()); retryDeletions.push_back (deletion); }
    }
    std::vector<Edit> retry;
    for (auto it = group.edits.rbegin(); it != group.edits.rend(); ++it)
    {
        const auto& change = *it;
        const auto result = LibraryStore::updateJson (change.file, [&] (juce::DynamicObject& object)
        {
            if (change.identity.isNotEmpty() && object.getProperty ("projectId").toString() != change.identity)
                return juce::Result::fail ("Recording identity changed; organization was preserved");
            auto latest = strings (object.getProperty (change.field));
            // An unchanged array can restore its exact sibling order. If
            // another writer edited it afterwards, retain the inverse deltas.
            if (latest == change.after) latest = change.before;
            else
            {
                for (const auto& item : change.after) if (! change.before.contains (item)) latest.removeString (item);
                for (const auto& item : change.before) if (! change.after.contains (item)) latest.addIfNotAlreadyThere (item);
            }
            juce::Array<juce::var> values; for (const auto& item : latest) values.add (item);
            object.setProperty (change.field, values); return juce::Result::ok();
        });
        if (result.failed()) { failures.add (result.getErrorMessage()); retry.push_back (change); }
    }
    if (! retry.empty() || ! retryDeletions.empty())
    {
        std::reverse (retry.begin(), retry.end()); group.edits = std::move (retry);
        group.deletions = std::move (retryDeletions); push (std::move (group));
    }
    return failures.isEmpty() ? juce::Result::ok() : juce::Result::fail (failures.joinIntoString ("\n"));
}
}
