#pragma once
#include "OfflineRendering.h"
namespace OfflineRendering
{
struct ProjectTiming
{
    juce::Result result = juce::Result::ok();
    double seconds = 0, bpm = 0;
    bool selection = false;
};
// Reference duration at the saved tempo. It does not simulate tempo automation,
// DSP tails, plugin loading or external hardware.
ProjectTiming readProjectTiming (const juce::File&, const Cancel& = {});
struct Estimate
{
    double audioSeconds = 0, timeSeconds = 0;
    juce::int64 bytes = 0;
    int unknownProjects = 0, historicalProjects = 0;
    bool unknownSize = false;
};
Estimate estimateBatch (const BatchPlan&, const FlStudioSettings&, const juce::var& config, const Cancel& = {});
juce::Result recordCompletedRender (const Command&, const RenderResult&, double audioSeconds, bool useAsPreview);
juce::File bestPreview (const juce::Array<juce::File>&);
}
