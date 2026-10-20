#pragma once
#include <JuceHeader.h>
#include <functional>

namespace LibraryStore::detail
{
// Internal transaction boundary for fault fixtures. The writer must set
// created immediately after creating its new temporary file. It must close
// all handles before returning, including after a partial write or flush error.
// This has no global override, environment switch or audio-thread entry point.
using TemporaryJsonWriter = std::function<juce::Result (const juce::File&, const juce::String&, bool& created)>;
juce::Result writeJsonWithTemporaryWriter (const juce::File& target, const juce::var&,
                                           const TemporaryJsonWriter&);
}
