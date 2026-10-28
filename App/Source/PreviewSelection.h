#pragma once
#include "ProjectScanner.h"

namespace PreviewSelection
{
// Worker only. Selection references the external file without changing either
// that file or the original capture.
juce::Result select (const ProjectEntry&, const juce::File&, bool manual);
juce::Result disable (const ProjectEntry&);
// Adopt a completed render for a newly discovered recording only when no
// selection decision has ever been stored for that recording.
bool inheritRender (ProjectEntry&, const juce::var& history);
std::unique_ptr<juce::AudioFormatReader> open (const ProjectEntry&, juce::File& actualFile);
}
