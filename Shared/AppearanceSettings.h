#pragma once
#include "LibraryStore.h"
#include "AppearanceMode.h"

// Config operations are worker-only; appearance never touches audio state.
namespace AppearanceSettings
{
inline juce::Result save (RememberAppearance mode)
{
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty ("appearanceMode", mode == RememberAppearance::System ? "system"
        : mode == RememberAppearance::Dark ? "dark" : "light");
    return LibraryStore::patchJson (LibraryStore::configFile(), patch, true);
}
}
