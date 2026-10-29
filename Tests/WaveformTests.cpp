#include "WaveformTests.h"
#include "../App/Source/WaveformPlayerComponent.h"
#include "../App/Source/PreviewExport.h"
#include <iostream>
#include <stdexcept>

struct WaveformTestAccess
{
    static bool ready (WaveformPlayerComponent& player) { return player.waveform && player.dragBtn.isEnabled(); }
    static juce::File exported (WaveformPlayerComponent& player) { return player.exportedPreview; }
    static juce::Rectangle<int> area (WaveformPlayerComponent& player) { return player.waveArea; }
    static double position (WaveformPlayerComponent& player) { return player.position(); }
    static juce::Component& drag (WaveformPlayerComponent& player) { return player.dragBtn; }
    static void intercept (WaveformPlayerComponent& player, std::function<bool (const juce::StringArray&, bool, juce::Component*)> action)
    { player.startPreviewDrag = std::move (action); }
    static bool failed (WaveformPlayerComponent& player) { return player.waveformError.isNotEmpty(); }
    static bool pending (WaveformPlayerComponent& player) { return player.playWhenLoaded; }
    static bool unnormalizedCanWaitForDevice (WaveformPlayerComponent& player)
    {
        player.deviceSettingsLoaded = false; player.play();
        const bool waiting = player.waitingForBuffer && ! player.playWhenLoaded && ! player.isAudioDeviceOpen();
        player.stop(); return waiting;
    }
};
namespace
{
void check (bool value, const char* message) { if (! value) throw std::runtime_error (message); }
template<class Condition> void awaitUi (Condition condition)
{
    const auto until = juce::Time::getMillisecondCounter() + 20000;
    while (! condition())
    {
        check (juce::Time::getMillisecondCounter() < until, "Waveform features timed out");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
}
void writeTone (const juce::File& file, double low, double high, int channels = 2)
{
    juce::WavAudioFormat format;
    auto stream = file.createOutputStream();
    check (stream && stream->setPosition (0) && stream->truncate().wasOk(), "Cannot reset spectral fixture");
    std::unique_ptr<juce::OutputStream> output (std::move (stream));
    auto writer = format.createWriterFor (output, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (channels).withBitsPerSample (24));
    check (writer != nullptr, "Cannot create spectral fixture");
    juce::AudioBuffer<float> samples (channels, 48000);
    for (int ch = 0; ch < channels; ++ch) for (int i = 0; i < 48000; ++i)
        samples.setSample (ch, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * (ch == 0 ? low : high) * i / 48000));
    check (writer->writeFromAudioSampleBuffer (samples, 0, 48000), "Cannot write spectral fixture");
}
void snapshot (juce::Component& component, juce::String name)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts");
    check (folder.createDirectory().wasOk(), "Cannot create waveform snapshots");
    juce::FileOutputStream output (folder.getChildFile (name)); juce::PNGImageFormat png;
    check (output.openedOk() && output.setPosition (0) && output.truncate().wasOk()
        && png.writeImageToStream (component.createComponentSnapshot (component.getLocalBounds(), true, 1, juce::SoftwareImageType{}), output), "Cannot save waveform snapshot");
}
juce::MouseEvent mouse (juce::Component& component, juce::Point<float> point, int modifiers)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys (modifiers),
             1, 0, 0, 0, 0, &component, &component, now, point, now, 1, false };
}
}
void testWaveformFeatures (const juce::File& root)
{
    struct Restore
    {
        juce::File config { LibraryStore::configFile() };
        juce::String text { config.loadFileAsString() };
        bool existed = config.existsAsFile();
        ~Restore() { if (existed) config.replaceWithText (text); else config.deleteFile(); }
    } restore;
    const auto library = root.getChildFile ("waveform-library"); check (library.createDirectory().wasOk(), "Cannot create export library");
    ProjectScanner::setPreviewsFolder (library);
    ProjectEntry entry; entry.name = "Frequency guide stereo preview";
    entry.previewOgg = library.getChildFile ("stereo.wav"); entry.metadataFile = library.getChildFile ("entry.json");
    writeTone (entry.previewOgg, 152, 5042);
    const auto malformed = library.getChildFile ("invalid-rate.wav");
    {
        // Independent minimal PCM header: JUCE's writer can put metadata chunks
        // before fmt, so a fixed offset into its output is not the sample rate.
        juce::MemoryOutputStream header;
        header.writeInt (0x46464952); header.writeInt (36 + 192000); header.writeInt (0x45564157);
        header.writeInt (0x20746d66); header.writeInt (16); header.writeShort (1); header.writeShort (2);
        header.writeInt (0x7fffffff); header.writeInt (192000); header.writeShort (4); header.writeShort (16);
        header.writeInt (0x61746164); header.writeInt (192000); header.writeRepeatedByte (0, 192000);
        check (malformed.replaceWithData (header.getData(), header.getDataSize()), "Cannot create malformed WAV sample rate");
    }
    auto malformedReader = PreviewAudio::open (malformed);
    check (malformedReader && malformedReader->sampleRate > 768000, "WAV fixture did not expose its oversized sample rate");
    WaveformEnvelope rejectedEnvelope;
    check (rejectedEnvelope.read (malformed, [] { return false; }).failed()
           && rejectedEnvelope.channels[0].empty() && rejectedEnvelope.channels[1].empty(),
           "Malformed sample rate allocated a waveform/loudness window");
    WaveformEnvelope envelope;
    check (envelope.read (entry.previewOgg, [] { return false; }).wasOk() && envelope.channelCount == 2, "Stereo envelope failed");
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& peak = envelope.channels[(size_t) ch][envelope.channels[0].size() / 2];
        const auto expected = ch == 0 ? 152.0f : 5042.0f;
        check (peak.spectralPower > 0 && std::abs (peak.frequencyMoment / peak.spectralPower - expected) < expected * 0.05f,
            "Spectral colour does not follow channel frequency");
    }
    const auto mono = library.getChildFile ("mono.wav"); writeTone (mono, 495, 0, 1);
    WaveformEnvelope monoEnvelope;
    check (monoEnvelope.read (mono, [] { return false; }).wasOk() && monoEnvelope.channelCount == 1, "Mono gained a second channel");
    for (const auto frequency : { 20.0, 152.0, 495.0, 1569.0, 5042.0, 16000.0 })
    {
        const auto tone = library.getChildFile ("frequency-" + juce::String ((int) frequency) + ".wav");
        writeTone (tone, frequency, 0, 1); WaveformEnvelope analysed;
        check (analysed.read (tone, [] { return false; }).wasOk(), "Frequency guide analysis failed");
        const auto& peak = analysed.channels[0][analysed.channels[0].size() / 2];
        check (peak.spectralPower > 0 && std::abs (peak.frequencyMoment / peak.spectralPower - frequency) < juce::jmax (12.0, frequency * 0.05),
            "Frequency guide landmark is inaccurate");
    }
    juce::File exported, again;
    const auto cancelledLibrary = root.getChildFile ("cancelled-export");
    int hashChecks = 0;
    check (PreviewExport::prepare (entry, cancelledLibrary, exported, [&] { return ++hashChecks >= 4; }).failed()
           && exported == juce::File{} && ! cancelledLibrary.exists(),
           "Export did not cancel during source hashing before creating output directories");
    check (PreviewExport::prepare (entry, library, exported, [] { return false; }).wasOk(), "Cannot prepare WAV copy");
    const auto stamp = exported.getLastModificationTime();
    check (exported.getParentDirectory() == library.getChildFile ("Exports")
        && PreviewExport::prepare (entry, library, again, [] { return false; }).wasOk() && exported == again
        && stamp == exported.getLastModificationTime(), "Export was not persistent and reusable");
    auto original = PreviewAudio::open (entry.previewOgg), copy = PreviewAudio::open (exported);
    check (original && copy && copy->lengthInSamples == 48000 && copy->numChannels == 2 && copy->sampleRate == 48000,
        "WAV lost complete duration or channel format");
    juce::AudioBuffer<float> a (2, 48000), b (2, 48000);
    check (original->read (&a, 0, 48000, 0, true, true) && copy->read (&b, 0, 48000, 0, true, true), "Cannot inspect WAV audio");
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 48000; ++i)
        check (std::abs (a.getSample (ch, i) - b.getSample (ch, i)) < 0.000001f, "WAV export changed source gain/audio");
    writeTone (entry.previewOgg, 495, 1569);
    check (PreviewExport::prepare (entry, library, again, [] { return false; }).wasOk() && again != exported && exported.existsAsFile(),
        "Changed audio overwrote a WAV referenced by a DAW");
    auto changed = PreviewAudio::open (again);
    check (changed && changed->read (&b, 0, 48000, 0, true, true)
        && std::abs (a.getSample (0, 100) - b.getSample (0, 100)) > 0.1f,
        "Changed audio reused the previous WAV contents");
    ProjectEntry package = entry; package.previewOgg = library.getChildFile ("segmented.rmp");
    check (PreviewAudio::write (package.previewOgg, entry.previewOgg, {}, 48000, 2, 48000, 48000, 4, 1).wasOk(), "Cannot create export package");
    check (PreviewExport::prepare (package, library, again, [] { return false; }).wasOk(), "Cannot export segmented preview");
    auto decoded = PreviewAudio::open (package.previewOgg); auto flattened = PreviewAudio::open (again);
    check (decoded && flattened && flattened->lengthInSamples == decoded->lengthInSamples, "RMP export lost duration");
    decoded->read (&a, 0, 48000, 0, true, true); flattened->read (&b, 0, 48000, 0, true, true);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 48000; ++i)
        check (std::abs (a.getSample (ch, i) - b.getSample (ch, i)) < 0.000001f, "RMP WAV does not match decoded preview");
    {
        WaveformPlayerComponent player (nullptr, false), other (nullptr, false);
        other.setCompactMode (true); player.setSize (640, 190); other.setSize (640, 150);
        player.loadEntry (entry); other.loadEntry (entry);
        awaitUi ([&] { return WaveformTestAccess::ready (player) && other.hasLoadedEntry(); });
        check (! player.hasColorfulWaveforms() && ! player.hasSeparateWaveformChannels(), "New waveform options did not start off");
        auto* assign = (juce::Button*) nullptr;
        for (auto* child : player.getChildren()) if (auto* button = dynamic_cast<juce::Button*> (child); button && button->getButtonText() == "Assign path") assign = button;
        check (assign && WaveformTestAccess::drag (player).getRight() < assign->getX(), "Drag is not left of Assign path");
        int drags = 0;
        WaveformTestAccess::intercept (player, [&] (const juce::StringArray& files, bool move, juce::Component*)
        {
            ++drags; check (! move && files.size() == 1 && juce::File (files[0]) == WaveformTestAccess::exported (player), "Drag did not copy complete WAV");
            return true;
        });
        auto& drag = WaveformTestAccess::drag (player); auto down = mouse (drag, { 4, 4 }, juce::ModifierKeys::leftButtonModifier);
        drag.mouseDown (down); drag.mouseDrag (down.withNewPosition (juce::Point<float> { 16, 4 }));
        drag.mouseDrag (down.withNewPosition (juce::Point<float> { 24, 4 })); drag.mouseUp (down);
        check (drags == 1 && ! player.isPlaying() && ! player.isAudioDeviceOpen(), "Drag repeated or triggered playback");
        const auto position = WaveformTestAccess::position (player);
        const auto right = mouse (player, WaveformTestAccess::area (player).toFloat().getCentre(), juce::ModifierKeys::rightButtonModifier);
        player.mouseDown (right); player.mouseDrag (right.withNewPosition (right.position + juce::Point<float> (20, 0)));
        check (WaveformTestAccess::position (player) == position, "Waveform context menu scrubbed playback");
        auto* menu = juce::Component::getCurrentlyModalComponent(); check (menu != nullptr, "Waveform menu is absent");
        snapshot (*menu, "waveform-options.png"); menu->exitModalState (1);
        awaitUi ([&] { return player.hasColorfulWaveforms() && other.hasColorfulWaveforms(); });
        const auto compactClick = mouse (other, WaveformTestAccess::area (other).toFloat().getCentre(), juce::ModifierKeys::rightButtonModifier);
        other.mouseDown (compactClick);
        auto* compactMenu = juce::Component::getCurrentlyModalComponent();
        check (compactMenu != nullptr, "Compact player waveform menu is absent");
        compactMenu->exitModalState (2);
        awaitUi ([&] { return player.hasSeparateWaveformChannels(); });
        for (bool dark : { false, true })
        {
            RememberUI::setAppearanceMode (dark ? RememberAppearance::Dark : RememberAppearance::Light);
            for (int width : { 640, 1024 })
            {
                player.setSize (width, 190); other.setSize (width, 150);
                snapshot (player, "waveform-color-split-" + juce::String (dark ? "dark-" : "light-") + juce::String (width) + ".png");
                snapshot (other, "waveform-compact-color-split-" + juce::String (dark ? "dark-" : "light-") + juce::String (width) + ".png");
            }
        }
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
        const auto saved = LibraryStore::readJson (LibraryStore::configFile());
        check ((bool) saved["colorfulWaveforms"] && (bool) saved["separateWaveformChannels"], "Waveform options were not saved");
        WaveformPlayerComponent reopened (nullptr, false);
        check (reopened.hasColorfulWaveforms() && reopened.hasSeparateWaveformChannels(), "Reopened player lost shared choices");
        player.setColorfulWaveforms (false); player.setSeparateWaveformChannels (false);
        player.clearPreview(); check (! WaveformTestAccess::drag (player).isEnabled(), "Cleared player exports previous audio");
    }
    awaitUi ([] { return WaveformPlayerComponent::pendingLoads() == 0 && ProjectScanner::pendingJobs() == 0; });
    {
        WaveformPlayerComponent player (nullptr, false); player.setCompactMode (true); player.setSize (640, 190);
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
        struct Gate { juce::WaitableEvent entered, release; ~Gate() { release.signal(); } };
        auto gate = std::make_shared<Gate>();
        struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release.signal(); } } release { gate };
        ProjectScanner::queryAsync ([gate] { gate->entered.signal(); gate->release.wait (10000); return juce::Result::ok(); });
        check (gate->entered.wait (2000), "Cannot hold waveform query queue");
        player.setNormalized (true); player.loadEntry (entry); player.play();
        awaitUi ([&] { return player.hasLoadedEntry(); });
        // Decoder/read-ahead is open, while the independent waveform open is
        // queued. Corrupt only the header to reproduce that second-open failure.
        {
            juce::FileOutputStream edit (entry.previewOgg);
            check (edit.openedOk() && edit.setPosition (0) && edit.write ("invalid!", 8), "Cannot simulate second-open failure"); edit.flush();
        }
        gate->release.signal();
        awaitUi ([&] { return WaveformTestAccess::failed (player); });
        const auto previousLanguage = RememberUI::language.load();
        for (auto language : { RememberUI::Language::English, RememberUI::Language::SpanishSpain })
        {
            RememberUI::setLanguage (language);
            check (! WaveformTestAccess::pending (player) && player.getPlaybackError() == RememberUI::localize (
                "Loudness measurement failed. Turn off normalization to play, or select the preview again."),
                "Failed loudness analysis stayed pending or lost its translated error");
            const auto locale = language == RememberUI::Language::English ? "en" : "es";
            snapshot (player, "waveform-error-compact-" + juce::String (locale) + ".png");
            player.setCompactMode (false);
            for (const int width : { 640, 1024 })
            {
                player.setSize (width, 190);
                snapshot (player, "waveform-error-" + juce::String (locale) + "-" + juce::String (width) + ".png");
            }
            player.setSize (640, 190); player.setCompactMode (true);
        }
        RememberUI::setLanguage (previousLanguage);
        player.setNormalized (false);
        check (player.getPlaybackError().isEmpty() && WaveformTestAccess::unnormalizedCanWaitForDevice (player),
               "Turning off normalization did not unblock playback after analysis failure");
        player.clearPreview();
        check (! WaveformTestAccess::failed (player), "Clearing preview retained an old waveform error");
    }
    RememberUI::setAppearanceMode (RememberAppearance::System);
    std::cout << "PASS: frequency analysis, mono/stereo, global saved waveform menu, copy-only WAV drag, immutable/reused Exports, RMP flattening and native snapshots\n";
}
