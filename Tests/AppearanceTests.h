#pragma once
#include "../Shared/RememberLookAndFeel.h"
#include "LanguageTests.h"
#include <stdexcept>

inline void testAutomaticAppearance()
{
    testLocalizedControls();
    const auto originalMode = RememberUI::appearanceMode;
    auto* hostLook = &juce::LookAndFeel::getDefaultLookAndFeel();
    juce::Component app, plugin;
    RememberUI::Style appStyle (app), pluginStyle (plugin);
    juce::TextEditor appText, pluginText;
    app.addAndMakeVisible (appText); plugin.addAndMakeVisible (pluginText);
    RememberUI::bindColour (appText, juce::TextEditor::textColourId, Pal::Text);
    RememberUI::bindColour (pluginText, juce::TextEditor::textColourId, Pal::Text);
    RememberUI::setAppearanceMode (RememberAppearance::System);
    if (RememberUI::isDark() != juce::Desktop::getInstance().isDarkModeActive())
        throw std::runtime_error ("Initial appearance does not match the system");
    bool systemDark = false;
    RememberUI::SystemAppearance detector ([&] { return systemDark; });
    for (bool value : { true, false, true })
    {
        systemDark = value; detector.darkModeSettingChanged();
        if (RememberUI::isDark() != value
            || appText.findColour (juce::TextEditor::textColourId) != RememberUI::colour (Pal::Text)
            || pluginText.findColour (juce::TextEditor::textColourId) != RememberUI::colour (Pal::Text))
            throw std::runtime_error ("System notification did not update every existing UI root");
    }
    RememberUI::setAppearanceMode (RememberAppearance::Light);
    detector.darkModeSettingChanged();
    if (RememberUI::isDark()) throw std::runtime_error ("System notification overrode an explicit appearance");
    RememberUI::setAppearanceMode (RememberAppearance::System);
    detector.darkModeSettingChanged();
    if (! RememberUI::isDark()) throw std::runtime_error ("Returning to System did not resume notifications");
    juce::var legacy (new juce::DynamicObject()); legacy.getDynamicObject()->setProperty ("appearance", "dark");
    if (appearanceFromConfig (legacy) != RememberAppearance::System)
        throw std::runtime_error ("Legacy profile did not adopt automatic appearance");
    if (&juce::LookAndFeel::getDefaultLookAndFeel() != hostLook)
        throw std::runtime_error ("Automatic appearance changed the host theme");
    RememberUI::setAppearanceMode (originalMode);
}

inline void testAppearanceContrast()
{
    const bool original = RememberUI::isDark();
    const auto luminance = [] (juce::Colour c)
    {
        const auto linear = [] (double channel)
        { return channel <= 0.04045 ? channel / 12.92 : std::pow ((channel + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear (c.getFloatRed()) + 0.7152 * linear (c.getFloatGreen()) + 0.0722 * linear (c.getFloatBlue());
    };
    const auto checkPair = [&] (Pal::Token foreground, Pal::Token background)
    {
        const auto a = luminance (RememberUI::colour (foreground)), b = luminance (RememberUI::colour (background));
        if ((juce::jmax (a, b) + 0.05) / (juce::jmin (a, b) + 0.05) < 4.5)
            throw std::runtime_error ("Appearance text contrast is below 4.5:1");
    };
    for (const bool dark : { false, true })
    {
        RememberUI::setDark (dark);
        for (auto background : { Pal::BgDeep, Pal::Surface, Pal::Content, Pal::Elevated, Pal::Selected })
            for (auto foreground : { Pal::Text, Pal::Subtext, Pal::Accent }) checkPair (foreground, background);
        checkPair (Pal::Error, Pal::BgDeep); checkPair (Pal::Success, Pal::BgDeep);
        checkPair (Pal::OnAction, Pal::Action);
        if (dark && (RememberUI::colour (Pal::BgDeep).getARGB() != 0xff141416
            || RememberUI::colour (Pal::Content).getBrightness() <= RememberUI::colour (Pal::BgDeep).getBrightness()))
            throw std::runtime_error ("Dark appearance lost charcoal base or elevated surface");
    }
    RememberUI::setDark (original);
}
