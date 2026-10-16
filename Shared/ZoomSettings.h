#pragma once
#include "LibraryStore.h"
#include "RememberLookAndFeel.h"

// Worker-only, atomic patches preserve recording and appearance preferences.
namespace ZoomSettings
{
inline int read (const juce::var& config, bool plugin)
{
    return RememberUI::validZoom ((int) config.getProperty (plugin ? "pluginZoom" : "appZoom", 100));
}
inline juce::Result save (int percent, bool plugin)
{
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty (plugin ? "pluginZoom" : "appZoom", RememberUI::validZoom (percent));
    return LibraryStore::patchJson (LibraryStore::configFile(), patch, true);
}
}
