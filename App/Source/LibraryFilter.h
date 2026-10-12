#pragma once
#include "ProjectScanner.h"
#include <algorithm>

struct LibraryFilter
{
    enum Sort { Latest = 1, Oldest, Name, BpmAscending, BpmDescending };
    juce::String query, folder, daw, key, tags, status, dateFrom, dateTo;
    double bpmFrom = 0, bpmTo = 0;
    bool favorites = false, missing = false, untagged = false;
    int sort = Latest;
    bool matches (const ProjectEntry& entry) const
    {
        if (! entry.matches (query) || (favorites && ! entry.favorite)) return false;
        if (missing && (entry.pending || entry.projectAvailable)) return false;
        if (untagged && ! entry.tags.isEmpty()) return false;
        if (daw.isNotEmpty() && ! entry.daw.equalsIgnoreCase (daw)) return false;
        if (key.isNotEmpty() && ! entry.key.equalsIgnoreCase (key)) return false;
        if (status.isNotEmpty() && entry.status != status) return false;
        if (bpmFrom > 0 && entry.bpm < bpmFrom) return false;
        if (bpmTo > 0 && (entry.bpm <= 0 || entry.bpm > bpmTo)) return false;
        const auto date = entry.recordedAt.substring (0, 10);
        if (dateFrom.isNotEmpty() && date < dateFrom) return false;
        if (dateTo.isNotEmpty() && date > dateTo) return false;
        for (auto tag : juce::StringArray::fromTokens (tags, ",", {}))
            if (! entry.tags.contains (tag.trim(), true)) return false;
        if (folder.isNotEmpty())
        {
            bool found = false;
            for (const auto& item : entry.folders)
                if (item == folder || item.startsWith (folder + "/")) { found = true; break; }
            if (! found) return false;
        }
        return true;
    }
    juce::var json() const
    {
        juce::var value (new juce::DynamicObject()); auto& o = *value.getDynamicObject();
        o.setProperty ("query", query); o.setProperty ("folder", folder);
        o.setProperty ("daw", daw); o.setProperty ("key", key); o.setProperty ("tags", tags);
        o.setProperty ("status", status); o.setProperty ("dateFrom", dateFrom); o.setProperty ("dateTo", dateTo);
        o.setProperty ("bpmFrom", bpmFrom); o.setProperty ("bpmTo", bpmTo);
        o.setProperty ("favorites", favorites); o.setProperty ("missing", missing);
        o.setProperty ("untagged", untagged); o.setProperty ("sort", sort); return value;
    }
    static LibraryFilter fromJson (const juce::var& v)
    {
        LibraryFilter f;
        f.query = v.getProperty ("query", "").toString(); f.folder = v.getProperty ("folder", "").toString();
        f.daw = v.getProperty ("daw", "").toString(); f.key = v.getProperty ("key", "").toString();
        f.tags = v.getProperty ("tags", "").toString(); f.status = v.getProperty ("status", "").toString();
        f.dateFrom = v.getProperty ("dateFrom", "").toString(); f.dateTo = v.getProperty ("dateTo", "").toString();
        f.bpmFrom = (double) v.getProperty ("bpmFrom", 0.0); f.bpmTo = (double) v.getProperty ("bpmTo", 0.0);
        f.favorites = (bool) v.getProperty ("favorites", false); f.missing = (bool) v.getProperty ("missing", false);
        f.untagged = (bool) v.getProperty ("untagged", false); f.sort = juce::jlimit (1, 5, (int) v.getProperty ("sort", 1));
        return f;
    }
    void order (juce::Array<ProjectEntry>& entries) const
    {
        std::stable_sort (entries.begin(), entries.end(), [this] (const auto& a, const auto& b)
        {
            int result = 0;
            if (sort == Name) result = a.name.compareNatural (b.name);
            else if (sort == BpmAscending || sort == BpmDescending)
                result = a.bpm < b.bpm ? -1 : a.bpm > b.bpm ? 1 : 0;
            else result = a.recordedAt.compare (b.recordedAt);
            if (sort == Latest || sort == BpmDescending) result = -result;
            return result != 0 ? result < 0 : a.projectId < b.projectId;
        });
    }
};
