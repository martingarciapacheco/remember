#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>

// Used exclusively by the index worker. Native requests retain their buffers
// until cancellation has completed; no watch handle belongs to a UI component.
class LibraryWatch
{
public:
    enum Change : unsigned { None = 0, Library = 1, Candidates = 2 };
    LibraryWatch();
    ~LibraryWatch();
    void configure (const juce::File& config, const juce::File& previews,
                    const juce::Array<juce::File>& projectRoots);
    unsigned takeChanges();
    void clear();
    bool hasCompleteCoverage() const;
private:
    struct Watch;
    std::vector<std::unique_ptr<Watch>> watches;
    juce::String signature;
    bool complete = false;
};
