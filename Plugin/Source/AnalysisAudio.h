#pragma once
#include <JuceHeader.h>
#include <functional>

namespace AnalysisAudio
{
constexpr double rate = 22050;
using Cancellation = std::function<bool()>;
int convertedLength (juce::int64 sourceSamples, double sourceRate, double outputRate);

// Offline, centred windowed-sinc conversion. The output timeline starts at
// input frame zero (no filter delay). Coefficients and read cache are bounded;
// this reader belongs on a background thread, never the DAW audio callback.
class FileStream
{
public:
    FileStream (const juce::File&, double outputRate, const Cancellation& = {});
    ~FileStream();
    bool valid() const;
    int length() const;
    double sourceRate() const;
    juce::int64 sourceLength() const;
    int sourceChannels() const;
    bool read (juce::AudioBuffer<float>&, int samples);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE (FileStream)
};
juce::AudioBuffer<float> fromFile (const juce::File&, int channels, const Cancellation& = {});
// Reads a prefix of the output timeline while the converter still sees the
// complete immutable source and its lookahead (no artificial prefix endpoint).
juce::AudioBuffer<float> fromFilePrefix (const juce::File&, int channels, int samples, const Cancellation& = {});
int unchangedOutputSample (int changedSourceSamples, double sourceRate);
juce::AudioBuffer<float> normalise (const juce::AudioBuffer<float>&, double sourceRate, const Cancellation& = {});
}
