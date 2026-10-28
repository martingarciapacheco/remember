#include "PreviewSelectionTests.h"
#include "../App/Source/PreviewSelection.h"
#include "../App/Source/PreviewExport.h"
#include "../App/Source/ProjectListComponent.h"
#include "../App/Source/WaveformPlayerComponent.h"
#include <stdexcept>
#include <iostream>

struct PreviewSelectionTestAccess
{
    static bool loaded (WaveformPlayerComponent& player) { return player.isLoaded && player.waveform != nullptr; }
    static juce::File source (WaveformPlayerComponent& player) { return player.currentAudioFile; }
    static double length (WaveformPlayerComponent& player) { return player.transport.getLengthInSeconds(); }
};

namespace
{
void check (bool ok, const char* reason) { if (! ok) throw std::runtime_error (reason); }
void audio (const juce::File& file, int frames, bool ogg)
{
    juce::WavAudioFormat wav; juce::OggVorbisAudioFormat compressed;
    juce::AudioFormat& format = ogg ? (juce::AudioFormat&) compressed : (juce::AudioFormat&) wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    check (writer != nullptr, "Cannot create preview selection fixture");
    juce::AudioBuffer<float> buffer (2, frames); buffer.clear();
    check (writer->writeFromAudioSampleBuffer (buffer, 0, frames), "Cannot write preview selection fixture");
}
void snapshot (juce::Component& component, const juce::String& name)
{
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts");
    check (directory.createDirectory().wasOk(), "Cannot create selected-preview snapshots");
    juce::FileOutputStream output (directory.getChildFile (name)); juce::PNGImageFormat png;
    check (output.openedOk() && png.writeImageToStream (component.createComponentSnapshot (
        component.getLocalBounds(), true, 1, juce::SoftwareImageType{}), output), "Cannot save selected-preview snapshot");
}
}
void testPreviewSelection (const juce::File& root)
{
    const auto library = root.getChildFile ("selected-preview-library");
    const auto directory = library.getChildFile ("capture");
    check (directory.createDirectory().wasOk(), "Cannot create selected-preview library");
    const auto original = directory.getChildFile ("preview.ogg");
    const auto external = root.getChildFile ("Full render español.wav");
    audio (original, 24000, true); audio (external, 96000, false);
    juce::var metadata (new juce::DynamicObject());
    metadata.getDynamicObject()->setProperty ("projectId", juce::Uuid().toString());
    metadata.getDynamicObject()->setProperty ("projectName", "A very long project name with a manually selected full render");
    const auto file = directory.getChildFile ("metadata.json");
    check (LibraryStore::writeJsonAtomically (file, metadata).wasOk(), "Cannot publish selection metadata");
    auto entries = ProjectScanner::scan (library, {});
    check (entries.size() == 1 && entries[0].storageError.isEmpty(), "Cannot scan selection fixture");
    const auto stale = entries[0];
    check (PreviewSelection::select (stale, external, true).wasOk(), "Cannot select external preview");
    entries = ProjectScanner::scan (library, {});
    auto selected = entries[0];
    check (selected.selectedPreviewEnabled && selected.selectedPreviewAvailable
           && selected.selectedPreviewSource == "manual" && selected.previewOgg == original, "Selection changed capture provenance");
    juce::File actual;
    auto reader = PreviewSelection::open (selected, actual);
    check (reader && actual == external && reader->lengthInSamples == 96000, "Playback did not choose full render");
    reader.reset();
    juce::File exported;
    const auto exportResult = PreviewExport::prepare (selected, library, exported, [] { return false; });
    if (exportResult.failed()) throw std::runtime_error ("Cannot export selected preview: " + exportResult.getErrorMessage().toStdString());
    reader = PreviewAudio::open (exported);
    check (reader && reader->lengthInSamples == 96000, "Drag exported original instead of selected preview");
    reader.reset();
    {
        WaveformPlayerComponent player;
        player.loadEntry (selected);
        const auto until = juce::Time::getMillisecondCounter() + 10000;
        while (! PreviewSelectionTestAccess::loaded (player))
        {
            check (juce::Time::getMillisecondCounter() < until, "Selected preview player load timed out");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        check (PreviewSelectionTestAccess::source (player) == external
               && std::abs (PreviewSelectionTestAccess::length (player) - 2.0) < 0.001,
               "Main player did not load full selected audio");
        check (! player.isAudioDeviceOpen(), "Selection alone opened the audio output");
        player.clearPreview();
    }
    check (PreviewSelection::select (stale, original, false).failed(), "Stale render overwrote manual preview");
    check (PreviewSelection::disable (stale).wasOk(), "Cannot disable selected preview");
    entries = ProjectScanner::scan (library, {});
    reader = PreviewSelection::open (entries[0], actual);
    check (reader && actual == original && reader->lengthInSamples == 24000, "Disable failed to restore capture");
    reader.reset();
    check (PreviewSelection::select (stale, original, false).failed(), "Render overrode disabled manual selection");
    check (PreviewSelection::select (stale, external, true).wasOk(), "Cannot reselect manual preview");
    auto other = stale; other.projectId = juce::Uuid().toString();
    check (PreviewSelection::disable (other).failed(), "Stale identity changed another recording");
    auto edit = stale; edit.notes = "Concurrent notes";
    check (edit.save ({ "notes" }).wasOk(), "Cannot save independent metadata");
    check (LibraryStore::readJson (file)["selectedPreviewPath"].toString() == external.getFullPathName(),
           "Unrelated save discarded manual selection");

    const auto language = RememberUI::language.load();
    for (const auto locale : { RememberUI::Language::English, RememberUI::Language::SpanishSpain })
    {
        RememberUI::language.store (locale);
        for (const auto width : { 384, 768 })
        {
            ProjectRowComponent row; row.setSize (width, RememberUI::recordingRowHeight); row.setEntry (selected, false);
            bool indicator = false;
            for (auto* child : row.getChildren())
                if (child->getTitle() == RememberUI::localize ("Selected preview"))
                {
                    indicator = child->isVisible() && ! child->getBounds().isEmpty();
                    auto* tooltip = dynamic_cast<juce::TooltipClient*> (child);
                    check (tooltip && tooltip->getTooltip().contains (RememberUI::localize ("Select preview")),
                           "Indicator lacks localized disable instructions");
                }
            check (indicator, "Selected preview indicator missing");
            snapshot (row, "selected-preview-" + juce::String (locale == RememberUI::Language::English ? "en-" : "es-")
                + juce::String (width) + ".png");
            const auto previousDark = RememberUI::isDark();
            RememberUI::setDark (false);
            snapshot (row, "selected-preview-light-" + juce::String (locale == RememberUI::Language::English ? "en-" : "es-")
                + juce::String (width) + ".png");
            RememberUI::setDark (previousDark);
            row.setEntry (stale, false);
            for (auto* child : row.getChildren()) if (child->getTitle() == RememberUI::localize ("Selected preview"))
                check (! child->isVisible(), "Recycled row retained another entry's preview indicator");
        }
    }
    RememberUI::language.store (language);
    check (external.deleteFile(), "Cannot remove external preview fixture");
    entries = ProjectScanner::scan (library, {});
    check (entries[0].selectedPreviewEnabled && ! entries[0].selectedPreviewAvailable, "Missing override lost its selection state");
    reader = PreviewSelection::open (entries[0], actual);
    check (reader && actual == original, "Missing selected audio did not fall back"); reader.reset();
    check (external.replaceWithText ("invalid audio"), "Cannot write unreadable audio fixture");
    reader = PreviewSelection::open (entries[0], actual);
    check (reader && actual == original, "Unreadable selected audio did not fall back"); reader.reset();
    check (PreviewSelection::select (stale, external, true).failed(), "Accepted invalid audio selection");
    audio (external, 96000, false);
    const auto future = library.getChildFile ("future-capture"); check (future.createDirectory().wasOk(), "Cannot create future recording");
    auto futureMetadata = metadata.clone(); futureMetadata.getDynamicObject()->setProperty ("projectId", juce::Uuid().toString());
    const auto futureFile = future.getChildFile ("metadata.json");
    check (LibraryStore::writeJsonAtomically (futureFile, futureMetadata).wasOk(), "Cannot write future recording");
    const auto projectFile = root.getChildFile ("future.flp"); check (projectFile.replaceWithText ("saved project"), "Cannot write future project");
    ProjectEntry later; later.projectId = futureMetadata["projectId"].toString(); later.projectAvailable = true;
    later.projectPath = projectFile.getFullPathName(); later.metadataFile = futureFile;
    auto path = projectFile.getFullPathName();
#if JUCE_WINDOWS
    path = path.toLowerCase();
#endif
    const auto key = juce::SHA256 (path.toRawUTF8(), path.getNumBytesAsUTF8()).toHexString();
    juce::var history (new juce::DynamicObject()), record (new juce::DynamicObject());
    record.getDynamicObject()->setProperty ("previewPath", external.getFullPathName());
    record.getDynamicObject()->setProperty ("projectFileId", ProjectIdentity::fileId (projectFile));
    record.getDynamicObject()->setProperty ("useAsPreview", true); history.getDynamicObject()->setProperty (key, record);
    check (PreviewSelection::inheritRender (later, history) && later.selectedPreviewEnabled && later.selectedPreviewSource == "render",
           "New recording did not inherit persistent render association");
    check (PreviewSelection::disable (later).wasOk(), "Cannot disable inherited render");
    auto staleLater = later; staleLater.selectedPreviewSource.clear();
    check (! PreviewSelection::inheritRender (staleLater, history)
           && ! (bool) LibraryStore::readJson (futureFile)["selectedPreviewEnabled"],
           "History overrode a concurrent decision to disable render preview");
    std::cout << "Selected previews passed: persistence, precedence, fallback, export and bilingual recycled rows\n";
}
