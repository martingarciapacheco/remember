#pragma once
#include "ProjectIdentity.h"

namespace RememberMac
{
struct DocumentWindow { juce::String title, path; };
// Pure selection policy, also exercised without a DAW. Multiple documents are
// ambiguous; window order/focus never establishes a plugin instance's identity.
HostProject projectFromWindows (const std::vector<DocumentWindow>&, const juce::String& daw);
#if JUCE_MAC
HostProject detectHostProject (const juce::String& daw);
juce::File applicationExecutable (const juce::File& bundle);
juce::Image applicationIcon (const juce::File& bundle);
juce::Result requireFlStudioClosed();
#endif
}
