#pragma once
#include <JuceHeader.h>

// Shared defaults. Read on workers and publish atomics to the audio callback.
struct CaptureOptions
{
    bool manual = false, locked = false, replace = false;
    static CaptureOptions fromJson (const juce::var& value)
    {
        return { (bool) value.getProperty ("manualCapture", false),
                 (bool) value.getProperty ("lockPreviews", false),
                 (bool) value.getProperty ("replacePreview", false) };
    }
    juce::var patch() const
    {
        juce::var value (new juce::DynamicObject());
        value.getDynamicObject()->setProperty ("manualCapture", manual);
        value.getDynamicObject()->setProperty ("lockPreviews", locked);
        value.getDynamicObject()->setProperty ("replacePreview", replace);
        return value;
    }
};
