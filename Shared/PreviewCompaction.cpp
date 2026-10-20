#include "PreviewCompaction.h"

namespace PreviewCompaction
{
juce::Result encode (const juce::File& source, const juce::File& destination, int quality,
                     Encoding& diagnostics, const std::function<bool()>& cancelled)
{
    const auto stopped = [&] { return cancelled && cancelled(); };
    if (source == destination) return juce::Result::fail ("Compaction requires a new destination");
    auto input = PreviewAudio::open (source);
    if (! input || input->lengthInSamples <= 0 || input->lengthInSamples > input->sampleRate * 120
        || input->numChannels < 1 || input->numChannels > 2)
        return juce::Result::fail ("Cannot read the complete preview for compaction");
    const auto channels = (int) input->numChannels;
    juce::AudioBuffer<float> block (channels, 4096);
    diagnostics = {};
    for (; diagnostics.passes < 3;)
    {
        if (stopped()) return juce::Result::fail ("Compaction cancelled; original audio preserved");
        {
            auto file = std::make_unique<juce::FileOutputStream> (destination);
            if (! file->openedOk() || ! file->setPosition (0) || file->truncate().failed())
                return juce::Result::fail ("Cannot create compacted OGG");
            std::unique_ptr<juce::OutputStream> stream = std::move (file);
            juce::OggVorbisAudioFormat format;
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                .withSampleRate (input->sampleRate).withNumChannels (channels).withBitsPerSample (16)
                .withQualityOptionIndex (juce::jlimit (0, 10, quality)));
            if (! writer) return juce::Result::fail ("Cannot create OGG encoder");
            for (juce::int64 position = 0; position < input->lengthInSamples;)
            {
                if (stopped()) return juce::Result::fail ("Compaction cancelled; original audio preserved");
                const auto count = (int) juce::jmin<juce::int64> (4096, input->lengthInSamples - position);
                if (! input->read (&block, 0, count, position, true, true)) return juce::Result::fail ("Cannot decode preview fragments");
                for (int ch = 0; ch < channels; ++ch) for (int i = 0; i < count; ++i)
                    if (! std::isfinite (block.getSample (ch, i))) return juce::Result::fail ("Preview contains invalid samples");
                block.applyGain (diagnostics.gain);
                if (! writer->writeFromAudioSampleBuffer (block, 0, count)) return juce::Result::fail ("Cannot write compacted OGG");
                position += count;
            }
        }
        ++diagnostics.passes;
        auto decoded = PreviewAudio::open (destination);
        if (! decoded || decoded->lengthInSamples != input->lengthInSamples || decoded->sampleRate != input->sampleRate
            || decoded->numChannels != input->numChannels) return juce::Result::fail ("Compacted audio format or duration changed");
        diagnostics.peak = 0;
        for (juce::int64 position = 0; position < decoded->lengthInSamples;)
        {
            if (stopped()) return juce::Result::fail ("Compaction cancelled; original audio preserved");
            const auto count = (int) juce::jmin<juce::int64> (4096, decoded->lengthInSamples - position);
            if (! decoded->read (&block, 0, count, position, true, true)) return juce::Result::fail ("Cannot validate compacted OGG");
            for (int ch = 0; ch < channels; ++ch) for (int i = 0; i < count; ++i)
            {
                const auto value = block.getSample (ch, i);
                if (! std::isfinite (value)) return juce::Result::fail ("Compacted OGG contains invalid samples");
                diagnostics.peak = juce::jmax (diagnostics.peak, std::abs (value));
            }
            position += count;
        }
        if (diagnostics.peak <= 1) return juce::Result::ok();
        diagnostics.gain *= 0.98f / diagnostics.peak;
    }
    return juce::Result::fail ("Compacted OGG exceeds unity; original audio preserved");
}
}
