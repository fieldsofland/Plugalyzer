#include "SignalGenerator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace vstest {
namespace {

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

void fillSine(juce::AudioBuffer<float>& buffer, int sampleRate, double frequencyHz,
              double amplitude) {
    const int numSamples = buffer.getNumSamples();
    for (int sample = 0; sample < numSamples; ++sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(sampleRate);
        const float value = static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi *
                                                        frequencyHz * t) *
                                               amplitude);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            buffer.setSample(ch, sample, value);
        }
    }
}

void fillLogSweep(juce::AudioBuffer<float>& buffer, int sampleRate, double startHz, double endHz,
                  double amplitude) {
    const int numSamples = buffer.getNumSamples();
    const double durationSec = static_cast<double>(numSamples) / static_cast<double>(sampleRate);

    const double w1 = 2.0 * juce::MathConstants<double>::pi * startHz;
    const double w2 = 2.0 * juce::MathConstants<double>::pi * endHz;
    const double k = durationSec / std::log(w2 / w1);
    const double l = w1 * k;

    for (int sample = 0; sample < numSamples; ++sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(sampleRate);
        const double phase = l * (std::exp(t / k) - 1.0);
        const float value = static_cast<float>(std::sin(phase) * amplitude);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            buffer.setSample(ch, sample, value);
        }
    }
}

void fillImpulse(juce::AudioBuffer<float>& buffer, double amplitude) {
    if (buffer.getNumSamples() <= 0) {
        return;
    }

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        buffer.setSample(ch, 0, static_cast<float>(amplitude));
    }
}

void fillWhiteNoise(juce::AudioBuffer<float>& buffer, juce::Random& random, double amplitude) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* writePtr = buffer.getWritePointer(ch);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const float noise = random.nextFloat() * 2.0f - 1.0f;
            writePtr[sample] = noise * static_cast<float>(amplitude);
        }
    }
}

void normalizePeakToAmplitude(juce::AudioBuffer<float>& buffer, double amplitude) {
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        peak = std::max(peak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
    }

    if (peak > 1.0e-9f) {
        buffer.applyGain(static_cast<float>(amplitude) / peak);
    }
}

void fillMultitone(juce::AudioBuffer<float>& buffer, int sampleRate, int toneCount,
                   double amplitude, juce::Random& random) {
    const int numSamples = buffer.getNumSamples();
    const int tones = std::clamp(toneCount, 1, 64);

    constexpr double startHz = 60.0;
    constexpr double endHz = 12000.0;

    // Log-spaced tone frequencies with seeded random phases (crest managed by
    // peak-normalizing the summed signal afterwards).
    std::vector<double> frequencies(static_cast<size_t>(tones));
    std::vector<double> phases(static_cast<size_t>(tones));
    for (int tone = 0; tone < tones; ++tone) {
        const double t = (tones == 1) ? 0.0 : static_cast<double>(tone) / (tones - 1);
        frequencies[static_cast<size_t>(tone)] = startHz * std::pow(endHz / startHz, t);
        phases[static_cast<size_t>(tone)] =
            random.nextDouble() * 2.0 * juce::MathConstants<double>::pi;
    }

    auto* write = buffer.getWritePointer(0);
    for (int sample = 0; sample < numSamples; ++sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(sampleRate);
        double value = 0.0;
        for (int tone = 0; tone < tones; ++tone) {
            value += std::sin(2.0 * juce::MathConstants<double>::pi *
                                  frequencies[static_cast<size_t>(tone)] * t +
                              phases[static_cast<size_t>(tone)]);
        }
        write[sample] = static_cast<float>(value / tones);
    }

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch) {
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }

    normalizePeakToAmplitude(buffer, amplitude);
}

// Karplus-Strong style pluck added into channel 0 starting at startSample.
// The excitation noise is lightly low-passed (band-limited) and the string loop
// uses two-point averaging with a damping factor. An exponential envelope caps
// overall decay. pluckGain scales this pluck relative to full scale.
void addKarplusStrongPluck(juce::AudioBuffer<float>& buffer, int sampleRate, int startSample,
                           double frequencyHz, double pluckGain, double decaySeconds,
                           double loopDamping, juce::Random& random) {
    const int numSamples = buffer.getNumSamples();
    if (startSample >= numSamples || frequencyHz <= 0.0) {
        return;
    }

    const int delayLength =
        std::max(2, static_cast<int>(std::round(static_cast<double>(sampleRate) / frequencyHz)));

    std::vector<float> delayLine(static_cast<size_t>(delayLength));
    float previous = 0.0f;
    for (int i = 0; i < delayLength; ++i) {
        const float noise = random.nextFloat() * 2.0f - 1.0f;
        // One-pole smoothing band-limits the excitation burst.
        const float bandLimited = 0.5f * (noise + previous);
        previous = noise;
        delayLine[static_cast<size_t>(i)] = bandLimited;
    }

    auto* write = buffer.getWritePointer(0);
    int readPos = 0;
    const double envelopeRate = 1.0 / std::max(0.01, decaySeconds);

    for (int sample = startSample; sample < numSamples; ++sample) {
        const double t = static_cast<double>(sample - startSample) / static_cast<double>(sampleRate);
        const double envelope = std::exp(-t * envelopeRate);
        if (envelope < 1.0e-5) {
            break;
        }

        const int nextPos = (readPos + 1) % delayLength;
        const float current = delayLine[static_cast<size_t>(readPos)];
        const float averaged =
            static_cast<float>(loopDamping) * 0.5f * (current + delayLine[static_cast<size_t>(nextPos)]);
        delayLine[static_cast<size_t>(readPos)] = averaged;
        readPos = nextPos;

        write[sample] += static_cast<float>(current * envelope * pluckGain);
    }
}

void fillPluck(juce::AudioBuffer<float>& buffer, int sampleRate, double frequencyHz,
               double intervalSec, double amplitude, juce::Random& random) {
    const int numSamples = buffer.getNumSamples();
    const int intervalSamples =
        std::max(1, static_cast<int>(std::round(std::max(0.05, intervalSec) * sampleRate)));

    for (int start = 0; start < numSamples; start += intervalSamples) {
        addKarplusStrongPluck(buffer, sampleRate, start, frequencyHz, 1.0, 1.2, 0.998, random);
    }

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch) {
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }

    normalizePeakToAmplitude(buffer, amplitude);
}

void fillDiRhythm(juce::AudioBuffer<float>& buffer, int sampleRate, double amplitude,
                  juce::Random& random) {
    const int numSamples = buffer.getNumSamples();

    // ~120 BPM eighth-note grid: one event every 0.25 s.
    constexpr double eighthNoteSec = 0.25;
    constexpr double lowEHz = 82.41; // low E fundamental
    constexpr double lowAHz = 110.0; // A fundamental

    const int stepSamples = std::max(1, static_cast<int>(std::round(eighthNoteSec * sampleRate)));

    int step = 0;
    for (int start = 0; start < numSamples; start += stepSamples, ++step) {
        const bool palmMuted = (step % 2) == 1;                 // alternate open / palm-muted
        const double frequencyHz = (step % 4) < 2 ? lowEHz : lowAHz; // alternate E and A pairs

        if (palmMuted) {
            // Palm mute: short decay, heavier loop damping, slightly quieter.
            addKarplusStrongPluck(buffer, sampleRate, start, frequencyHz, 0.8, 0.09, 0.985, random);
        } else {
            // Open pluck: longer ring-out.
            addKarplusStrongPluck(buffer, sampleRate, start, frequencyHz, 1.0, 0.6, 0.997, random);
        }
    }

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch) {
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }

    normalizePeakToAmplitude(buffer, amplitude);
}

} // namespace

juce::AudioBuffer<float> SignalGenerator::generate(const SignalDefinition& def, int sampleRate,
                                                   int channels, unsigned int seed) {
    if (sampleRate <= 0) {
        throw std::runtime_error("Invalid sample rate for signal generation");
    }

    if (channels <= 0) {
        throw std::runtime_error("Invalid channel count for signal generation");
    }

    const int numSamples = std::max(1, static_cast<int>(std::round(def.durationSec * sampleRate)));
    juce::AudioBuffer<float> buffer(channels, numSamples);
    buffer.clear();

    const auto amplitude = dbToLinear(def.levelDbfs);
    juce::Random random(static_cast<juce::int64>(seed));

    if (def.type == "sine") {
        fillSine(buffer, sampleRate, def.frequencyHz, amplitude);
        return buffer;
    }

    if (def.type == "logSweep") {
        fillLogSweep(buffer, sampleRate, def.startHz, def.endHz, amplitude);
        return buffer;
    }

    if (def.type == "impulse") {
        fillImpulse(buffer, amplitude);
        return buffer;
    }

    if (def.type == "silence") {
        return buffer;
    }

    if (def.type == "noise") {
        fillWhiteNoise(buffer, random, amplitude);
        return buffer;
    }

    if (def.type == "multitone") {
        fillMultitone(buffer, sampleRate, def.toneCount, amplitude, random);
        return buffer;
    }

    if (def.type == "pluck") {
        fillPluck(buffer, sampleRate, def.frequencyHz > 0.0 ? def.frequencyHz : 110.0,
                  def.intervalSec, amplitude, random);
        return buffer;
    }

    if (def.type == "diRhythm") {
        fillDiRhythm(buffer, sampleRate, amplitude, random);
        return buffer;
    }

    throw std::runtime_error("Unsupported signal type: " + def.type);
}

juce::AudioBuffer<float> SignalGenerator::generateImdDualTone(double durationSec, int sampleRate,
                                                              int channels, double levelDbfs,
                                                              double lowHz, double highHz) {
    const int numSamples = std::max(1, static_cast<int>(std::round(durationSec * sampleRate)));
    juce::AudioBuffer<float> buffer(channels, numSamples);

    const auto amplitude = dbToLinear(levelDbfs) * 0.5;

    for (int sample = 0; sample < numSamples; ++sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(sampleRate);
        const auto value = static_cast<float>(
            amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * lowHz * t) +
            amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * highHz * t));

        for (int ch = 0; ch < channels; ++ch) {
            buffer.setSample(ch, sample, value);
        }
    }

    return buffer;
}

} // namespace vstest
