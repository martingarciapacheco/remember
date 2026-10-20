#include "../Shared/PreviewAudio.h"
#include "WorkflowCaptureTests.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Plugin/Source/PluginProcessor.h"
#include "../Plugin/Source/PluginEditor.h"
#include "../App/Source/ProjectScanner.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }
template<class Fn> void waitAt (int line, Fn condition)
{
    const auto until = juce::Time::getMillisecondCounter() + 15000;
    while (! condition()) { if (juce::Time::getMillisecondCounter() >= until) throw std::runtime_error ("Capture workflow timed out at line " + std::to_string (line)); juce::Thread::sleep (5); }
}
#define waitFor(...) waitAt (__LINE__, __VA_ARGS__)
struct Head : juce::AudioPlayHead
{
    bool playing = true; double seconds = 0;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setIsPlaying (playing); p.setTimeInSeconds (seconds); p.setBpm (128); return p; }
};
void render (RecordingEngine& engine, double start, double seconds, float value, bool stopAfter = true)
{
    Head head; juce::AudioBuffer<float> audio (2, 128);
    const auto samples = (int) (seconds * 8000);
    for (int done = 0; done < samples; done += audio.getNumSamples())
    {
        audio.setSize (2, juce::jmin (128, samples - done), false, false, true);
        audio.clear(); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample (ch, i, value * (float) std::cos (juce::MathConstants<double>::twoPi * 400 * (done + i) / 8000));
        head.seconds = start + done / 8000.0; engine.processBlock (audio, &head);
        check (audio.getSample (0, 0) == value * (float) std::cos (juce::MathConstants<double>::twoPi * 400 * done / 8000), "Capture options changed passthrough audio");
        if (done % 4096 == 0) juce::Thread::sleep (1);
    }
    if (stopAfter)
    { head.playing = false; head.seconds = start + seconds; engine.processBlock (audio, &head); }
}
juce::var committedMetadata (const juce::File& manifest)
{
    juce::var result;
    waitFor ([&]
    {
        result = LibraryStore::readJson (manifest);
        return result.getDynamicObject() != nullptr && result["audioRevision"].toString().isNotEmpty();
    });
    return result;
}
}
void testWorkflowCapture (const juce::File& root)
{
    const auto library = root.getChildFile ("workflow-capture"), project = root.getChildFile ("Workflow.als");
    check (library.createDirectory().wasOk() && project.replaceWithText ("capture fixture"), "Cannot create workflow capture files");
    check (ProjectScanner::saveSettings (library, { root }, 15, 4, false).wasOk(), "Cannot configure capture library");
    auto options = std::make_shared<SettingsManager::Snapshot>(); options->previews = library; options->seconds = 15;
    options->fingerprint = "fixture";
    const auto detect = [project] { return HostProject { true, project.getFileNameWithoutExtension(), project.getFullPathName(), "Ableton Live" }; };
    {
        // Defaults apply at creation; live shared locks never replace local mode.
        RememberProcessor existing (detect); existing.prepareToPlay (8000, 128);
        waitFor ([&] { return ! existing.getEngine().isPreparingSettings() && existing.getEngine().isProjectConfirmed(); });
        CaptureOptions appOptions { true, false, false };
        check (ProjectScanner::saveSettings (library, { root }, 15, 4, false, &appOptions).wasOk(), "Cannot set app capture defaults");
        RememberProcessor processor (detect); processor.prepareToPlay (8000, 128);
        waitFor ([&] { return ! processor.getEngine().isPreparingSettings() && processor.getEngine().getCaptureOptions().manual; });
        appOptions = { false, true, true };
        check (ProjectScanner::saveSettings (library, { root }, 15, 4, false, &appOptions).wasOk(), "Cannot update shared app capture switches");
        waitFor ([&] { const auto value = processor.getEngine().getCaptureOptions(); return value.manual && value.locked && value.replace; });
        check (! existing.getEngine().getCaptureOptions().manual, "New default changed an existing instance");
        processor.prepareToPlay (44100, 512);
        waitFor ([&] { return ! processor.getEngine().isPreparingSettings() && processor.getEngine().getCaptureOptions().replace; });
        check (processor.getEngine().getCaptureOptions().manual, "Repreparation replaced this project's manual mode");
        auto editor = std::unique_ptr<RememberEditor> (static_cast<RememberEditor*> (processor.createEditor()));
        juce::ToggleButton* lockToggle = nullptr;
        juce::ToggleButton* manualToggle = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
            {
                if (toggle->getButtonText() == "Lock preview updates") lockToggle = toggle;
                if (toggle->getButtonText() == "Capture from any position") manualToggle = toggle;
            }
        check (lockToggle && manualToggle, "Plugin editor capture switches are missing");
        waitFor ([&] { editor->timerCallback(); return lockToggle->isEnabled() && lockToggle->getToggleState(); });
        lockToggle->setToggleState (false, juce::dontSendNotification); lockToggle->onClick();
        waitFor ([&] { editor->timerCallback(); return lockToggle->isEnabled() && ! processor.getEngine().getCaptureOptions().locked; });
        const auto persisted = ProjectScanner::readSettings().capture;
        check (! persisted.locked && ! persisted.manual && persisted.replace, "Plugin switch did not persist to app Settings or changed other options");
        manualToggle->setToggleState (false, juce::dontSendNotification); manualToggle->onClick();
        manualToggle->setToggleState (true, juce::dontSendNotification); manualToggle->onClick();
        check (processor.getEngine().getCaptureOptions().manual && ! ProjectScanner::readSettings().capture.manual,
               "Local manual switch changed the creation default");
        juce::MemoryBlock state; processor.getStateInformation (state);
        RememberProcessor reopened (detect); reopened.setStateInformation (state.getData(), (int) state.getSize()); reopened.prepareToPlay (8000, 128);
        waitFor ([&] { return ! reopened.getEngine().isPreparingSettings() && reopened.getEngine().isProjectConfirmed(); });
        check (reopened.getEngine().getCaptureOptions().manual, "Saved project's manual mode did not survive reopening");
        appOptions = { true, false, true };
        check (ProjectScanner::saveSettings (library, { root }, 15, 4, false, &appOptions).wasOk(), "Cannot enable default for legacy-state test");
        manualToggle->setToggleState (false, juce::dontSendNotification); manualToggle->onClick();
        check (ProjectScanner::readSettings().capture.manual, "Local disable overwrote the enabled creation default");
        processor.getStateInformation (state);
        reopened.setStateInformation (state.getData(), (int) state.getSize()); reopened.prepareToPlay (8000, 128);
        waitFor ([&] { return ! reopened.getEngine().isPreparingSettings(); });
        check (! reopened.getEngine().getCaptureOptions().manual, "Saved disabled mode inherited the enabled default");
        auto legacy = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        legacy->removeAttribute ("manualCapture"); juce::AudioProcessor::copyXmlToBinary (*legacy, state);
        reopened.setStateInformation (state.getData(), (int) state.getSize()); reopened.prepareToPlay (8000, 128);
        waitFor ([&] { return ! reopened.getEngine().isPreparingSettings(); });
        check (! reopened.getEngine().getCaptureOptions().manual, "Legacy project inherited a newer manual default");
        editor->timerCallback();
        check (! processor.getEngine().getCaptureOptions().manual, "Editor refresh replaced local manual mode");
        editor.reset();
        appOptions = {}; check (ProjectScanner::saveSettings (library, { root }, 15, 4, false, &appOptions).wasOk(), "Cannot reset fixture switches");
    }
    waitFor ([] { return BackgroundRetirement::pending() == 0; });
    {
        // An already prepared processor follows app Settings without reopening.
        RememberProcessor processor (detect); processor.prepareToPlay (8000, 128);
        auto& engine = processor.getEngine();
        waitFor ([&] { return ! engine.isPreparingSettings() && engine.isProjectConfirmed(); });
        engine.setCaptureOptions ({ true, false, true });
        const auto movedLibrary = root.getChildFile ("live-output-library");
        check (movedLibrary.createDirectory().wasOk(), "Cannot create live output library");
        check (ProjectScanner::saveSettings (movedLibrary, { root }, 15, 4, false).wasOk(), "Cannot change app output folder");
        waitFor ([&]
        {
            juce::XmlElement state ("Remember"); engine.saveState (state);
            return state.getStringAttribute ("previewsFolder") == movedLibrary.getFullPathName();
        });
        render (engine, 12, 2, 0.3f);
        const auto manifest = movedLibrary.getChildFile (engine.getAssociationInfo().id).getChildFile ("metadata.json");
        const auto firstRevision = committedMetadata (manifest)["audioRevision"].toString();
        waitFor ([&] { return engine.getState() == RecordingEngine::State::Done; });
        check (manifest.existsAsFile(), "Live folder change did not redirect the next preview");
        // Two takes separated by a seek while the host stays playing. There is
        // no stopped callback and no arming request between these positions.
        render (engine, 24, 2, 0.4f, false);
        render (engine, 36, 2, 0.5f);
        waitFor ([&] { return committedMetadata (manifest)["audioRevision"].toString() != firstRevision; });
        juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
        const auto hasCaptured = [&] (float value)
        {
            const auto info = LibraryStore::readJson (manifest);
            auto captured = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (
                LibraryStore::audioFile (manifest.getParentDirectory(), info, true)));
            juce::AudioBuffer<float> sample (2, 1);
            const auto readable = captured && captured->read (&sample, 0, 1, 4000, true, true);
            return readable
                && std::abs (sample.getSample (0, 0) - value) < 0.02f;
        };
        waitFor ([&] { return hasCaptured (0.5f); });
        waitFor ([&] { return engine.getState() == RecordingEngine::State::Done; });
        render (engine, 48, 2, 0.6f, false);
        render (engine, 6, 2, 0.7f);
        waitFor ([&] { return hasCaptured (0.7f); });
        check (ProjectScanner::saveSettings (library, { root }, 15, 4, false).wasOk(), "Cannot restore app output folder");
    }
    waitFor ([] { return BackgroundRetirement::pending() == 0; });
    {
        RecordingEngine engine (detect); engine.prepare (8000, 128, *options);
        waitFor ([&] { return engine.isProjectConfirmed(); });
        render (engine, 0, 5, 0.1f); waitFor ([&] { return engine.getState() == RecordingEngine::State::Done; });
        const auto folder = library.getChildFile (engine.getAssociationInfo().id), manifest = folder.getChildFile ("metadata.json");
        const auto baseline = committedMetadata (manifest);
        check ((double) baseline["durationSeconds"] >= 4.99, "Initial full preview was incomplete");
        const auto revision = baseline["audioRevision"].toString();
        auto baselineReader = PreviewAudio::open (LibraryStore::audioFile (folder, baseline, false));
        juce::AudioBuffer<float> baselineTail (2, 1024);
        check (baselineReader && baselineReader->read (&baselineTail, 0, 1024, 32000, true, true), "Cannot read original tail");
        baselineReader.reset();
        engine.setCaptureOptions ({ true, false, false });
        render (engine, 10, 2, 0.2f);
        juce::var merged;
        waitFor ([&]
        {
            merged = LibraryStore::readJson (manifest);
            const auto candidate = merged["audioRevision"].toString();
            return merged.getDynamicObject() != nullptr && candidate.isNotEmpty() && candidate != revision;
        });
        if (! ((double) merged["durationSeconds"] > 4.99 && (double) merged["preservedTailSeconds"] > 2.9))
            throw std::runtime_error (("Short manual capture did not preserve the old tail: " + juce::JSON::toString (merged)).toStdString());
        juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
        auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (LibraryStore::audioFile (folder, merged, true)));
        juce::AudioBuffer<float> sample (2, 1024); check (reader && reader->read (&sample, 0, 1024, 32000, true, true), "Cannot read preserved tail");
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 1024; ++i)
            check (sample.getSample (ch, i) == baselineTail.getSample (ch, i), "Tail changed under prefix policy");
        reader.reset();
        const auto mergedRevision = merged["audioRevision"].toString();
        engine.setCaptureOptions ({ true, true, false }); render (engine, 20, 2, 0.3f); juce::Thread::sleep (100);
        check (committedMetadata (manifest)["audioRevision"].toString() == mergedRevision, "Global lock changed preview audio");
        juce::var patch (new juce::DynamicObject()); patch.getDynamicObject()->setProperty ("bpmOverride", 133.5);
        patch.getDynamicObject()->setProperty ("keyOverride", "G major"); patch.getDynamicObject()->setProperty ("notes", "Keep this note");
        check (LibraryStore::patchJson (manifest, patch).wasOk(), "Cannot set musical corrections");
        engine.setCaptureOptions ({ true, false, true });
        render (engine, 20, 2, 0.4f);
        juce::var replaced;
        waitFor ([&]
        {
            replaced = LibraryStore::readJson (manifest);
            const auto candidate = replaced["audioRevision"].toString();
            return replaced.getDynamicObject() != nullptr && candidate.isNotEmpty() && candidate != mergedRevision;
        });
        check ((double) replaced["durationSeconds"] < 2.1 && (double) replaced["preservedTailSeconds"] == 0
            && replaced["previewUpdatePolicy"].toString() == "replace", "Full replacement retained old tail");
        check ((double) replaced["bpm"] == 128 && (double) replaced["bpmOverride"] == 133.5
            && replaced["keyOverride"].toString() == "G major" && replaced["notes"].toString() == "Keep this note", "Recapture overwrote manual corrections or DAW tempo");
        waitFor ([&] { return engine.getDetectedBpm() == 133.5 && engine.getDetectedKey() == "G major"; });
        patch = juce::var (new juce::DynamicObject()); patch.getDynamicObject()->setProperty ("previewLocked", true);
        check (LibraryStore::patchJson (manifest, patch).wasOk(), "Cannot lock individual preview");
        const auto replacedRevision = replaced["audioRevision"].toString();
        render (engine, 30, 2, 0.5f); juce::Thread::sleep (250);
        check (committedMetadata (manifest)["audioRevision"].toString() == replacedRevision, "Individual preview lock was ignored");
    }
    waitFor ([] { return BackgroundRetirement::pending() == 0; });
    std::cout << "PASS: automatic capture at nonzero transport and seeks, global/individual locks, prefix preservation, full replacement, untouched passthrough and persistent musical overrides\n";
}
