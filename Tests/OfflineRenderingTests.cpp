#include "OfflineRenderingTests.h"
#include "../App/Source/OfflineRendering.h"
#include "../App/Source/OfflineRenderTiming.h"
#include "../Shared/LibraryStore.h"
#include "../App/Source/ProjectScanner.h"
#include <stdexcept>
#include <iostream>

void testOfflineRendering (const juce::File& root)
{
    const auto check = [] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); };
    const auto settings = OfflineRendering::readFlStudioSettings ("26", [] (const juce::String& key)
    { return key == "WFBox" ? "1" : key == "MasterFXBox" ? "0" : key == "IPBox" ? "malformed" : ""; });
    check (settings.result.wasOk() && (int) settings.values["WFBox"] == 1
           && settings.values.contains ("MasterFXBox") && (int) settings.values["MasterFXBox"] == 0
           && ! settings.values.contains ("IPBox") && settings.missing.contains ("IPBox"),
           "FL settings reader invented defaults or lost disabled options");
    check (OfflineRendering::readFlStudioSettings ("26", [] (const juce::String&) { return ""; }).result.failed(),
           "Missing FL settings presented as discovered");
    check (OfflineRendering::readFlStudioSettings ("26\\General", [] (const juce::String&) { return "1"; }).result.failed(),
           "Settings discovery escaped the selected version");
    const auto folder = root.getChildFile ("render sources & spaces");
    const auto child = folder.getChildFile ("subfolder");
    check (child.createDirectory().wasOk(), "Cannot create render fixture");
    const auto first = folder.getChildFile ("Tema español & 1.flp");
    const auto second = child.getChildFile ("Second.FLP");
    check (first.replaceWithText ("fixture") && second.replaceWithText ("fixture"), "Cannot write FLP fixtures");
    check (folder.getChildFile ("ignore.wav").replaceWithText ("fixture"), "Cannot write non-project fixture");
    auto sources = OfflineRendering::collectProjects (folder, false);
    check (sources.result.wasOk() && sources.projects.size() == 1 && sources.projects[0] == first,
           "Nonrecursive scan escaped selected folder");
    sources = OfflineRendering::collectProjects (folder, true);
    check (sources.result.wasOk() && sources.projects.size() == 2, "Recursive scan missed FLP");
    sources = OfflineRendering::collectProjects (first, true);
    check (sources.projects.size() == 1 && sources.projects[0] == first, "Single FLP scan changed scope");
    sources = OfflineRendering::collectProjects (folder, true, [] { return true; });
    check (sources.result.failed() && sources.projects.isEmpty(), "Cancelled scan published projects");
    check (OfflineRendering::collectProjects (folder.getChildFile ("ignore.wav"), false).result.failed(),
           "Accepted non-FLP source");
    const auto exe = root.getChildFile ("FL64.exe");
    check (exe.replaceWithText ("not executable; never launched"), "Cannot write executable fixture");
    const auto previous = folder.getChildFile (first.getFileNameWithoutExtension() + ".wav");
    check (previous.replaceWithText ("preserve me"), "Cannot write existing render");
    const auto command = OfflineRendering::flStudioCommand (exe, first, folder, { "wav", "mp3", "wav" });
    check (command.result.wasOk() && command.arguments.size() == 5, "Cannot plan explicit render command");
    check (command.arguments[1] == "/Ewav,mp3" && command.arguments[2] == "/R"
           && command.arguments[3] == first.getFullPathName()
           && command.arguments[4] == "/O" + folder.getFullPathName(), "Render arguments changed paths/scope");
    check (command.outputs.size() == 2 && command.existingOutputs.size() == 1
           && command.existingOutputs[0] == previous && previous.loadFileAsString() == "preserve me",
           "Planning failed to report collision or changed an existing render");
    check (OfflineRendering::flStudioCommand (exe, first, folder, { "wav /D" }).result.failed(),
           "Accepted injected format argument");
    check (OfflineRendering::flStudioCommand (exe, first, folder, {}).result.failed(), "Accepted no audio formats");
    const auto single = OfflineRendering::flStudioCommand (exe, first, folder, { "wav" });
    int launches = 0;
    auto fake = [&] (const juce::StringArray& arguments, const OfflineRendering::Cancel&)
    {
        ++launches;
        check (arguments.size() == 5 && arguments[2] == "/R" && arguments[3] == first.getFullPathName(),
               "Executor changed the explicit source project");
        const juce::File stage (arguments[4].substring (2));
        check (stage.getParentDirectory() == folder && stage.getFileName().startsWith (".remember-render-"),
               "FL was not directed to isolated staging");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> output (stage.getChildFile (first.getFileNameWithoutExtension() + ".wav").createOutputStream());
        auto writer = format.createWriterFor (output, juce::AudioFormatWriterOptions{}
            .withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
        check (writer != nullptr, "Cannot write staged render fixture");
        juce::AudioBuffer<float> silence (2, 480); silence.clear();
        check (writer->writeFromAudioSampleBuffer (silence, 0, silence.getNumSamples()), "Cannot finish staged render fixture");
        return juce::Result::ok();
    };
    auto rendered = OfflineRendering::render (single, false, {}, fake);
    check (rendered.result.failed() && launches == 0 && previous.loadFileAsString() == "preserve me",
           "Executor launched or overwrote before approval");
    rendered = OfflineRendering::render (single, true, {}, [] (const juce::StringArray&, const OfflineRendering::Cancel&)
    { return juce::Result::fail ("Synthetic FL failure"); });
    check (rendered.result.failed() && previous.loadFileAsString() == "preserve me", "Failed render destroyed existing audio");
    bool cancelled = false;
    rendered = OfflineRendering::render (single, true, [&] { return cancelled; },
        [&] (const juce::StringArray& arguments, const OfflineRendering::Cancel& cancel)
        { const auto result = fake (arguments, cancel); cancelled = true; return result; });
    check (rendered.cancelled && rendered.published.isEmpty() && previous.loadFileAsString() == "preserve me",
           "Cancelled render published its output");
    rendered = OfflineRendering::render (single, true, {}, fake);
    check (rendered.result.wasOk() && rendered.published.size() == 1 && rendered.published[0] == previous,
           "Approved staged render did not publish the verified WAV");
    const auto beforeStaleAttempt = previous.getSize();
    const auto launchCount = launches;
    rendered = OfflineRendering::render (single, true, {}, fake);
    check (rendered.result.failed() && launches == launchCount && previous.getSize() == beforeStaleAttempt,
           "Stale overwrite approval launched another render");
    const auto replan = OfflineRendering::flStudioCommand (exe, first, folder, { "wav" });
    rendered = OfflineRendering::render (replan, true, {}, [&] (const juce::StringArray& arguments, const OfflineRendering::Cancel& cancel)
    {
        const auto result = fake (arguments, cancel);
        check (previous.replaceWithText ("a newer external edit"), "Cannot simulate concurrent output edit");
        return result;
    });
    check (rendered.result.failed() && previous.loadFileAsString() == "a newer external edit",
           "Render overwrote a concurrent output change after confirmation");
    check (folder.findChildFiles (juce::File::findDirectories, false, ".remember-render-*").isEmpty(),
           "Completed/failed jobs left staging directories");
    const auto extraPlan = OfflineRendering::flStudioCommand (exe, first, folder, { "wav" });
    rendered = OfflineRendering::render (extraPlan, true, {}, [&] (const juce::StringArray& args, const OfflineRendering::Cancel& cancel)
    {
        const auto result = fake (args, cancel);
        const juce::File stage (args[4].substring (2));
        check (stage.getChildFile (first.getFileNameWithoutExtension() + ".wav").copyFileTo (stage.getChildFile ("Extra mixer stem.wav")), "Cannot simulate stem output");
        return result;
    });
    const auto retained = folder.findChildFiles (juce::File::findDirectories, false, ".remember-render-*");
    check (rendered.result.failed() && rendered.published.isEmpty() && retained.size() == 1
           && retained[0].getChildFile ("Extra mixer stem.wav").existsAsFile()
           && previous.loadFileAsString() == "a newer external edit", "Additional outputs were silently lost or published without review");

    const auto configFile = LibraryStore::configFile();
    const auto configText = configFile.loadFileAsString(); const bool existed = configFile.existsAsFile();
    struct RestoreConfig { juce::File file; juce::String text; bool existed;
        ~RestoreConfig() { if (existed) file.replaceWithText (text); else file.deleteFile(); } } restore { configFile, configText, existed };
    juce::var unrelated (new juce::DynamicObject()); unrelated.getDynamicObject()->setProperty ("offlineDefaultDaw", "fl-studio");
    check (LibraryStore::patchJson (configFile, unrelated, true).wasOk(), "Cannot prepare unrelated preference");
    OfflineRendering::RememberOptions options;
    check (options.saveBesideProject && ! options.includeSubfolders, "Offline destination/recursion defaults changed");
    options.saveBesideProject = false; options.includeSubfolders = true; options.sourceIsFolder = true;
    options.useRendersAsPreviews = true; options.sourcePath = folder.getFullPathName(); options.customDestination = root.getFullPathName();
    options.formats = { "wav", "flac" };
    check (OfflineRendering::saveOptions (options).wasOk(), "Cannot persist Remember offline options");
    const auto savedConfig = LibraryStore::readJson (configFile);
    const auto reopened = OfflineRendering::optionsFromConfig (savedConfig);
    check (! reopened.saveBesideProject && reopened.includeSubfolders && reopened.sourceIsFolder && reopened.useRendersAsPreviews
           && reopened.sourcePath == options.sourcePath && reopened.customDestination == options.customDestination && reopened.formats == options.formats
           && savedConfig["offlineDefaultDaw"].toString() == "fl-studio", "Offline preference round-trip lost choices or unrelated config");
    const auto homonym = child.getChildFile (first.getFileName());
    check (first.copyFileTo (homonym), "Cannot create same-name project fixture");
    OfflineRendering::Sources pair; pair.projects = { first, homonym };
    auto batch = OfflineRendering::planBatch (exe, pair, options, { "wav" });
    check (batch.result.wasOk() && batch.jobs.size() == 2 && batch.duplicateOutputs.size() == 1
           && batch.jobs[0].destination == root && batch.jobs[1].destination == root,
           "Custom destination did not flatten subfolders/report within-batch collision");
    options.saveBesideProject = true;
    batch = OfflineRendering::planBatch (exe, pair, options, { "wav" });
    check (batch.result.wasOk() && batch.duplicateOutputs.isEmpty() && batch.jobs[0].destination == folder
           && batch.jobs[1].destination == child, "Default destination was not beside each FLP");
    check (OfflineRendering::planBatch (exe, pair, options, { "wav" }, [] { return true; }).result.failed(),
           "Cancelled batch published a plan");
    check (ProjectScanner::saveSettings (root, { folder }, 60, 4, false, nullptr, 0, -1, "fl-studio").wasOk()
           && ProjectScanner::readSettings().offlineDefaultDaw == "fl-studio", "Default offline DAW did not persist with Settings");
    check (ProjectScanner::saveSettings (root, { folder }, 60, 4, false).wasOk()
           && ProjectScanner::readSettings().offlineDefaultDaw == "fl-studio"
           && OfflineRendering::optionsFromConfig (LibraryStore::readJson (configFile)).includeSubfolders,
           "Legacy settings save discarded default DAW or offline preferences");
    // Minimal independent binary fixtures: saved tempo, two arrangements and
    // a selection belonging to only the inactive arrangement.
    juce::MemoryOutputStream events;
    events.writeByte ((char) 199); events.writeByte (5); events.write ("26.0", 5);
    events.writeByte ((char) 156); events.writeInt (120000);
    events.writeByte (99); events.writeShort (0);
    events.writeByte ((char) 217); events.writeByte (8); events.writeInt (0); events.writeInt (96);
    events.writeByte (99); events.writeShort (1);
    events.writeByte (100); events.writeShort (1);
    events.writeByte ((char) 233); events.writeByte (88);
    events.writeInt (0); events.writeShort (20480); events.writeShort (1); events.writeInt (768);
    events.writeRepeatedByte (0, 76);
    auto writeTiming = [&] (const juce::String& name, bool truncate)
    {
        juce::MemoryOutputStream data;
        data.writeInt (0x64684c46); data.writeInt (6); data.writeShort (0); data.writeShort (1); data.writeShort (96);
        data.writeInt (0x74644c46); data.writeInt ((int) events.getDataSize());
        data.write (events.getData(), events.getDataSize() - (truncate ? 1 : 0));
        auto file = root.getChildFile (name); check (file.replaceWithData (data.getData(), data.getDataSize()), "Cannot write timing fixture");
        return file;
    };
    const auto timed = writeTiming ("timing.flp", false);
    auto timing = OfflineRendering::readProjectTiming (timed);
    check (timing.result.wasOk() && std::abs (timing.seconds - 4.0) < 0.001 && ! timing.selection,
           "Timing used an inactive arrangement selection or wrong tempo");
    events.writeByte ((char) 217); events.writeByte (8); events.writeInt (96); events.writeInt (288);
    timing = OfflineRendering::readProjectTiming (writeTiming ("selection.flp", false));
    check (timing.result.wasOk() && timing.selection && std::abs (timing.seconds - 1.0) < 0.001,
           "Active saved selection duration was not used");
    check (OfflineRendering::readProjectTiming (writeTiming ("truncated.flp", true)).result.failed()
           && OfflineRendering::readProjectTiming (timed, [] { return true; }).result.failed(),
           "Truncated/cancelled timing read produced an estimate");
    OfflineRendering::BatchPlan timedPlan;
    timedPlan.jobs.push_back (OfflineRendering::flStudioCommand (exe, timed, root, { "wav" }));
    auto sizeSettings = OfflineRendering::readFlStudioSettings ("26", [] (const juce::String& key)
    { return key == "SampleRate" ? "48000" : key == "WFBox" ? "1" : key == "ChannelsBox" ? "0" : ""; });
    auto estimate = OfflineRendering::estimateBatch (timedPlan, sizeSettings, {});
    check (estimate.audioSeconds == 4 && estimate.timeSeconds == 4 && estimate.historicalProjects == 0
           && estimate.bytes == 4 * 48000 * 2 * 3 + 4096 && ! estimate.unknownSize,
           "Initial audio-time/PCM-size reference is wrong");
    timedPlan.jobs.push_back (OfflineRendering::flStudioCommand (exe, first, root, { "wav" }));
    estimate = OfflineRendering::estimateBatch (timedPlan, {}, {});
    check (estimate.unknownProjects == 1 && estimate.unknownSize && estimate.bytes == 0,
           "Missing metadata became a false size/duration estimate");
    timedPlan.jobs.pop_back();
    OfflineRendering::RenderResult measured; measured.seconds = 12;
    measured.published.add (retained[0].getChildFile ("Extra mixer stem.wav"));
    check (OfflineRendering::recordCompletedRender (timedPlan.jobs[0], measured, 4, false).wasOk(), "Cannot persist measured render time");
    estimate = OfflineRendering::estimateBatch (timedPlan, sizeSettings, LibraryStore::readJson (configFile));
    check (estimate.historicalProjects == 1 && estimate.timeSeconds == 12, "Measured render history did not replace the initial 1x reference");
    auto invalidChannels = sizeSettings; invalidChannels.values.set ("ChannelsBox", 99);
    check (OfflineRendering::estimateBatch (timedPlan, invalidChannels, {}).unknownSize, "Invalid channel mode became a known size");
    std::cout << "Offline rendering planning, staged execution, collision protection and preferences passed (no DAW launched)\n";
}
