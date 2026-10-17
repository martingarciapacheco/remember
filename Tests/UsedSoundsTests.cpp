#include "UsedSoundsTests.h"
#include "../App/Source/UsedSoundsDialog.h"
#include "../App/Source/SettingsDialog.h"
#include "../App/Source/WaveformPlayerComponent.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>

struct UsedSoundsTestAccess
{
    static WaveformPlayerComponent& player (UsedSoundsDialog& dialog) { return *dialog.auditionPlayer; }
    static void noAutoPlay (UsedSoundsDialog& dialog) { dialog.autoPlayAudition = false; }
    static bool hasWaveform (WaveformPlayerComponent& player) { return player.waveform != nullptr; }
    static juce::ListBox& list (UsedSoundsDialog& dialog) { return dialog.list; }
    static void audition (UsedSoundsDialog& dialog, size_t index) { dialog.audition (index); }
    static void stop (UsedSoundsDialog& dialog) { dialog.stopAudition(); }
    static void sampleAudition (UsedSoundsDialog& dialog)
    { for (size_t i = 0; i < dialog.data->sounds.size(); ++i) if (dialog.data->sounds[i].kind == UsedSounds::Kind::Sample) { dialog.audition (i); return; } }
    static void scrubWhilePlaying (WaveformPlayerComponent& player)
    {
        // Drive the actual audio source manually; no physical device is opened.
        player.metronome.prepareToPlay (256, 44100);
        player.transport.setPosition (0);
        waitForReady (player);
        player.transport.start();
        juce::AudioBuffer<float> block (2, 256); player.metronome.getNextAudioBlock ({ &block, 0, 256 });
        const auto area = player.waveArea;
        const auto point = area.toFloat().getCentre(); const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &player, &player, now, point, now, 1, false);
        player.mouseDown (click);
        if (! player.transport.isPlaying() || std::abs (player.position() - player.duration() * 0.5) > 0.03)
            throw std::runtime_error ("Clicking the timeline stops or fails to seek playback");
        player.mouseDrag (click.withNewPosition (juce::Point<float> { (float) area.getX() + area.getWidth() * 0.75f, point.y }));
        if (! player.transport.isPlaying() || std::abs (player.position() - player.duration() * 0.75) > 0.03)
            throw std::runtime_error ("Scrubbing the timeline stops or fails to seek playback");
        waitForReady (player);
        player.metronome.getNextAudioBlock ({ &block, 0, 256 });
        if (player.position() <= player.duration() * 0.75)
            throw std::runtime_error ("Audio does not continue after seeking");
        player.stop(); player.metronome.releaseResources();
    }
    static void waitForReady (WaveformPlayerComponent& player)
    {
        const auto until = juce::Time::getMillisecondCounter() + 10000;
        player.readerSource->requestPrefill();
        while (! player.readerSource->ready (256))
        {
            if (juce::Time::getMillisecondCounter() > until) throw std::runtime_error ("Timeline seek did not prefill");
            juce::Thread::sleep (2);
        }
    }
    static void captureDrags (UsedSoundsDialog& dialog, std::vector<juce::String>& paths, bool& mayMove)
    {
        dialog.startFileDrag = [&paths, &mayMove] (const juce::StringArray& files, bool move, juce::Component*, std::function<void()> done)
        {
            for (const auto& file : files) paths.push_back (file);
            mayMove = move; if (done) done(); return true;
        };
    }
};

namespace
{
void check (bool pass, const char* reason) { if (! pass) throw std::runtime_error (reason); }
template<class Condition> void waitFor (Condition condition, const char* reason)
{
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (! condition())
    {
        check (juce::Time::getMillisecondCounter() < deadline, reason);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
}
void number (juce::MemoryOutputStream& stream, int id, int value)
{
    stream.writeByte ((char) id);
    if (id < 64) stream.writeByte ((char) value);
    else if (id < 128) stream.writeShort ((short) value);
    else stream.writeInt (value);
}
void blob (juce::MemoryOutputStream& stream, int id, const void* data, size_t size)
{
    stream.writeByte ((char) id);
    auto n = size;
    do { auto b = n & 127; n >>= 7; stream.writeByte ((char) (b | (n > 0 ? 128 : 0))); } while (n > 0);
    stream.write (data, size);
}
void text (juce::MemoryOutputStream& stream, int id, const juce::String& value, bool unicode = true)
{
    juce::MemoryOutputStream bytes;
    if (unicode)
    { for (auto c : value) bytes.writeShort ((short) c); bytes.writeShort (0); }
    else bytes.write (value.toRawUTF8(), value.getNumBytesAsUTF8() + 1);
    blob (stream, id, bytes.getData(), bytes.getDataSize());
}
void note (juce::MemoryOutputStream& stream, int key, int channel, int position, int length)
{
    stream.writeInt (position); stream.writeShort (0); stream.writeShort ((short) channel);
    stream.writeInt (length); stream.writeShort ((short) key); stream.writeShort (0);
    for (int value : { 120, 0, 64, 0, 64, 100, 128, 128 }) stream.writeByte ((char) value);
}
juce::File fixture (const juce::File& folder, int major = 21, int playlistClips = 1, int milliBpm = 128500)
{
    const auto file = folder.getChildFile (juce::String::fromUTF8 ("Canción de prueba.flp"));
    juce::MemoryOutputStream data;
    text (data, 199, juce::String (major) + ".2.3", false);
    if (major >= 25)
    {
        data.writeByte ((char) 172); data.writeByte (1); data.writeByte (1); data.writeByte (0);
        text (data, 192, "FL Studio " + juce::String (major));
    }
    number (data, 156, milliBpm);
    number (data, 64, 0); number (data, 21, 0); text (data, 203, "Piano sample");
    text (data, 201, ""); text (data, 196, "Samples/tone.wav"); number (data, 135, 60);
    number (data, 64, 7); number (data, 21, 4); text (data, 203, juce::String::fromUTF8 ("Lead sintético")); text (data, 201, "Fruity Wrapper");
    juce::MemoryOutputStream vst; vst.writeInt (10); vst.writeInt (54); vst.writeInt64 (6); vst.write ("Serum\0", 6);
    blob (data, 213, vst.getData(), vst.getDataSize());
    number (data, 64, 8); number (data, 21, 5); text (data, 201, "Automation");
    number (data, 65, 1); juce::MemoryOutputStream notes;
    note (notes, 60, 0, 0, 96); note (notes, 64, 0, 96, 96); note (notes, 67, 7, 0, 192);
    blob (data, 224, notes.getData(), notes.getDataSize());
    number (data, 65, 2); juce::MemoryOutputStream unused; note (unused, 72, 7, 0, 96);
    blob (data, 224, unused.getData(), unused.getDataSize());
    number (data, 65, 1); text (data, 193, juce::String::fromUTF8 ("Melodía")); number (data, 65, 2); text (data, 193, "Unused");
    juce::MemoryOutputStream playlist;
    for (int clip = 0; clip < playlistClips; ++clip)
    {
        playlist.writeInt (clip * 384); playlist.writeShort (20480); playlist.writeShort (20481);
        playlist.writeInt (384); for (int i = 12; i < (major >= 26 ? 88 : major >= 25 ? 80 : 60); ++i) playlist.writeByte (0);
    }
    blob (data, 233, playlist.getData(), playlist.getDataSize());
    // Mixer plugin names must not overwrite the final rack channel's generator.
    juce::MemoryOutputStream insert; insert.writeInt (0); blob (data, 236, insert.getData(), insert.getDataSize());
    text (data, 201, "Fruity Limiter");
    juce::MemoryOutputStream flp; flp.write ("FLhd", 4); flp.writeInt (6); flp.writeShort (0); flp.writeShort (3); flp.writeShort (96);
    flp.write ("FLdt", 4); flp.writeInt ((int) data.getDataSize()); flp.write (data.getData(), data.getDataSize());
    check (file.replaceWithData (flp.getData(), flp.getDataSize()), "Cannot write FLP fixture"); return file;
}
void snapshot (juce::Component& component, const juce::String& name)
{
    auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts"); check (dir.createDirectory().wasOk(), "Snapshot directory");
    juce::FileOutputStream stream (dir.getChildFile (name)); juce::PNGImageFormat png;
    stream.setPosition (0); stream.truncate();
    check (stream.openedOk() && png.writeImageToStream (component.createComponentSnapshot (component.getLocalBounds(), true, 1, juce::SoftwareImageType{}), stream), "Snapshot failed");
}
}
void testUsedSounds (const juce::File& root)
{
    const auto config = LibraryStore::configFile(); const auto savedConfig = config.loadFileAsString(); const bool hadConfig = config.existsAsFile();
    struct Restore
    {
        juce::File file; juce::String contents; bool existed;
        ~Restore() { if (existed) file.replaceWithText (contents); else file.deleteFile(); ProjectScanner::refreshAsync(); }
    } restore { config, savedConfig, hadConfig };
    auto dir = root.getChildFile ("used-sounds"); check (dir.getChildFile ("Samples").createDirectory().wasOk(), "Fixture directory");
    auto sample = dir.getChildFile ("Samples/tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (sample);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (44100).withNumChannels (1).withBitsPerSample (16));
        check (writer != nullptr, "Sample writer"); juce::AudioBuffer<float> tone (1, 44100);
        for (int i = 0; i < 44100; ++i) tone.setSample (0, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 440 * i / 44100)));
        check (writer->writeFromAudioSampleBuffer (tone, 0, 44100), "Sample fixture write");
    }
    const auto file = fixture (dir); const auto original = file.loadFileAsString();
    const auto inventory = UsedSounds::readProject (file);
    ProjectEntry entry; entry.name = juce::String::fromUTF8 ("Canción de prueba / Long project name for Used sounds layout");
    entry.projectPath = file.getFullPathName(); entry.key = "A minor";
    auto bad = dir.getChildFile ("truncated.flp"); juce::MemoryBlock bytes; file.loadFileAsData (bytes);
    check (bad.replaceWithData (bytes.getData(), bytes.getSize() - 1), "Bad fixture");
    check (inventory.result.wasOk(), inventory.result.getErrorMessage().toRawUTF8());
    check (inventory.ppq == 96 && inventory.bpm == 128.5 && inventory.playlistKnown, "Tempo/timebase/playlist");
    check (inventory.sounds.size() == 5, "Unexpected sound count or mixer/automation leaked");
    for (int major : { 25, 26 }) for (int clips : { 1, 3, 20 })
    {
        const auto modernDir = dir.getChildFile ("fl" + juce::String (major) + "-" + juce::String (clips));
        check (modernDir.createDirectory().wasOk(), "Modern fixture directory");
        const auto modern = UsedSounds::readProject (fixture (modernDir, major, clips));
        check (modern.result.wasOk() && modern.bpm == 128.5 && modern.sounds.size() == 5, "FL 25/26 three-byte flag or playlist layout failed");
        int usedModern = 0; for (const auto& sound : modern.sounds) if (sound.inPlaylist) ++usedModern;
        check (usedModern == 4, "FL 25/26 playlist stride picked phantom used patterns");
    }
    {
        auto failedEntry = entry; failedEntry.projectPath = bad.getFullPathName();
        UsedSoundsDialog dialog (failedEntry);
        const auto until = juce::Time::getMillisecondCounter() + 10000;
        while (! dialog.hasLoaded() && juce::Time::getMillisecondCounter() < until) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        check (dialog.hasLoaded() && dialog.inventory().result.failed() && ! UsedSoundsTestAccess::list (dialog).isVisible(), "Read failure hidden by empty list");
        for (auto* child : dialog.getChildren()) if (auto* button = dynamic_cast<juce::TextButton*> (child))
            check (! button->getButtonText().contains ("(0)"), "A read failure is presented as zero sounds");
        dialog.setSize (640, 480); snapshot (dialog, "used-sounds-error-minimum.png");
        dialog.setSize (1024, 768); snapshot (dialog, "used-sounds-error-normal.png");
    }
    {
        const auto noTempoDir = dir.getChildFile ("no-tempo"); check (noTempoDir.createDirectory().wasOk(), "No-tempo fixture directory");
        auto noTempoEntry = entry; noTempoEntry.projectPath = fixture (noTempoDir, 26, 1, 0).getFullPathName();
        UsedSoundsDialog dialog (noTempoEntry);
        const auto until = juce::Time::getMillisecondCounter() + 10000;
        while (! dialog.hasLoaded() && juce::Time::getMillisecondCounter() < until) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        check (dialog.hasLoaded() && dialog.inventory().result.wasOk() && dialog.inventory().sounds.size() == 5, "MIDI export failure erased the inventory");
        int failedExports = 0; for (const auto& sound : dialog.inventory().sounds) if (sound.exportError.isNotEmpty()) ++failedExports;
        check (failedExports == 3, "Missing tempo was not explained per MIDI export");
    }
    int used = 0; const UsedSounds::Sound* midi = nullptr; bool serum = false;
    for (const auto& sound : inventory.sounds)
    {
        if (sound.inPlaylist) ++used;
        if (sound.kind == UsedSounds::Kind::Midi && sound.channelId == 0) midi = &sound;
        if (sound.kind == UsedSounds::Kind::Generator) serum = sound.name == "Serum";
    }
    check (used == 4 && serum && midi && midi->notes.size() == 2 && midi->sample == sample, "Association or usage wrong");
    std::cout << "Pattern name: " << midi->patternName << "; note beat: " << midi->notes[1].beat << "; version: " << inventory.version << "\n";
    check (midi->patternName == juce::String::fromUTF8 ("Melodía"), "Unicode pattern name wrong");
    check (midi->notes[1].beat == 1, "Note timing wrong");
    const auto filename = UsedSounds::exportName ("My:project", *midi, 128.5, "F# minor");
    check (filename.contains ("128.50BPM_F# minor") && filename.endsWith (".mid") && ! filename.containsChar (':'), "Export naming");
    const auto exported = dir.getChildFile (filename);
    check (UsedSounds::writeMidi (*midi, 128.5, exported).wasOk(), "MIDI export failed");
    juce::FileInputStream input (exported); juce::MidiFile parsed;
    check (parsed.readFrom (input) && parsed.getNumTracks() == 2 && parsed.getTimeFormat() == 960, "MIDI cannot be read");
    const auto* track = parsed.getTrack (1);
    check (track->getEventPointer (0)->message.isNoteOn() && track->getEventPointer (0)->message.getNoteNumber() == 60, "MIDI pitch wrong");
    check (track->getEndTime() == 1920, "MIDI duration wrong");
    juce::MidiMessageSequence tempos; parsed.findAllTempoEvents (tempos);
    check (tempos.getNumEvents() == 1 && std::abs (tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote() - 60 / 128.5) < 0.000002, "MIDI tempo wrong");
    auto synth = *midi; synth.sourcePath.clear(); synth.sample = {};
    for (int v = 1; v <= 4; ++v)
    {
        auto reader = UsedSounds::createPreviewReader (synth, std::make_shared<const std::vector<UsedSounds::Note>> (synth.notes), 128.5, UsedSounds::voiceFromId (v));
        check (reader && reader->lengthInSamples > 44000 && reader->lengthInSamples < 50000, "Live audition length wrong");
        juce::AudioBuffer<float> decoded (2, 4096), sought (2, 1024);
        for (int at = 0; at < 4096; at += 512) check (reader->read (&decoded, at, 512, at, true, true), "Live block failed");
        check (decoded.getMagnitude (0, 0, 4096) > 0.01f && decoded.getMagnitude (0, 0, 4096) < 1, "Silent/clipping live synth");
        if (v == 4) check (decoded.getMagnitude (0, 0, 4096) > 0.19f, "MIDI audition has not gained volume");
        check (reader->read (&sought, 0, 1024, 1024, true, true), "Live seek failed");
        for (int i = 0; i < 1024; ++i) check (std::abs (decoded.getSample (0, i + 1024) - sought.getSample (0, i)) < 0.00001f, "Seek changed synth phase");
    }
    {
        const auto notes = std::make_shared<const std::vector<UsedSounds::Note>> (midi->notes);
        auto reader = UsedSounds::createPreviewReader (*midi, notes, 128.5, UsedSounds::Voice::Sine);
        check ((bool) reader, "Live sampler unavailable");
        juce::AudioBuffer<float> first (2, 4096), sought (2, 1024);
        check (reader->read (&first, 0, 4096, 0, true, true) && first.getMagnitude (0, 0, 4096) > 0.01f, "Live sampler silent");
        check (reader->read (&sought, 0, 1024, 1024, true, true), "Sampler seek failed");
        for (int i = 0; i < 1024; ++i) check (std::abs (first.getSample (0, i + 1024) - sought.getSample (0, i)) < 0.00001f, "Sampler seek changed sample position");
        auto missing = *midi; missing.sample = {};
        check (! UsedSounds::createPreviewReader (missing, notes, 128.5, UsedSounds::Voice::Sine), "Missing sample silently replaced");
    }
    {
        auto notes = std::make_shared<std::vector<UsedSounds::Note>>();
        for (int key = 36; key < 100; ++key) notes->push_back ({ 0, 8, key, 70, 1 });
        auto reader = UsedSounds::createPreviewReader (synth, notes, 128.5, UsedSounds::Voice::Saw);
        juce::AudioBuffer<float> block (2, 512);
        const auto began = juce::Time::getMillisecondCounterHiRes();
        for (int at = 0; at < 44100; at += 512)
            check (reader->read (&block, 0, 512, at, true, true) && std::isfinite (block.getSample (0, 511)), "64-voice live synthesis failed");
        std::cout << "64-voice saw: one second synthesized in " << juce::Time::getMillisecondCounterHiRes() - began << " ms\n";
    }
    check (UsedSounds::readProject (file, [] { return true; }).result.failed(), "Cancellation ignored");
    check (UsedSounds::readProject (bad).result.failed(), "Truncated file accepted");
    auto wrong = dir.getChildFile ("wrong.als"); wrong.replaceWithText ("not supported");
    check (UsedSounds::readProject (wrong).result.failed(), "Ableton falsely accepted");
    check (UsedSounds::resolveSample ("%FLStudioFactoryData%/tone.wav", file, {}, { sample.getParentDirectory() }) == sample, "Factory token wrong");
    check (file.loadFileAsString() == original, "Source project changed");
    check (ProjectScanner::saveSettings (dir, { dir }, 60, 4, false, nullptr, 4).wasOk()
        && ProjectScanner::readSettings().midiPreviewVoice == 4, "MIDI voice was not saved");
    check (ProjectScanner::saveSettings (dir, { dir }, 30, 4, false).wasOk()
        && ProjectScanner::readSettings().midiPreviewVoice == 4, "Unrelated settings discarded MIDI voice");
    {
        SettingsDialog settings (false); bool committed = false; settings.onCompleted = [&] { committed = true; };
        const auto until = juce::Time::getMillisecondCounter() + 10000;
        while (! settings.hasLoadedSettings() && juce::Time::getMillisecondCounter() < until) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        juce::ComboBox* voice = nullptr; juce::TextButton* ok = nullptr;
        for (auto* child : settings.getChildren())
        {
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child); combo && combo->getTitle() == "Default MIDI audition instrument") voice = combo;
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "OK") ok = button;
        }
        check (settings.hasLoadedSettings() && voice && ok && voice->getSelectedId() == 4, "Settings MIDI voice did not load");
        voice->setSelectedId (2, juce::dontSendNotification); ok->onClick();
        while (! committed && juce::Time::getMillisecondCounter() < until) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        check (committed && ProjectScanner::readSettings().midiPreviewVoice == 2, "Settings MIDI voice commit failed");
        snapshot (settings, "used-sounds-settings.png");
    }
    // Exercise shipped, real Image-Line projects when FL is installed on the validation host.
    for (const auto& path : { "C:/Program Files/Image-Line/FL Studio 2026/Data/Demo projects/NewStuff.flp",
                              "C:/Program Files/Image-Line/FL Studio 21/Data/Templates/LIVE.flp" })
    {
        const juce::File realProject (path);
        if (! realProject.existsAsFile()) continue;
        const auto real = UsedSounds::readProject (realProject);
        std::cout << "Real FLP " << realProject.getFileName() << ": " << real.sounds.size() << " sounds; " << real.result.getErrorMessage() << "\n";
        check (real.result.wasOk(), "Installed FL Studio project failed to parse");
    }
    {
        std::vector<juce::String> draggedFiles; bool mayMove = true;
        int clicks = 0;
        UsedSoundsDialog dialog (entry, [&] (const ProjectEntry&) { ++clicks; });
        const auto until = juce::Time::getMillisecondCounter() + 10000;
        while (! dialog.hasLoaded() && juce::Time::getMillisecondCounter() < until) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        check (dialog.hasLoaded() && dialog.inventory().result.wasOk(), "Dialog inventory did not load");
        UsedSoundsTestAccess::captureDrags (dialog, draggedFiles, mayMove);
        const auto dragFrom = [&] (juce::Component& source, juce::Point<float> point)
        {
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &source, &source, now, point, now, 1, false);
            source.mouseDown (down); const auto before = draggedFiles.size();
            source.mouseDrag (down.withNewPosition (point.translated (2, 0)));
            check (draggedFiles.size() == before, "A click started a file drag before the movement threshold");
            source.mouseDrag (down.withNewPosition (point.translated (20, 0)));
            source.mouseDrag (down.withNewPosition (point.translated (30, 0)));
            source.mouseUp (down.withNewPosition (point.translated (30, 0)));
            check (draggedFiles.size() == before + 1 && ! mayMove, "Row drag did not export exactly one copy");
        };
        const auto clickRow = [&] (juce::Component& source, juce::Point<float> point)
        {
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &source, &source, now, point, now, 1, false);
            const int before = clicks; source.mouseDown (event); source.mouseUp (event);
            waitFor ([&] { return clicks == before + 1; }, "Clicking a sound row did not audition it");
        };
        dialog.setCategory (UsedSounds::Kind::Sample);
        auto* sampleRow = UsedSoundsTestAccess::list (dialog).getComponentForRowNumber (0);
        check (sampleRow, "Sample row missing"); clickRow (*sampleRow, { 100, 24 }); dragFrom (*sampleRow, { 100, 24 });
        check (juce::File (draggedFiles.back()) == sample && sample.existsAsFile(), "Dragging the sample name exported the wrong source");
        juce::TextButton* dragButton = nullptr;
        juce::TextButton* saveButton = nullptr;
        for (auto* child : sampleRow->getChildren()) if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Drag") dragButton = button;
        for (auto* child : sampleRow->getChildren()) if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle() == "Save a copy") saveButton = button;
        check (saveButton && dragButton && sampleRow->getHeight() == RememberUI::soundRowHeight
            && sampleRow->getHeight() < RememberUI::midiSoundRowHeight
            && dragButton->getY() == saveButton->getY() && dragButton->getRight() + 8 == saveButton->getX(), "Sample rows are not compact with horizontal actions");
        check (dragButton, "Drag button missing"); dragFrom (*dragButton, { 20, 15 });
        check (juce::File (draggedFiles.back()) == sample, "Drag button differs from sample-row drag");
        dialog.setCategory (UsedSounds::Kind::Midi);
        auto* midiRow = UsedSoundsTestAccess::list (dialog).getComponentForRowNumber (0);
        check (midiRow, "MIDI row missing"); clickRow (*midiRow, { 100, 84 }); dragFrom (*midiRow, { 100, 84 });
        check (juce::File (draggedFiles.back()).hasFileExtension ("mid") && juce::File (draggedFiles.back()).existsAsFile(), "Piano-roll drag did not export a ready MIDI file");
        check (clicks == 2, "Dragging a row also auditioned it");
        RememberUI::setAppearanceMode (RememberAppearance::Light);
        dialog.setCategory (UsedSounds::Kind::Midi); dialog.setSize (640, 480); snapshot (dialog, "used-sounds-midi-minimum.png");
        dialog.setSize (1024, 768); snapshot (dialog, "used-sounds-midi-normal.png");
        dialog.setCategory (UsedSounds::Kind::Sample); snapshot (dialog, "used-sounds-samples.png");
        dialog.setSize (640, 480); snapshot (dialog, "used-sounds-samples-minimum.png");
        dialog.setSize (1024, 768);
        RememberUI::setAppearanceMode (RememberAppearance::Dark);
        dialog.setCategory (UsedSounds::Kind::Generator);
        check (UsedSoundsTestAccess::list (dialog).getRowHeight() == RememberUI::generatorSoundRowHeight
            && RememberUI::generatorSoundRowHeight < RememberUI::soundRowHeight, "Generators are not compact");
        snapshot (dialog, "used-sounds-generators-dark.png");
        RememberUI::setAppearanceMode (RememberAppearance::System);
    }
    {
        waitFor ([] { return BackgroundRetirement::pending() == 0; }, "Readers did not retire");
        const auto baseline = ReadAheadSource::processBufferBytes();
        WaveformPlayerComponent projectPlayer (nullptr, false);
        int played = 0;
        UsedSoundsDialog dialog (entry, [&] (const ProjectEntry& preview)
        {
            ++played;
            check ((bool) preview.auditionReader && preview.previewOgg == juce::File{}, "MIDI audition creates an audio file");
        }, {}, {}, &projectPlayer, false);
        UsedSoundsTestAccess::noAutoPlay (dialog);
        auto& player = UsedSoundsTestAccess::player (dialog);
        check (&player != &projectPlayer && player.getParentComponent() == &dialog && projectPlayer.getParentComponent() == nullptr,
            "Used sounds moves the main player rather than using its own timeline");
        waitFor ([&] { return dialog.hasLoaded(); }, "Inventory did not load");
        dialog.setCategory (UsedSounds::Kind::Midi);
        check (played == 0 && ReadAheadSource::processBufferBytes() == baseline, "Browsing generates audio");
        std::vector<size_t> indices;
        for (size_t i = 0; i < dialog.inventory().sounds.size(); ++i)
            if (dialog.inventory().sounds[i].kind == UsedSounds::Kind::Midi) indices.push_back (i);
        check (indices.size() >= 2, "Fixture lacks MIDI");
        for (size_t i = 0; i < indices.size(); ++i)
        {
            UsedSoundsTestAccess::audition (dialog, indices[i]);
            waitFor ([&] { return played == (int) i + 1 && player.hasLoadedEntry() && BackgroundRetirement::pending() == 0; }, "Live MIDI did not load");
            check (ReadAheadSource::processBufferBytes() - baseline <= 100 * 1024, "Live MIDI ring exceeds bounded budget");
            check (! player.isAudioDeviceOpen(), "Test opened device");
            UsedSoundsTestAccess::scrubWhilePlaying (player);
            check (! player.isAudioDeviceOpen(), "Seeking opens a device");
            if (i == 0)
            {
                dialog.setSize (640, 480); snapshot (dialog, "used-sounds-transport-midi-minimum.png");
                dialog.setSize (1024, 768); snapshot (dialog, "used-sounds-transport-midi-normal.png");
            }

        }
        UsedSoundsTestAccess::stop (dialog);
        waitFor ([&] { return BackgroundRetirement::pending() == 0 && ReadAheadSource::processBufferBytes() == baseline; }, "Stop retained audio buffers");
        for (int i = 0; i < 20; ++i) UsedSoundsTestAccess::audition (dialog, indices[(size_t) i % indices.size()]);
        UsedSoundsTestAccess::stop (dialog);
        waitFor ([] { return ProjectScanner::pendingJobs() == 0 && WaveformPlayerComponent::pendingLoads() == 0 && BackgroundRetirement::pending() == 0; }, "Cancelled requests did not drain");
        check (played == (int) indices.size() && ReadAheadSource::processBufferBytes() == baseline, "Cancelled requests played stale audio");
    }
    {
        // Keep the main project waveform, controls and bounds throughout audition.
        juce::Component parent; WaveformPlayerComponent player (nullptr, false);
        parent.addAndMakeVisible (player); player.setBounds (0, 0, 640, 190);
        const auto originalBounds = player.getBounds();
        auto previous = entry; previous.previewOgg = sample; previous.metadataFile = dir.getChildFile ("other-project.json");
        player.loadEntry (previous);
        waitFor ([&] { return player.hasLoadedEntry(); }, "Previous preview did not load");
        {
            UsedSoundsDialog dialog (entry, {}, {}, {}, &player, false);
            UsedSoundsTestAccess::noAutoPlay (dialog);
            auto& soundPlayer = UsedSoundsTestAccess::player (dialog);
            check (player.hasLoadedEntry() && ! soundPlayer.hasLoadedEntry() && player.getParentComponent() == &parent
                && player.getBounds() == originalBounds, "Opening Used sounds removes or clears the main preview");
            waitFor ([&] { return dialog.hasLoaded(); }, "Sample inventory did not load");
            UsedSoundsTestAccess::sampleAudition (dialog);
            waitFor ([&] { return soundPlayer.hasLoadedEntry() && ProjectScanner::pendingJobs() == 0 && WaveformPlayerComponent::pendingLoads() == 0; }, "Sample did not load");
            check (soundPlayer.getOutputGain() == 1.0f, "Sample audition volume changed");
            UsedSoundsTestAccess::scrubWhilePlaying (soundPlayer);
            check (player.hasLoadedEntry() && UsedSoundsTestAccess::hasWaveform (player) && player.getParentComponent() == &parent
                && player.getBounds() == originalBounds, "Audition replaces the main project waveform");
            dialog.setSize (640, 480); snapshot (dialog, "used-sounds-transport-sample-minimum.png");
            snapshot (player, "used-sounds-main-retained-minimum.png");
            dialog.setSize (1024, 768); snapshot (dialog, "used-sounds-transport-sample-normal.png");
            player.setSize (1024, 190); snapshot (player, "used-sounds-main-retained-normal.png");
            player.setBounds (originalBounds);
        }
        check (player.getParentComponent() == &parent && player.getBounds() == originalBounds, "Closing Used sounds changed the main player");
    }
    std::cout << "PASS: FLP samples/generators/patterns/usage/unicode, MIDI export tempo/pitch/timing, four audition voices, sampler, sample-row/button and piano-roll drag, malformed/cancelled input, native component snapshots\n";
}
