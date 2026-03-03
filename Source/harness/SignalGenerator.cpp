#include "SignalGenerator.h"

#include <cmath>
#include <stdexcept>

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
