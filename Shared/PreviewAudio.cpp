#include "PreviewAudio.h"
#include <map>
#include <list>

namespace PreviewAudio
{
namespace
{
constexpr char magic[] = "RMPREV03";
constexpr int maximumIndexBytes = 1024 * 1024;
struct Asset { juce::String hash; juce::int64 offset = 0, bytes = 0, frames = 0; double rate = 0; int channels = 0; juce::File file; };
struct Envelope { double start = 0, end = 0, a = 0, b = 1; };
struct Span { int asset = 0; double start = 0, end = 0, source = 0, gain = 1; std::vector<Envelope> envelopes; };
struct Index { double rate = 0; int channels = 0; juce::int64 frames = 0, payload = 0; std::vector<Asset> assets; std::vector<Span> spans; };
bool finite (double x) { return std::isfinite (x); }
bool load (juce::InputStream& input, Index& index)
{
    if (input.getTotalLength() < 12 || input.getTotalLength() > maximumPackageBytes || ! input.setPosition (0)) return false;
    char signature[8]; if (input.read (signature, 8) != 8 || std::memcmp (signature, magic, 8) != 0) return false;
    const auto size = input.readInt();
    if (size < 2 || size > maximumIndexBytes || size > input.getTotalLength() - 12) return false;
    juce::MemoryBlock text; if (input.readIntoMemoryBlock (text, (size_t) size) != (size_t) size) return false;
    const auto json = juce::JSON::parse (text.toString());
    if ((int) json.getProperty ("version", 0) != 3) return false;
    index.rate = (double) json["rate"]; index.channels = (int) json["channels"]; index.frames = (juce::int64) json["frames"]; index.payload = 12 + size;
    if (! finite (index.rate) || index.rate < 8000 || index.rate > 768000 || index.channels < 1 || index.channels > 2
        || index.frames <= 0 || index.frames > index.rate * 120) return false;
    const auto* assets = json["assets"].getArray(); const auto* spans = json["spans"].getArray();
    if (! assets || ! spans || assets->isEmpty() || assets->size() > maximumSpans || spans->isEmpty() || spans->size() > maximumSpans) return false;
    juce::int64 expected = 0;
    for (const auto& value : *assets)
    {
        Asset asset; asset.hash = value["hash"].toString(); asset.offset = (juce::int64) value["offset"];
        asset.bytes = (juce::int64) value["bytes"]; asset.frames = (juce::int64) value["frames"];
        asset.rate = (double) value["rate"]; asset.channels = (int) value["channels"];
        if (asset.hash.length() != 64 || asset.offset != expected || asset.bytes <= 0 || asset.bytes > maximumPackageBytes
            || asset.bytes > input.getTotalLength() - index.payload - asset.offset || ! finite (asset.rate)
            || asset.rate < 8000 || asset.rate > 768000 || asset.channels < 1 || asset.channels > 2
            || asset.frames <= 0 || asset.frames > asset.rate * 1.1) return false;
        expected += asset.bytes; index.assets.push_back (asset);
    }
    if (index.payload + expected != input.getTotalLength()) return false;
    for (const auto& value : *spans)
    {
        Span span; span.asset = (int) value["asset"]; span.start = (double) value["start"]; span.end = (double) value["end"]; span.source = (double) value["source"];
        span.gain = (double) value.getProperty ("gain", 1.0);
        if (span.asset < 0 || span.asset >= (int) index.assets.size() || ! finite (span.start) || ! finite (span.end) || ! finite (span.source)
            || ! finite (span.gain) || span.gain < 0 || span.gain > 1
            || span.start < 0 || span.end <= span.start || span.end > index.frames / index.rate + 1e-8 || span.source < 0) return false;
        const auto& asset = index.assets[(size_t) span.asset];
        if (span.source + span.end - span.start > asset.frames / asset.rate + 2.0 / asset.rate) return false;
        const auto* envelopes = value["envelopes"].getArray();
        if (! envelopes || envelopes->size() > maximumEnvelopes) return false;
        for (const auto& envelope : *envelopes)
        {
            Envelope e { (double) envelope["start"], (double) envelope["end"], (double) envelope["a"], (double) envelope["b"] };
            if (! finite (e.start) || ! finite (e.end) || ! finite (e.a) || ! finite (e.b) || e.end <= e.start
                || e.a < 0 || e.a > 1 || e.b < 0 || e.b > 1) return false;
            span.envelopes.push_back (e);
        }
        index.spans.push_back (span);
    }
    // Check the envelope partition at every breakpoint. This rejects holes and
    // excessive overlaps before playback; normal seams form a convex mixture.
    std::vector<double> points { 0, index.frames / index.rate };
    for (const auto& s : index.spans) { points.push_back (s.start); points.push_back (s.end); }
    std::sort (points.begin(), points.end());
    for (size_t i = 1; i < points.size(); ++i)
    {
        if (points[i] <= points[i - 1]) continue;
        const auto t = (points[i] + points[i - 1]) * 0.5; int count = 0;
        for (const auto& s : index.spans) if (s.start <= t && t < s.end) ++count;
        if (count == 0 || count > maximumEnvelopes) return false;
    }
    return true;
}
double weight (const Span& span, double time)
{
    double gain = span.gain;
    for (const auto& e : span.envelopes)
        gain *= e.a + (e.b - e.a) * juce::jlimit (0.0, 1.0, (time - e.start) / (e.end - e.start));
    return gain;
}
// Each Vorbis decoder must observe an independent cursor. JUCE's
// SubregionStream exposes its parent's cursor, so it cannot be shared by an
// LRU of decoders even when all calls execute on the same worker.
class AssetStream final : public juce::InputStream
{
public:
    AssetStream (juce::InputStream& stream, juce::int64 first, juce::int64 bytes)
        : source (stream), start (first), length (bytes) {}
    juce::int64 getTotalLength() override { return length; }
    juce::int64 getPosition() override { return position; }
    bool setPosition (juce::int64 next) override
    { if (next < 0 || next > length) return false; position = next; return true; }
    bool isExhausted() override { return position >= length; }
    int read (void* data, int count) override
    {
        if (count <= 0 || position >= length || ! source.setPosition (start + position)) return 0;
        const auto read = source.read (data, (int) juce::jmin<juce::int64> (count, length - position));
        position += read; return read;
    }
private:
    juce::InputStream& source;
    const juce::int64 start, length;
    juce::int64 position = 0;
};
class Reader final : public juce::AudioFormatReader
{
public:
    Reader (juce::InputStream* stream) : AudioFormatReader (stream, "Remember segmented preview")
    {
        if (! load (*input, index)) return;
        sampleRate = index.rate; numChannels = (unsigned int) index.channels; lengthInSamples = index.frames;
        bitsPerSample = 32; usesFloatingPointData = true; ready = true;
    }
    bool valid() const { return ready; }
    bool readSamples (int* const* dest, int countChannels, int destination, juce::int64 position, int count) override
    {
        if (! ready || position < 0 || count < 0) return false;
        for (int ch = 0; ch < countChannels; ++ch) if (dest[ch]) juce::FloatVectorOperations::clear (reinterpret_cast<float*> (dest[ch]) + destination, count);
        // One-second assets decode from frame zero into an 8 MiB/four-asset
        // worker cache. Vorbis random seeks can differ near packet boundaries;
        // canonical decoding makes output independent of reads and seeks.
        for (int block = 0; block < count; block += 2048)
        {
            const int n = juce::jmin (2048, count - block);
            const auto first = position + block, end = juce::jmin<juce::int64> (lengthInSamples, first + n);
            if (end <= first) continue;
            for (const auto& span : index.spans)
            {
                const auto beginFrame = juce::jmax (first, (juce::int64) std::ceil (span.start * sampleRate - 1e-7));
                const auto endFrame = juce::jmin (end, (juce::int64) std::ceil (span.end * sampleRate - 1e-7));
                if (endFrame <= beginFrame) continue;
                const auto* decoded = get (span.asset); if (! decoded) return false;
                const auto& asset = index.assets[(size_t) span.asset];
                const double ratio = asset.rate / sampleRate;
                const auto sourceAt = [&] (juce::int64 frame) { return (span.source + frame / sampleRate - span.start) * asset.rate; };
                const int half = asset.rate == sampleRate ? 0 : (int) std::ceil (32 * juce::jmax (1.0, ratio));
                for (auto frame = beginFrame; frame < endFrame; ++frame)
                {
                    const auto source = sourceAt (frame); const auto centre = (juce::int64) std::llround (source);
                    const auto gain = weight (span, frame / sampleRate);
                    double values[2] {}; double norm = 0;
                    if (half == 0)
                    {
                        if (centre >= 0 && centre < asset.frames)
                            for (int ch = 0; ch < 2; ++ch) values[ch] = decoded->getSample (asset.channels == 1 ? 0 : ch, (int) centre);
                    }
                    else
                    {
                        const auto cutoff = 0.95 / juce::jmax (1.0, ratio);
                        for (int tap = -half; tap <= half; ++tap)
                        {
                            const auto delta = (double) (centre + tap) - source, x = delta / half;
                            if (std::abs (x) >= 1) continue;
                            const auto angle = juce::MathConstants<double>::pi * cutoff * delta;
                            const auto coefficient = cutoff * (std::abs (angle) < 1e-12 ? 1 : std::sin (angle) / angle)
                                * (0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * x) + 0.08 * std::cos (juce::MathConstants<double>::twoPi * x));
                            norm += coefficient; const auto offset = centre + tap;
                            if (offset >= 0 && offset < asset.frames)
                                for (int ch = 0; ch < 2; ++ch) values[ch] += coefficient * decoded->getSample (asset.channels == 1 ? 0 : ch, (int) offset);
                        }
                        if (std::abs (norm) > 1e-12) for (auto& v : values) v /= norm;
                    }
                    if (numChannels == 1) values[0] = (values[0] + values[1]) * 0.5;
                    for (int ch = 0; ch < juce::jmin (countChannels, (int) numChannels); ++ch)
                        if (dest[ch]) reinterpret_cast<float*> (dest[ch])[destination + block + (int) (frame - first)] += (float) (values[ch] * gain);
                }
            }
        }
        return true;
    }
private:
    const juce::AudioBuffer<float>* get (int assetId)
    {
        for (auto it = cache.begin(); it != cache.end(); ++it) if (it->first == assetId)
        { cache.splice (cache.begin(), cache, it); return cache.front().second.get(); }
        const auto& asset = index.assets[(size_t) assetId]; juce::OggVorbisAudioFormat format;
        const auto bytes = (size_t) asset.frames * (size_t) asset.channels * sizeof (float);
        constexpr size_t maximumCacheBytes = 8 * 1024 * 1024;
        if (bytes > maximumCacheBytes) return nullptr;
        while (! cache.empty() && (cache.size() >= 4 || cacheBytes + bytes > maximumCacheBytes))
        {
            const auto& audio = *cache.back().second;
            cacheBytes -= (size_t) audio.getNumSamples() * (size_t) audio.getNumChannels() * sizeof (float);
            cache.pop_back();
        }
        auto stream = std::make_unique<AssetStream> (*input, index.payload + asset.offset, asset.bytes);
        std::unique_ptr<juce::AudioFormatReader> decoder (format.createReaderFor (stream.release(), true));
        if (! decoder || decoder->sampleRate != asset.rate || decoder->numChannels != (unsigned int) asset.channels || decoder->lengthInSamples != asset.frames) return nullptr;
        auto audio = std::make_unique<juce::AudioBuffer<float>> (asset.channels, (int) asset.frames);
        if (! decoder->read (audio.get(), 0, (int) asset.frames, 0, true, true)) return nullptr;
        cacheBytes += bytes; cache.emplace_front (assetId, std::move (audio)); return cache.front().second.get();
    }
    Index index; bool ready = false; size_t cacheBytes = 0;
    std::list<std::pair<int, std::unique_ptr<juce::AudioBuffer<float>>>> cache;
};
class Format final : public juce::AudioFormat
{
public:
    Format() : AudioFormat ("Remember preview", ".rmp") {}
    juce::Array<int> getPossibleSampleRates() override { return { 8000, 44100, 48000, 96000, 192000 }; }
    juce::Array<int> getPossibleBitDepths() override { return { 32 }; }
    bool canDoStereo() override { return true; } bool canDoMono() override { return true; }
    juce::AudioFormatReader* createReaderFor (juce::InputStream* input, bool deleteOnFailure) override
    {
        auto reader = std::make_unique<Reader> (input);
        if (reader->valid()) return reader.release();
        if (! deleteOnFailure) reader->input = nullptr;
        return nullptr;
    }
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor (std::unique_ptr<juce::OutputStream>&, const juce::AudioFormatWriterOptions&) override { return {}; }
};
struct Builder
{
    Index index; juce::File temporary; std::vector<juce::File> cleanup;
    ~Builder() { for (const auto& file : cleanup) file.deleteFile(); }
    int asset (const Asset& a)
    {
        for (size_t i = 0; i < index.assets.size(); ++i) if (index.assets[i].hash == a.hash) return (int) i;
        index.assets.push_back (a); return (int) index.assets.size() - 1;
    }
    bool appendPackage (const juce::File& file, double start, double end, double fadeStart, double fadeEnd, bool fadeIn)
    {
        juce::FileInputStream stream (file); Index old; if (! stream.openedOk() || ! load (stream, old)) return false;
        for (auto span : old.spans)
        {
            // A splice replaces the old prefix, including its outgoing seam.
            // Reuse the incoming tail's source context instead of retaining a
            // growing stack of historical fades at the same cut position.
            if (fadeIn && span.end <= fadeEnd + 1e-9) continue;
            if (fadeIn && span.start <= fadeEnd && span.start > start && span.source >= span.start - start)
            { span.source -= span.start - start; span.start = start; }
            const auto clipped = juce::jmax (start, span.start); span.end = juce::jmin (end, span.end);
            if (span.end <= clipped) continue;
            span.source += clipped - span.start; span.start = clipped;
            auto a = old.assets[(size_t) span.asset]; a.file = file; a.offset += old.payload; span.asset = asset (a);
            span.envelopes.erase (std::remove_if (span.envelopes.begin(), span.envelopes.end(), [&] (const auto& e)
                { return (e.b == 1 && e.end <= (fadeIn ? fadeEnd : span.start)) || (e.a == 1 && e.start >= span.end); }), span.envelopes.end());
            if (fadeEnd > fadeStart && span.start < fadeEnd && span.end > fadeStart)
                span.envelopes.push_back ({ fadeStart, fadeEnd, fadeIn ? 0.0 : 1.0, fadeIn ? 1.0 : 0.0 });
            if (span.envelopes.size() > maximumEnvelopes) return false;
            index.spans.push_back (std::move (span));
        }
        return true;
    }
    bool encode (const juce::File& source, double start, double end, int quality, float gain, double outerFadeStart, double outerFadeEnd, bool fadeIn,
                 const std::function<bool()>& cancelled)
    {
        auto reader = open (source); if (! reader) return false;
        const auto rate = reader->sampleRate; const int channels = index.channels;
        // One-second immutable assets with 20 ms context on either side.
        for (double cursor = start; cursor < end - 1e-9; cursor += 1)
        {
            if (cancelled && cancelled()) return false;
            const auto spanEnd = juce::jmin (end, cursor + 1.0);
            const auto spanStart = cursor == start ? cursor : cursor - 0.005;
            const auto first = juce::jmax<juce::int64> (0, (juce::int64) std::floor ((spanStart - 0.02) * rate));
            const auto last = juce::jmin (reader->lengthInSamples, (juce::int64) std::ceil ((spanEnd + 0.02) * rate));
            if (last <= first) return false;
            const auto file = temporary.getSiblingFile (temporary.getFileName() + "-" + juce::Uuid().toString() + ".ogg"); cleanup.push_back (file);
            juce::OggVorbisAudioFormat ogg; std::unique_ptr<juce::OutputStream> output (file.createOutputStream());
            auto writer = ogg.createWriterFor (output, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (channels)
                .withBitsPerSample (16).withQualityOptionIndex (quality));
            if (! writer) return false;
            juce::AudioBuffer<float> block (2, 4096), encoded (channels, 4096);
            for (auto pos = first; pos < last;)
            {
                if (cancelled && cancelled()) return false;
                const auto count = (int) juce::jmin<juce::int64> (4096, last - pos);
                if (! reader->read (&block, 0, count, pos, true, true)) return false;
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < count; ++i)
                    {
                        const auto value = gain * (channels == 1 && reader->numChannels > 1
                            ? 0.5f * (block.getSample (0, i) + block.getSample (1, i)) : block.getSample (reader->numChannels == 1 ? 0 : ch, i));
                        encoded.setSample (ch, i, std::isfinite (value) ? value : 0);
                    }
                if (! writer->writeFromAudioSampleBuffer (encoded, 0, count)) return false; pos += count;
            }
            writer.reset();
            Asset a; a.file = file; a.bytes = file.getSize(); a.rate = rate; a.channels = channels; a.frames = last - first;
            juce::FileInputStream bytes (file); a.hash = juce::SHA256 (bytes).toHexString();
            Span s; s.asset = asset (a); s.start = spanStart; s.end = spanEnd; s.source = spanStart - first / rate;
            if (cursor > start)
            {
                s.envelopes.push_back ({ spanStart, cursor, 0, 1 });
                index.spans.back().envelopes.push_back ({ spanStart, cursor, 1, 0 });
            }
            if (outerFadeEnd > outerFadeStart) s.envelopes.push_back ({ outerFadeStart, outerFadeEnd, fadeIn ? 0.0 : 1.0, fadeIn ? 1.0 : 0.0 });
            index.spans.push_back (std::move (s));
        }
        return true;
    }
    juce::Result save (const juce::File& destination, const std::function<bool()>& cancelled)
    {
        if (index.spans.empty() || index.spans.size() > maximumSpans || index.assets.size() > maximumSpans)
            return juce::Result::fail ("Preview fragment limit reached. Capture the full preview or enable Replace whole previews; the previous preview is preserved");
        juce::var json (new juce::DynamicObject()); auto* obj = json.getDynamicObject();
        obj->setProperty ("version", 3); obj->setProperty ("rate", index.rate); obj->setProperty ("channels", index.channels); obj->setProperty ("frames", index.frames);
        juce::Array<juce::var> assets, spans; juce::int64 offset = 0;
        for (const auto& a : index.assets)
        {
            juce::var v (new juce::DynamicObject()); auto* o = v.getDynamicObject(); o->setProperty ("hash", a.hash); o->setProperty ("offset", offset);
            o->setProperty ("bytes", a.bytes); o->setProperty ("frames", a.frames); o->setProperty ("rate", a.rate); o->setProperty ("channels", a.channels); assets.add (v); offset += a.bytes;
        }
        for (const auto& s : index.spans)
        {
            if (s.envelopes.size() > maximumEnvelopes) return juce::Result::fail ("Preview crossfade limit reached. Capture the full preview or use full replacement");
            juce::var v (new juce::DynamicObject()); auto* o = v.getDynamicObject(); o->setProperty ("asset", s.asset); o->setProperty ("start", s.start); o->setProperty ("end", s.end); o->setProperty ("source", s.source);
            o->setProperty ("gain", s.gain);
            juce::Array<juce::var> envelopes;
            for (const auto& e : s.envelopes) { juce::var x (new juce::DynamicObject()); auto* p = x.getDynamicObject(); p->setProperty ("start", e.start); p->setProperty ("end", e.end); p->setProperty ("a", e.a); p->setProperty ("b", e.b); envelopes.add (x); }
            o->setProperty ("envelopes", envelopes); spans.add (v);
        }
        obj->setProperty ("assets", assets); obj->setProperty ("spans", spans); const auto text = juce::JSON::toString (json, true);
        if (text.getNumBytesAsUTF8() > maximumIndexBytes || offset + text.getNumBytesAsUTF8() + 12 > maximumPackageBytes)
            return juce::Result::fail ("Preview storage limit reached. Capture the full preview or use full replacement; old audio was preserved");
        {
            juce::FileOutputStream output (destination); if (! output.openedOk() || ! output.setPosition (0) || output.truncate().failed()) return juce::Result::fail ("Cannot create preview package");
            if (! output.write (magic, 8) || ! output.writeInt ((int) text.getNumBytesAsUTF8()) || ! output.write (text.toRawUTF8(), text.getNumBytesAsUTF8())) return juce::Result::fail ("Cannot write preview index");
            for (const auto& a : index.assets)
            {
                if (cancelled && cancelled()) return juce::Result::fail ("Preview encoding cancelled");
                juce::FileInputStream input (a.file); if (! input.openedOk() || ! input.setPosition (a.offset) || output.writeFromInputStream (input, a.bytes) != a.bytes) return juce::Result::fail ("Cannot copy immutable OGG fragment");
            }
            output.flush(); if (output.getStatus().failed()) return output.getStatus();
        }
        return verify (destination, cancelled);
    }
};
}
bool isPackage (const juce::File& file) { return file.hasFileExtension ("rmp"); }
void registerFormats (juce::AudioFormatManager& formats) { formats.registerBasicFormats(); formats.registerFormat (new Format(), false); }
std::unique_ptr<juce::AudioFormatReader> open (const juce::File& file)
{ juce::AudioFormatManager formats; registerFormats (formats); return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)); }
juce::Result verify (const juce::File& file, const std::function<bool()>& cancelled)
{
    juce::FileInputStream stream (file); Index index; if (! stream.openedOk() || ! load (stream, index)) return juce::Result::fail ("Invalid or incomplete preview package");
    for (const auto& a : index.assets)
    {
        if (cancelled && cancelled()) return juce::Result::fail ("Preview validation cancelled");
        juce::SubregionStream bytes (&stream, index.payload + a.offset, a.bytes, false);
        if (juce::SHA256 (bytes).toHexString() != a.hash) return juce::Result::fail ("Preview fragment checksum mismatch");
    }
    return juce::Result::ok();
}
juce::Result write (const juce::File& destination, const juce::File& captured, const juce::File& previous, double rate, int channels,
                    int prefixFrames, int totalFrames, int quality, float newGain, const std::function<bool()>& cancelled, float outputGain)
{
    if (destination == captured || destination == previous)
        return juce::Result::fail ("A new preview must use an independent destination");
    if (! finite (rate) || rate < 8000 || rate > 768000 || channels < 1 || channels > 2 || prefixFrames <= 0 || totalFrames < prefixFrames || totalFrames > rate * 120
        || ! finite (newGain) || newGain <= 0 || newGain > 1 || ! finite (outputGain) || outputGain <= 0 || outputGain > 1)
        return juce::Result::fail ("Invalid segmented preview duration or format");
    Builder build; build.temporary = destination; build.index.rate = rate; build.index.channels = channels; build.index.frames = totalFrames;
    const auto prefix = prefixFrames / rate, total = totalFrames / rate, fade = total > prefix ? juce::jmin (0.005, prefix) : 0.0;
    bool fresh = false;
    if (isPackage (captured)) fresh = build.appendPackage (captured, 0, prefix, prefix - fade, prefix, false);
    else fresh = build.encode (captured, 0, prefix, quality, newGain, prefix - fade, prefix, false, cancelled);
    if (! fresh) return juce::Result::fail ("Cannot prepare new preview fragments or crossfade limit reached; previous preview preserved");
    if (total > prefix)
    {
        const auto tailStart = prefix - fade;
        const bool tail = isPackage (previous) ? build.appendPackage (previous, tailStart, total, tailStart, prefix, true)
            : build.encode (previous, tailStart, total, quality, newGain, tailStart, prefix, true, cancelled);
        if (! tail) return juce::Result::fail ("Cannot preserve old preview fragments; previous preview preserved");
    }
    for (auto& span : build.index.spans) span.gain *= outputGain;
    return build.save (destination, cancelled);
}
}
