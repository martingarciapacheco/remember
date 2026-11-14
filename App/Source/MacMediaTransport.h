#pragma once
#include <JuceHeader.h>

#if JUCE_MAC
// Standalone only. The VST3 must never register commands in its host process.
class MacMediaTransport final : private juce::Timer
{
public:
    struct State
    {
        bool playing = false, canPlay = false, hasQueue = false;
        juce::String title;
        double duration = 0, position = 0;
    };
    MacMediaTransport (std::function<State()>, std::function<void(int)>);
    ~MacMediaTransport() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback() override;
};
#endif
