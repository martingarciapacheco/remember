#include "../Shared/PreviewAudio.h"
#include "PreviewQualityTests.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Shared/LibraryStore.h"
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double rate = 48000;
constexpr int frames = 57600;
void check (bool ok, const juce::String& message)
{
    if (! ok) throw std::runtime_error (message.toStdString());
}
struct Head : juce::AudioPlayHead
{
    bool playing = true;
    double time = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing); info.setTimeInSeconds (time); info.setBpm (120);
        return info;
    }
};
float signal (int sample, bool square)
{
    const auto sine = std::sin (juce::MathConstants<double>::twoPi * 997 * sample / rate);
    return square ? (sine >= 0 ? 0.99f : -0.99f) : (float) (1.5 * sine);
}
void capture (RecordingEngine& engine, bool square, bool mono)
{
    Head head;
    for (int offset = 0; offset < frames;)
    {
        const auto count = juce::jmin (128, frames - offset);
        juce::AudioBuffer<float> audio (2, count);
        for (int i = 0; i < count; ++i)
        {
            const auto value = signal (offset + i, square);
            audio.setSample (0, i, value); audio.setSample (1, i, mono ? value : -value);
        }
        head.time = offset / rate;
        engine.processBlock (audio, &head);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < count; ++i)
                check (audio.getSample (ch, i) == signal (offset + i, square) * (ch == 1 && ! mono ? -1 : 1),
                       "Preview gain modified the DAW's input");
        offset += count;
    }
    head.playing = false; head.time = frames / rate;
    juce::AudioBuffer<float> stopped (2, 128); stopped.clear(); engine.processBlock (stopped, &head);
    const auto until = juce::Time::getMillisecondCounter() + 15000;
    while (engine.getState() != RecordingEngine::State::Pending && engine.getState() != RecordingEngine::State::Error)
    {
        check (juce::Time::getMillisecondCounter() < until, "Preview quality capture timed out");
        juce::Thread::sleep (2);
    }
    check (engine.getState() == RecordingEngine::State::Pending, "Preview quality capture failed: " + engine.getLastError());
}
float decode (const juce::File& file, int channels)
{
    juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->lengthInSamples == frames && (int) reader->numChannels == channels
           && reader->sampleRate == rate, "Preview codec changed length, rate or channels");
    juce::AudioBuffer<float> audio (channels, 4096);
    float peak = 0;
    for (int offset = 0; offset < frames;)
    {
        const auto count = juce::jmin (4096, frames - offset);
        check (reader->read (&audio, 0, count, offset, true, true), "Cannot decode complete quality preview");
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < count; ++i)
            {
                const auto value = audio.getSample (ch, i);
                check (std::isfinite (value), "Codec produced a nonfinite sample");
                peak = juce::jmax (peak, std::abs (value));
            }
        offset += count;
    }
    return peak;
}
juce::var validate (const juce::File& folder, int quality, bool mono, bool square)
{
    const auto metadata = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
    const auto audio = LibraryStore::audioFile (folder, false);
    check ((int) metadata["audioStorageVersion"] == 3 && audio.hasFileExtension ("rmp"), "New capture did not use compressed fragment storage");
    const auto until = juce::Time::getMillisecondCounter() + 5000;
    while (audio.getSiblingFile ("preview.wav").existsAsFile() && juce::Time::getMillisecondCounter() < until) juce::Thread::sleep (2);
    check (! audio.getSiblingFile ("preview.wav").existsAsFile(), "Successful capture retained its temporary WAV");
    const auto pcmPeak = (double) metadata.getProperty ("pcmPeak", -1.0);
    const auto oggPeak = decode (audio, mono ? 1 : 2);
    const auto gain = (double) metadata.getProperty ("previewGain", 0.0);
    const auto passes = (int) metadata.getProperty ("previewGainPasses", 0);
    check (oggPeak <= 1 && gain > 0 && gain <= 1 && passes >= 1 && passes <= 3,
           "Decoded preview exceeded unity or gain policy is invalid");
    check (std::abs (oggPeak - (double) metadata.getProperty ("previewDecodedPeak", -1.0)) < 1.0e-6
           && metadata.getProperty ("previewGainPolicy", "").toString() == "segmented-source-peak-guard-v1",
           "Peak metadata does not describe the committed codec output");
    const auto integrity = LibraryStore::recoverEntry (folder);
    check (integrity.result.wasOk() && ! integrity.repaired, "Validated preview has an invalid integrity receipt");
    if (square)
        check (gain < 1 && passes > 1 && (juce::int64) metadata.getProperty ("samplesAboveUnity", -1) == 0,
               "Below-unity square wave did not exercise codec overshoot correction");
    else
        check (pcmPeak > 1 && gain < 1 && (juce::int64) metadata.getProperty ("samplesAboveUnity", 0) > 0,
               "Headroom fixture did not measure and attenuate above-unity input");
    std::cout << "QUALITY q=" << quality << " channels=" << (mono ? 1 : 2) << " shape=" << (square ? "square" : "sine")
              << " pcmPeak=" << pcmPeak << " gain=" << gain << " decodedPeak=" << oggPeak << " passes=" << passes << '\n';
    return metadata;
}
}
void testPreviewQuality (const juce::File& root)
{
    const auto library = root.getChildFile ("quality-library");
    check (library.createDirectory().wasOk(), "Cannot create isolated quality library");
    for (int quality : { 2, 4, 9 })
        for (bool mono : { false, true })
        {
            SettingsManager::Snapshot settings;
            settings.previews = library; settings.seconds = 15; settings.quality = quality; settings.mono = mono;
            RecordingEngine engine ([] { return HostProject{}; }); engine.prepare (rate, 128, settings);
            capture (engine, false, mono);
            const auto folder = library.getChildFile (".pending").getChildFile (engine.getAssociationInfo().id);
            const auto first = validate (folder, quality, mono, false);
            capture (engine, true, mono);
            validate (folder, quality, mono, true);
            // Recovery must restore gain diagnostics from the surviving audio
            // receipt, while preserving unrelated edits on the current entry.
            juce::var edit (new juce::DynamicObject()); edit.getDynamicObject()->setProperty ("favorite", true);
            check (LibraryStore::patchJson (folder.getChildFile ("metadata.json"), edit).wasOk(), "Cannot edit quality fixture");
            const auto damaged = LibraryStore::audioFile (folder, false);
            check (damaged.replaceWithText ("corrupt codec fixture"), "Cannot damage isolated codec fixture");
            const auto recovered = LibraryStore::recoverEntry (folder);
            check (recovered.result.wasOk() && recovered.repaired, "Cannot recover previous validated preview");
            const auto restored = validate (folder, quality, mono, false);
            for (const auto* field : { "previewGain", "previewDecodedPeak", "previewGainPasses", "previewGainPolicy" })
                check (restored.getProperty (field, {}) == first.getProperty (field, {}), "Recovery mixed gain metadata from two revisions");
            check ((bool) restored.getProperty ("favorite", false), "Audio recovery erased an unrelated edit");
        }
    std::cout << "PASS: OGG fragments q2/q4/q9 mono/stereo decoded sample peaks, temporary WAV cleanup, input transparency and gain-aware recovery\n";
}
