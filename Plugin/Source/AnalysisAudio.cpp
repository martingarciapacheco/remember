#include "../../Shared/PreviewAudio.h"
#include "AnalysisAudio.h"
#include <numeric>
#if JUCE_INTEL && (defined (__SSE2__) || defined (_M_X64))
 #include <immintrin.h>
 #define REMEMBER_SINC_SSE 1
#else
 #define REMEMBER_SINC_SSE 0
#endif

namespace AnalysisAudio
{
namespace
{
std::unique_ptr<juce::AudioFormatReader> open (const juce::File& file)
{
    juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
    return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
}
bool supportedRate (double value)
{
    return std::isfinite (value) && value >= 1 && value <= 768000;
}
float dot (const float* samples, const float* coefficients, int count)
{
    int i = 0;
    float sum = 0;
   #if REMEMBER_SINC_SSE
    auto accumulator = _mm_setzero_ps();
    for (; i + 4 <= count; i += 4)
        accumulator = _mm_add_ps (accumulator, _mm_mul_ps (_mm_loadu_ps (samples + i), _mm_loadu_ps (coefficients + i)));
    alignas (16) float values[4]; _mm_store_ps (values, accumulator);
    sum = values[0] + values[1] + values[2] + values[3];
   #endif
    for (; i < count; ++i) sum += samples[i] * coefficients[i];
    return sum;
}

// The same converter serves immutable files and memory fixtures. It looks
// ahead into the source, zero-pads the endpoints and never drops filter delay
// from a recording prefix. Rational audio rates use exact polyphase positions.
class Converter
{
public:
    using Reader = std::function<bool (juce::AudioBuffer<float>&, int, int, juce::int64)>;
    Converter (double sourceRate, double outputRate, juce::int64 sourceFrames, int channelCount,
               Reader input, Cancellation stop)
        : sourceLength (sourceFrames), channels (channelCount), reader (std::move (input)), cancelled (std::move (stop))
    {
        if (! supportedRate (sourceRate) || ! supportedRate (outputRate) || sourceFrames < 0
            || channels < 1 || channels > 2 || shouldStop()) return;
        ratio = sourceRate / outputRate;
        if (ratio < 1.0 / 64 || ratio > 64) return;
        outputLength = convertedLength (sourceFrames, sourceRate, outputRate);
        bypass = sourceRate == outputRate;
        if (! bypass)
        {
            half = (int) std::ceil (64 * juce::jmax (1.0, ratio));
            taps = 2 * half + 1;
            const auto maxPhases = juce::jmin (1024, (int) ((8 * 1024 * 1024) / (sizeof (float) * (size_t) taps)) - 1);
            const auto roundedSource = std::llround (sourceRate), roundedTarget = std::llround (outputRate);
            if (sourceRate == (double) roundedSource && outputRate == (double) roundedTarget)
            {
                const auto divisor = std::gcd (roundedSource, roundedTarget);
                numerator = roundedSource / divisor; denominator = roundedTarget / divisor;
                exactPhases = denominator <= maxPhases;
            }
            phases = exactPhases ? (int) denominator : maxPhases;
            coefficients.resize ((size_t) (phases + (exactPhases ? 0 : 1)) * (size_t) taps);
            const auto cutoff = 0.95 / juce::jmax (1.0, ratio);
            for (int phase = 0; phase < phases + (exactPhases ? 0 : 1); ++phase)
            {
                if (shouldStop()) return;
                const auto fraction = (double) phase / phases;
                auto* kernel = coefficients.data() + (size_t) phase * (size_t) taps;
                double sum = 0;
                for (int tap = 0; tap < taps; ++tap)
                {
                    const auto distance = tap - half - fraction;
                    const auto x = distance / half;
                    const auto window = std::abs (x) >= 1 ? 0 : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * x)
                        + 0.08 * std::cos (juce::MathConstants<double>::twoPi * x);
                    const auto angle = juce::MathConstants<double>::pi * cutoff * distance;
                    const auto sinc = std::abs (angle) < 1.0e-12 ? 1 : std::sin (angle) / angle;
                    kernel[tap] = (float) (cutoff * sinc * window); sum += kernel[tap];
                }
                for (int tap = 0; tap < taps; ++tap) kernel[tap] = (float) (kernel[tap] / sum);
            }
            cache.setSize (channels, 16384 + taps);
        }
        ready = true;
    }
    bool valid() const { return ready && ! failed; }
    int length() const { return outputLength; }
    bool read (juce::AudioBuffer<float>& output, int count)
    {
        if (! valid() || count < 0 || count > output.getNumSamples() || output.getNumChannels() < channels || shouldStop()) return false;
        for (int ch = 0; ch < output.getNumChannels(); ++ch) output.clear (ch, 0, count);
        if (bypass)
        {
            const auto available = (int) juce::jlimit<juce::int64> (0, count, juce::jmin<juce::int64> (sourceLength, outputLength) - position);
            if (available > 0 && ! reader (output, 0, available, position)) { failed = true; return false; }
            position += count; return true;
        }
        for (int sample = 0; sample < count; ++sample, ++position)
        {
            if ((sample & 255) == 0 && shouldStop()) return false;
            if (position >= outputLength) continue;
            juce::int64 centre = 0;
            double fraction = 0;
            int phase = 0;
            float interpolation = 0;
            if (denominator > 0)
            {
                const auto scaled = position * numerator;
                centre = scaled / denominator; fraction = (double) (scaled % denominator) / denominator;
            }
            else
            {
                const auto sourcePosition = position * ratio;
                centre = (juce::int64) std::floor (sourcePosition); fraction = sourcePosition - centre;
            }
            if (exactPhases) phase = (int) ((position * numerator) % denominator);
            else
            {
                const auto scaled = fraction * phases;
                phase = juce::jmin (phases - 1, (int) scaled); interpolation = (float) (scaled - phase);
            }
            const auto first = centre - half;
            if (cacheStart == std::numeric_limits<juce::int64>::min() || first < cacheStart
                || first + taps > cacheStart + cache.getNumSamples())
            {
                cacheStart = first; cache.clear();
                const auto inputStart = juce::jmax<juce::int64> (0, cacheStart);
                const auto destination = (int) (inputStart - cacheStart);
                const auto available = (int) juce::jlimit<juce::int64> (0, cache.getNumSamples() - destination, sourceLength - inputStart);
                if (available > 0 && ! reader (cache, destination, available, inputStart)) { failed = true; return false; }
            }
            const auto offset = (int) (first - cacheStart);
            const auto* kernel = coefficients.data() + (size_t) phase * (size_t) taps;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto* source = cache.getReadPointer (ch, offset);
                const auto value = dot (source, kernel, taps);
                output.setSample (ch, sample, exactPhases ? value
                    : value + interpolation * (dot (source, kernel + taps, taps) - value));
            }
        }
        return true;
    }
private:
    bool shouldStop() const { return cancelled && cancelled(); }
    juce::int64 sourceLength = 0, position = 0, numerator = 0, denominator = 0;
    juce::int64 cacheStart = std::numeric_limits<juce::int64>::min();
    int channels = 0, outputLength = 0, half = 0, taps = 0, phases = 0;
    double ratio = 1;
    bool ready = false, failed = false, bypass = false, exactPhases = false;
    Reader reader;
    Cancellation cancelled;
    std::vector<float> coefficients;
    juce::AudioBuffer<float> cache;
};
}
struct FileStream::Impl
{
    explicit Impl (const juce::File& file, double targetRate, const Cancellation& cancelled) : reader (open (file))
    {
        if (reader && reader->numChannels > 0)
            converter = std::make_unique<Converter> (reader->sampleRate, targetRate, reader->lengthInSamples,
                juce::jmin (2, (int) reader->numChannels),
                [this] (juce::AudioBuffer<float>& output, int destination, int count, juce::int64 offset)
                { return reader->read (&output, destination, count, offset, true, true); }, cancelled);
    }
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<Converter> converter;
};
int convertedLength (juce::int64 sourceSamples, double sourceRate, double outputRate)
{
    if (sourceSamples < 0 || ! supportedRate (sourceRate) || ! supportedRate (outputRate)) return 0;
    // Use the same operation order for readers and cache metadata. Dividing
    // by a pre-rounded rate ratio can move an exact half-frame below the tie.
    return (int) juce::jmin (outputRate * 120.0, std::round (sourceSamples * outputRate / sourceRate));
}
FileStream::FileStream (const juce::File& file, double targetRate, const Cancellation& cancelled)
    : impl (std::make_unique<Impl> (file, targetRate, cancelled)) {}
FileStream::~FileStream() = default;
bool FileStream::valid() const { return impl->converter && impl->converter->valid(); }
int FileStream::length() const { return valid() ? impl->converter->length() : 0; }
double FileStream::sourceRate() const { return impl->reader ? impl->reader->sampleRate : 0; }
juce::int64 FileStream::sourceLength() const { return impl->reader ? impl->reader->lengthInSamples : 0; }
int FileStream::sourceChannels() const { return impl->reader ? (int) impl->reader->numChannels : 0; }
bool FileStream::read (juce::AudioBuffer<float>& buffer, int samples)
{
    if (! valid() || ! impl->converter->read (buffer, samples)) return false;
    // Filter a mono file once, then preserve the public stereo read contract.
    if (impl->reader->numChannels == 1 && buffer.getNumChannels() > 1 && samples > 0)
        buffer.copyFrom (1, 0, buffer, 0, 0, samples);
    return true;
}
juce::AudioBuffer<float> fromFile (const juce::File& file, int channels, const Cancellation& cancelled)
{
    return fromFilePrefix (file, channels, (int) (120 * rate), cancelled);
}
int unchangedOutputSample (int changedSourceSamples, double sourceRate)
{
    if (! supportedRate (sourceRate) || changedSourceSamples < 0) return (int) (120 * rate);
    if (sourceRate == rate) return changedSourceSamples;
    const auto ratio = sourceRate / rate;
    const auto half = std::ceil (64 * juce::jmax (1.0, ratio));
    return (int) juce::jmin (120 * rate, std::ceil ((changedSourceSamples + half + 1) / ratio) + 2);
}
juce::AudioBuffer<float> fromFilePrefix (const juce::File& file, int channels, int samples, const Cancellation& cancelled)
{
    if (channels < 1 || channels > 2 || samples < 0 || (cancelled && cancelled())) return {};
    FileStream input (file, rate, cancelled);
    if (! input.valid()) return {};
    juce::AudioBuffer<float> result (channels, juce::jmin (samples, input.length())), block (2, 4096);
    for (int offset = 0; offset < result.getNumSamples();)
    {
        const auto count = juce::jmin (4096, result.getNumSamples() - offset);
        if (! input.read (block, count)) return {};
        for (int ch = 0; ch < channels; ++ch) result.copyFrom (ch, offset, block, ch, 0, count);
        offset += count;
    }
    return result;
}
juce::AudioBuffer<float> normalise (const juce::AudioBuffer<float>& audio, double sourceRate, const Cancellation& cancelled)
{
    if (audio.getNumChannels() == 0 || audio.getNumSamples() == 0 || (cancelled && cancelled())) return {};
    const auto channels = juce::jmin (2, audio.getNumChannels());
    Converter input (sourceRate, rate, audio.getNumSamples(), channels,
        [&audio, channels] (juce::AudioBuffer<float>& output, int destination, int count, juce::int64 offset)
        {
            for (int ch = 0; ch < channels; ++ch) output.copyFrom (ch, destination, audio, ch, (int) offset, count);
            return true;
        }, cancelled);
    if (! input.valid()) return {};
    juce::AudioBuffer<float> result (channels, input.length()), block (channels, 4096);
    for (int offset = 0; offset < result.getNumSamples();)
    {
        const auto count = juce::jmin (4096, result.getNumSamples() - offset);
        if (! input.read (block, count)) return {};
        for (int ch = 0; ch < channels; ++ch) result.copyFrom (ch, offset, block, ch, 0, count);
        offset += count;
    }
    return result;
}
}
