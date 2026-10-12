#pragma once
#include "LibraryStore.h"
#include <mutex>
#include <vector>

// Bounded, session-local inverse array deltas. Does not roll back audio/identity
// or overwrite unrelated edits made after the original organization action.
namespace OrganizationUndo
{
struct Edit { juce::File file; juce::String identity; juce::Identifier field; juce::StringArray before, after; };
struct Group { std::vector<Edit> edits; size_t bytes = 0; };
inline std::mutex lock;
inline std::vector<Group> history;
inline size_t retainedBytes = 0;
inline thread_local Group* current = nullptr;
inline thread_local bool replaying = false;
inline void push (Group group)
{
    if (group.edits.empty() || group.bytes > 16 * 1024 * 1024) return;
    std::lock_guard<std::mutex> guard (lock);
    while (! history.empty() && (history.size() >= 32 || retainedBytes + group.bytes > 16 * 1024 * 1024))
    { retainedBytes -= history.front().bytes; history.erase (history.begin()); }
    retainedBytes += group.bytes; history.push_back (std::move (group));
}
struct Batch
{
    Group group; Group* parent = current;
    Batch() { if (! parent) current = &group; }
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
inline bool available() { std::lock_guard<std::mutex> guard (lock); return ! history.empty(); }
inline juce::Result undo()
{
    Group group;
    { std::lock_guard<std::mutex> guard (lock); if (history.empty()) return juce::Result::fail ("No organization changes to undo");
      group = std::move (history.back()); history.pop_back(); retainedBytes -= group.bytes; }
    const juce::ScopedValueSetter<bool> replay (replaying, true); juce::StringArray failures;
    std::vector<Edit> retry;
    for (auto it = group.edits.rbegin(); it != group.edits.rend(); ++it)
    {
        const auto& change = *it;
        const auto result = LibraryStore::updateJson (change.file, [&] (juce::DynamicObject& object)
        {
            if (change.identity.isNotEmpty() && object.getProperty ("projectId").toString() != change.identity)
                return juce::Result::fail ("Recording identity changed; organization was preserved");
            auto latest = strings (object.getProperty (change.field));
            for (const auto& item : change.after) if (! change.before.contains (item)) latest.removeString (item);
            for (const auto& item : change.before) if (! change.after.contains (item)) latest.addIfNotAlreadyThere (item);
            juce::Array<juce::var> values; for (const auto& item : latest) values.add (item);
            object.setProperty (change.field, values); return juce::Result::ok();
        });
        if (result.failed()) { failures.add (result.getErrorMessage()); retry.push_back (change); }
    }
    if (! retry.empty()) { std::reverse (retry.begin(), retry.end()); group.edits = std::move (retry); push (std::move (group)); }
    return failures.isEmpty() ? juce::Result::ok() : juce::Result::fail (failures.joinIntoString ("\n"));
}
}
