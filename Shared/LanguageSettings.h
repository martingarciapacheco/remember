#pragma once
#include "LibraryStore.h"
#include "RememberLanguage.h"

namespace LanguageSettings
{
// Worker only; the shared atomic patch preserves all other settings.
inline juce::Result save (RememberUI::Language language)
{
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty ("language", language == RememberUI::Language::SpanishSpain ? "es-ES" : "en");
    return LibraryStore::patchJson (LibraryStore::configFile(), patch, true);
}
}
