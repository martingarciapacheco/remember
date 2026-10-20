#pragma once
#include <JuceHeader.h>

enum class RememberAppearance { System, Light, Dark };

inline RememberAppearance appearanceFromConfig (const juce::var& config)
{
    // Legacy light/dark settings predate automatic appearance. Existing
    // profiles adopt System until the user explicitly chooses an override.
    const auto mode = config.getProperty ("appearanceMode", "system").toString();
    if (mode == "light") return RememberAppearance::Light;
    if (mode == "dark") return RememberAppearance::Dark;
    return RememberAppearance::System;
}
