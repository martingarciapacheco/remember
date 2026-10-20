#pragma once
#include "ProjectScanner.h"
#include <array>
#include <set>

namespace ProjectStatistics
{
enum class Category { Samples, Instruments, Effects };
struct Item
{
    juce::String name, identity, path;
    int projects = 0;
    bool available = false;
};
struct Inventory
{
    juce::Result result = juce::Result::ok();
    std::array<std::vector<Item>, 3> items;
    int unclassifiedPlugins = 0;
    juce::StringArray warnings;
};
struct Report
{
    std::array<std::vector<Item>, 3> items;
    int read = 0, excluded = 0, failed = 0, unlinked = 0, unclassifiedPlugins = 0;
    juce::StringArray details;
    juce::String error;
    // Estimate owned metadata, including string allocation overhead; excludes UI/audio.
    size_t metadataBytes() const;
};
using Cancel = std::function<bool()>;
using HiddenEntries = std::array<std::set<juce::String>, 3>;
// Worker-only preference operations. Atomic per-identity deltas preserve other windows/settings.
juce::Result readHiddenEntries (HiddenEntries&);
juce::Result saveHiddenEntry (Category, const juce::String& identity, bool hidden);
// Worker-only. Readers inspect saved data; never instantiate plugins or decode sample audio.
Inventory readProject (const juce::File&, const Cancel& = {});
Report collect (const juce::Array<ProjectEntry>&, const Cancel& = {});
juce::String pathIdentity (const juce::String&);
}
