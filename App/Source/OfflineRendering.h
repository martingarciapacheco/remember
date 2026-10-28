#pragma once
#include <JuceHeader.h>
#include <functional>
#include <map>
#include <vector>

// Filesystem and process work belongs on a dedicated worker, never on a UI,
// audio or library-index callback.
namespace OfflineRendering
{
using Cancel = std::function<bool()>;
struct FlStudioSettings
{
    juce::Result result = juce::Result::ok();
    juce::String registryVersion;
    juce::NamedValueSet values; // Actual stored numeric values; absent stays unknown.
    juce::StringArray missing;
};
using SettingReader = std::function<juce::String (const juce::String& valueName)>;
FlStudioSettings readFlStudioSettings (const juce::String& registryVersion, SettingReader = {});
struct Sources
{
    juce::Result result = juce::Result::ok();
    juce::Array<juce::File> projects;
};
Sources collectProjects (const juce::File& fileOrFolder, bool includeSubfolders,
                         const Cancel& cancelled = {});

struct Command
{
    juce::Result result = juce::Result::ok();
    // Pass as an argument vector, never concatenate into a shell command.
    juce::StringArray arguments;
    juce::Array<juce::File> outputs;
    juce::Array<juce::File> existingOutputs;
    juce::File executable, project, destination;
    juce::StringArray formats;
    std::map<juce::String, juce::String> outputStamps;
};
// Formats are explicit, canonical extension names; no quality options or
// implicit overwrite approval are inferred from them.
Command flStudioCommand (const juce::File& executable, const juce::File& project,
                         const juce::File& destination,
                         const juce::StringArray& formats);

struct RememberOptions
{
    bool saveBesideProject = true, includeSubfolders = false;
    bool useRendersAsPreviews = false, sourceIsFolder = false;
    juce::String customDestination, sourcePath;
    juce::StringArray formats;
};
RememberOptions optionsFromConfig (const juce::var&);
juce::Result saveOptions (const RememberOptions&);
struct BatchPlan
{
    juce::Result result = juce::Result::ok();
    std::vector<Command> jobs;
    juce::Array<juce::File> existingOutputs, duplicateOutputs;
};
BatchPlan planBatch (const juce::File& executable, const Sources&, const RememberOptions&,
                     const juce::StringArray& formats, const Cancel& = {});

using ProcessRunner = std::function<juce::Result (const juce::StringArray&, const Cancel&)>;
struct RenderResult
{
    juce::Result result = juce::Result::ok();
    juce::Array<juce::File> published;
    double seconds = 0;
    bool cancelled = false;
};
// The approval covers only the exact output state recorded by planning. New
// or changed files invalidate it. FL writes to staging; validation precedes
// replacing any final output. A runner can be injected for native regressions.
RenderResult render (const Command&, bool overwriteApproved, const Cancel& = {}, ProcessRunner = {});
juce::Result requireFlStudioClosed();
}
