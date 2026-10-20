#include "MetronomeSource.h"
#include <RememberAudioData.h>

MetronomeSource::MetronomeSource (juce::AudioTransportSource& source) : transport (source)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (
        new juce::MemoryInputStream (RememberAudioData::Metronome4_wav, RememberAudioData::Metronome4_wavSize, false), true));
    if (! reader || reader->sampleRate <= 0 || reader->lengthInSamples <= 0 || reader->lengthInSamples > reader->sampleRate * 2) return;
    clickRate = reader->sampleRate;
    click.setSize (2, (int) reader->lengthInSamples);
    if (! reader->read (&click, 0, click.getNumSamples(), 0, true, true)) { click.setSize (0, 0); clickRate = 0; }
}
void MetronomeSource::prepareToPlay (int frames, double rate) { transport.prepareToPlay (frames, rate); }
void MetronomeSource::releaseResources() { transport.releaseResources(); }
void MetronomeSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    const auto before = transport.getCurrentPosition();
    const auto playing = transport.isPlaying();
    transport.getNextAudioBlock (info);
    const auto after = transport.getCurrentPosition();
    // A stalled read-ahead source must not advance an independent metronome.
    if (playing && after > before && info.numSamples > 0)
        mixClick (info, before, (after - before) / info.numSamples);
}
void MetronomeSource::mixClick (const juce::AudioSourceChannelInfo& info, double start, double step) const
{
    const auto tempo = bpm.load();
    const auto gain = volume.load() * master.load();
    if (! enabled.load() || tempo <= 0 || gain <= 0 || clickRate <= 0 || ! info.buffer || info.numSamples <= 0 || step <= 0) return;
    const auto beatSeconds = 60.0 / tempo;
    for (int i = 0; i < info.numSamples; ++i)
    {
        const auto seconds = juce::jmax (0.0, start + i * step);
        // Round near-exact beat boundaries to avoid skipping the first tick frame
        // after a seek or a different host buffer size.
        const auto beats = seconds / beatSeconds;
        auto phase = beats - std::floor (beats + 1.0e-10);
        if (phase < 0) phase = 0;
        const auto position = phase * beatSeconds * clickRate;
        if (position >= click.getNumSamples() - 1) continue;
        const int a = (int) position; const auto fraction = (float) (position - a);
        for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
        {
            const auto* data = click.getReadPointer (juce::jmin (1, ch));
            info.buffer->addSample (ch, info.startSample + i, gain * (data[a] + fraction * (data[a + 1] - data[a])));
        }
    }
}
