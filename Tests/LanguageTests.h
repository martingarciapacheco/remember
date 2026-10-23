#pragma once
#include "../Shared/RememberLanguage.h"
#include <stdexcept>

inline void testLocalizedControls()
{
    using namespace RememberUI;
    const auto previous = language.load();
    setLanguage (Language::English);
    RememberUI::Label label ({}, tr ("Settings")), projectName ({}, "Settings");
    RememberUI::TextButton button (tr ("Cancel"));
    RememberUI::ComboBox choice;
    choice.addItem (tr ("Light"), 17); choice.addItem (tr ("Dark"), 29);
    choice.setSelectedId (29, juce::dontSendNotification);
    RememberUI::TextEditor editor;
    editor.setText (juce::String::fromUTF8 ("Mi canción — Settings.wav"));
    editor.setTextToShowWhenEmpty (tr ("Search library"), juce::Colours::grey);
    button.setTitle (tr ("Settings")); button.setTooltip (tr ("Settings"));
    setLanguage (Language::SpanishSpain);
    if (label.getText() != localize ("Settings") || label.getText() == "Settings"
        || button.getButtonText() != localize ("Cancel") || button.getTitle() != localize ("Settings")
        || button.getTooltip() != localize ("Settings") || choice.getText() != localize ("Dark")
        || choice.getSelectedId() != 29 || projectName.getText() != "Settings"
        || editor.getText() != juce::String::fromUTF8 ("Mi canción — Settings.wav")
        || editor.getTextToShowWhenEmpty() != localize ("Search library")
        || localize ("unregistered fallback") != "unregistered fallback")
        throw std::runtime_error ("Language update changed user data, lost IDs or missed an existing control");
    if (localize ("Settings") != juce::String::fromUTF8 ("Configuración")
        || localize ("Statistics") != juce::String::fromUTF8 ("Estadísticas"))
        throw std::runtime_error ("Spanish catalog is not decoded as UTF-8");
    const auto interpolated = RememberUI::format ("Open \"{name}\" in {daw}?",
        { { "{name}", "Unchanged {daw}" }, { "{daw}", "FL Studio" } });
    if (! interpolated.contains ("Unchanged {daw}") || ! interpolated.contains ("FL Studio"))
        throw std::runtime_error ("Translation interpolated a token inside user data");
    // Recycled rows must not retain a former source binding.
    label.setText ("Settings", juce::dontSendNotification);
    setLanguage (Language::English); setLanguage (Language::SpanishSpain);
    if (label.getText() != "Settings") throw std::runtime_error ("Recycled language binding translated user data");
    setLanguage (Language::English);
    if (button.getButtonText() != "Cancel" || choice.getSelectedId() != 29 || choice.getText() != "Dark")
        throw std::runtime_error ("Returning to English lost original text or stable selection");
    juce::var config (new juce::DynamicObject());
    config.getDynamicObject()->setProperty ("language", "es-ES");
    if (languageFromConfig (config) != Language::SpanishSpain) throw std::runtime_error ("Spanish language config not recognised");
    config.getDynamicObject()->setProperty ("language", "future-language");
    if (languageFromConfig (config) != Language::English) throw std::runtime_error ("Unknown language has no English fallback");
    setLanguage (previous);
}
