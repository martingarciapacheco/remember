#pragma once
#include "LibraryStore.h"

// Config operations are worker-only; appearance never touches audio state.
namespace AppearanceSettings
{
inline bool isDark (const juce::var& config)
{
    return config.getProperty ("appearance", "light").toString() == "dark";
}
inline juce::Result save (bool dark)
{
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty ("appearance", dark ? "dark" : "light");
    return LibraryStore::patchJson (LibraryStore::configFile(), patch, true);
}
}
