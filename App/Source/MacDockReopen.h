#pragma once
#include <JuceHeader.h>

#if JUCE_MAC
// Standalone only: extend JUCE's delegate without replacing its quit/file handlers.
class MacDockReopen final
{
public:
    explicit MacDockReopen (std::function<void()>);
    ~MacDockReopen();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
#endif
