#pragma once
#include "ProjectScanner.h"
#include <algorithm>
#include <random>
#include <vector>

// UI-thread model: shuffle draws from a bag, Previous walks listening history.
class PlaybackQueue
{
public:
    enum Repeat { Off, All, One };
    void setEntries (const juce::Array<ProjectEntry>& value)
    {
        std::vector<juce::String> next;
        for (const auto& e : value) if (e.storageError.isEmpty() && ! e.trashed) next.push_back (id (e));
        if (next != ids) { ids = std::move (next); rebuildBag(); }
        entries = value;
    }
    void select (const ProjectEntry& e)
    {
        const auto target = id (e);
        if (target != current)
        {
            if (cursor + 1 < (int) history.size()) history.resize ((size_t) cursor + 1);
            history.push_back (target); cursor = (int) history.size() - 1;
        }
        current = target;
        bag.erase (std::remove (bag.begin(), bag.end(), current), bag.end());
    }
    std::unique_ptr<ProjectEntry> next (bool automatic = false)
    {
        if (automatic && repeat == One) return find (current);
        while (cursor + 1 < (int) history.size())
        {
            const auto target = history[(size_t) ++cursor];
            if (auto entry = find (target))
            { current = target; bag.erase (std::remove (bag.begin(), bag.end(), current), bag.end()); return entry; }
        }
        if (ids.empty()) return {};
        juce::String target;
        if (shuffle)
        {
            if (bag.empty()) { if (repeat != All) return {}; rebuildBag(); }
            if (bag.empty()) return repeat == All ? find (current) : nullptr;
            target = bag.back(); bag.pop_back();
        }
        else
        {
            const auto at = std::find (ids.begin(), ids.end(), current);
            if (at == ids.end()) target = ids.front();
            else if (at + 1 != ids.end()) target = *(at + 1);
            else if (repeat == All) target = ids.front();
            else return {};
        }
        auto entry = find (target); if (entry) select (*entry); return entry;
    }
    std::unique_ptr<ProjectEntry> previous()
    {
        while (cursor > 0)
            if (auto entry = find (history[(size_t) --cursor])) { current = id (*entry); return entry; }
        return find (current);
    }
    void setShuffle (bool value) { shuffle = value; rebuildBag(); }
    static juce::String id (const ProjectEntry& e) { return e.projectId.isEmpty() ? e.metadataFile.getFullPathName() : e.projectId; }
    bool shuffle = false;
    Repeat repeat = Off;
private:
    std::unique_ptr<ProjectEntry> find (const juce::String& target) const
    { for (const auto& e : entries) if (id (e) == target && ! e.trashed && e.storageError.isEmpty()) return std::make_unique<ProjectEntry> (e); return {}; }
    void rebuildBag()
    { bag = ids; bag.erase (std::remove (bag.begin(), bag.end(), current), bag.end()); std::shuffle (bag.begin(), bag.end(), random); }
    juce::Array<ProjectEntry> entries;
    std::vector<juce::String> ids, bag, history;
    juce::String current;
    int cursor = -1;
    std::mt19937 random { std::random_device{}() };
};
