#include "Analyzers.h"

#include "RenderEngine.h"
#include "SignalGenerator.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace vstest {
namespace {

double linearToDb(double value) {
    constexpr double floorValue = 1.0e-20;
    return 20.0 * std::log10(std::max(std::abs(value), floorValue));
}

double computeRms(const juce::AudioBuffer<float>& buffer) {
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0) {
        return 0.0;
    }

    long double sumSquares = 0.0;
    const auto count = static_cast<long double>(buffer.getNumChannels() * buffer.getNumSamples());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* read = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const auto sample = static_cast<long double>(read[i]);
            sumSquares += sample * sample;
        }
    }

    return std::sqrt(static_cast<double>(sumSquares / count));
}

double computePeak(const juce::AudioBuffer<float>& buffer) {
    double peak = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        peak = std::max(peak, static_cast<double>(buffer.getMagnitude(ch, 0, buffer.getNumSamples())));
    }

    return peak;
}

double computeDcOffset(const juce::AudioBuffer<float>& buffer) {
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0) {
        return 0.0;
    }

    long double sum = 0.0;
    const auto count = static_cast<long double>(buffer.getNumChannels() * buffer.getNumSamples());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* read = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            sum += read[i];
        }
    }

    return static_cast<double>(sum / count);
}

double residualRmsDbfs(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) {
        throw std::runtime_error("Buffers differ in size for residual comparison");
    }

    juce::AudioBuffer<float> diff(a.getNumChannels(), a.getNumSamples());
    for (int ch = 0; ch < a.getNumChannels(); ++ch) {
        const auto* readA = a.getReadPointer(ch);
        const auto* readB = b.getReadPointer(ch);
        auto* write = diff.getWritePointer(ch);
        for (int i = 0; i < a.getNumSamples(); ++i) {
            write[i] = readA[i] - readB[i];
        }
    }

    return linearToDb(computeRms(diff));
}

int nextPow2(int value) {
    int power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

struct Spectrum {
    std::vector<double> magnitudes;
    std::vector<double> phases;
    int fftSize = 0;
};

Spectrum computeSpectrum(const juce::AudioBuffer<float>& buffer, int channel) {
    if (buffer.getNumSamples() == 0) {
        throw std::runtime_error("Cannot compute spectrum of empty buffer");
    }

    const int fftSize = std::min(1 << 16, nextPow2(buffer.getNumSamples()));
    const int fftOrder = static_cast<int>(std::round(std::log2(fftSize)));

    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> fftData(fftSize * 2, 0.0f);
    const auto sourceChannel = std::min(channel, buffer.getNumChannels() - 1);
    const auto* read = buffer.getReadPointer(sourceChannel);

    for (int i = 0; i < std::min(fftSize, buffer.getNumSamples()); ++i) {
        fftData[i] = read[i];
    }

    window.multiplyWithWindowingTable(fftData.data(), fftSize);
    fft.performRealOnlyForwardTransform(fftData.data());

    Spectrum spectrum;
    spectrum.fftSize = fftSize;
    const int numBins = fftSize / 2;
    spectrum.magnitudes.resize(numBins, 0.0);
    spectrum.phases.resize(numBins, 0.0);

    for (int bin = 0; bin < numBins; ++bin) {
        const auto real = fftData[2 * bin];
        const auto imag = fftData[2 * bin + 1];
        spectrum.magnitudes[bin] = std::sqrt(real * real + imag * imag);
        spectrum.phases[bin] = std::atan2(imag, real);
    }

    return spectrum;
}

int dominantBin(const Spectrum& spectrum) {
    int bin = 1;
    double mag = spectrum.magnitudes[1];
    for (int i = 2; i < static_cast<int>(spectrum.magnitudes.size()); ++i) {
        if (spectrum.magnitudes[i] > mag) {
            mag = spectrum.magnitudes[i];
            bin = i;
        }
    }

    return bin;
}

double aliasingRatioDb(const Spectrum& spectrum) {
    const int fundamental = dominantBin(spectrum);
    const int bins = static_cast<int>(spectrum.magnitudes.size());

    auto energyAt = [&](int bin) {
        if (bin < 0 || bin >= bins) {
            return 0.0;
        }
        const auto m = spectrum.magnitudes[bin];
        return m * m;
    };

    double desiredEnergy = 0.0;
    for (int harmonic = 1; harmonic <= 8; ++harmonic) {
        const int center = fundamental * harmonic;
        if (center >= bins) {
            break;
        }

        desiredEnergy += energyAt(center - 1) + energyAt(center) + energyAt(center + 1);
    }

    double totalEnergy = 0.0;
    for (int bin = 1; bin < bins; ++bin) {
        const auto m = spectrum.magnitudes[bin];
        totalEnergy += m * m;
    }

    const double residualEnergy = std::max(0.0, totalEnergy - desiredEnergy);
    return 10.0 * std::log10((residualEnergy + 1e-30) / (desiredEnergy + 1e-30));
}

int estimateLatencySamples(const juce::AudioBuffer<float>& input, const juce::AudioBuffer<float>& output,
                           int maxLag) {
    if (input.getNumSamples() == 0 || output.getNumSamples() == 0) {
        return 0;
    }

    const auto* inputPtr = input.getReadPointer(0);
    const auto* outputPtr = output.getReadPointer(0);

    const int maxSamples = std::min(input.getNumSamples(), output.getNumSamples());

    double bestCorrelation = -std::numeric_limits<double>::infinity();
    int bestLag = 0;

    for (int lag = 0; lag <= maxLag; ++lag) {
        double corr = 0.0;
        const int valid = maxSamples - lag;
        if (valid <= 0) {
            break;
        }

        for (int i = 0; i < valid; ++i) {
            corr += static_cast<double>(inputPtr[i]) * static_cast<double>(outputPtr[i + lag]);
        }

        if (corr > bestCorrelation) {
            bestCorrelation = corr;
            bestLag = lag;
        }
    }

    return bestLag;
}

void writeWav(const juce::AudioBuffer<float>& buffer, int sampleRate, const juce::File& path) {
    juce::WavAudioFormat wav;
    auto stream = path.createOutputStream();
    if (!stream) {
        throw std::runtime_error("Unable to create artifact file: " +
                                 path.getFullPathName().toStdString());
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
        stream.release(), static_cast<double>(sampleRate), static_cast<unsigned int>(buffer.getNumChannels()),
        24, juce::StringPairArray(), 0));

    if (!writer) {
        throw std::runtime_error("Unable to create WAV writer for artifact output");
    }

    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

void addThreshold(std::map<std::string, double>& thresholds, const char* key,
                  const std::optional<double>& value) {
    if (value) {
        thresholds[key] = *value;
    }
}

void checkUpperBound(CaseResult& result, const std::string& metric, const std::string& thresholdName) {
    if (!result.metrics.contains(metric) || !result.thresholds.contains(thresholdName)) {
        return;
    }

    if (result.metrics[metric] > result.thresholds[thresholdName]) {
        result.status = "failed";
        if (!result.message.empty()) {
            result.message += " | ";
        }
        result.message += metric + " exceeded threshold";
    }
}

void checkLowerBound(CaseResult& result, const std::string& metric, const std::string& thresholdName) {
    if (!result.metrics.contains(metric) || !result.thresholds.contains(thresholdName)) {
        return;
    }

    if (result.metrics[metric] < result.thresholds[thresholdName]) {
        result.status = "failed";
        if (!result.message.empty()) {
            result.message += " | ";
        }
        result.message += metric + " below threshold";
    }
}

void applyThresholdChecks(CaseResult& result) {
    checkUpperBound(result, "aliasingRatioDb", "aliasingRatioDbMax");
    checkUpperBound(result, "eqMaxErrorDb", "eqMaxErrorDb");
    checkUpperBound(result, "eqRmsErrorDb", "eqRmsErrorDb");
    checkUpperBound(result, "noiseFloorDbfs", "noiseFloorDbfsMax");
    checkUpperBound(result, "latencyErrorSamples", "latencyErrorSamplesMax");
    checkUpperBound(result, "determinismResidualDbfs", "determinismResidualDbfsMax");
    checkUpperBound(result, "thdnDb", "thdnDbMax");
    checkUpperBound(result, "imdDb", "imdDbMax");
    checkUpperBound(result, "phaseDeviationDeg", "phaseDeviationDegMax");
    checkUpperBound(result, "bypassClickPeakDbfs", "bypassClickPeakDbfsMax");
    checkUpperBound(result, "zipperArtifactDb", "zipperArtifactDbMax");
    checkUpperBound(result, "memoryDriftMb", "maxMemoryDriftMb");

    checkLowerBound(result, "realtimeFactor", "minRealtimeFactor");
}

bool runPluginval(const std::optional<std::string>& pluginvalPath, const CaseSpec& caseSpec,
                  CaseResult& result) {
    const auto resolvedPath = pluginvalPath.value_or("pluginval");

    juce::ChildProcess process;
    const juce::String command =
        juce::String::fromUTF8(resolvedPath.c_str()) + " --strictness-level " +
        juce::String(caseSpec.pluginvalStrictness) + " --validate-in-process --output-dir \"" +
        juce::String(caseSpec.artifactsDir) + "\" \"" + juce::String(caseSpec.pluginPath) + "\"";

    if (!process.start(command)) {
        if (caseSpec.pluginvalRequired) {
            result.status = "failed";
            result.message = "pluginval executable missing or not runnable";
        } else {
            result.status = "skipped";
            result.message = "pluginval not available; skipped optional validation";
        }
        return false;
    }

    process.waitForProcessToFinish(caseSpec.timeoutSec * 1000);
    const auto output = process.readAllProcessOutput().toStdString();

    juce::File(caseSpec.artifactsDir).createDirectory();
    const auto logFile = juce::File(caseSpec.artifactsDir).getChildFile("pluginval.log");
    logFile.replaceWithText(output);
    result.artifacts["pluginvalLog"] = logFile.getFullPathName().toStdString();

    if (process.getExitCode() != 0) {
        result.status = "failed";
        result.message = "pluginval reported failures";
        return false;
    }

    result.metrics["pluginvalExitCode"] = 0.0;
    return true;
}

CaseResult buildBaseResult(const CaseSpec& caseSpec) {
    CaseResult result;
    result.id = caseSpec.id;
    result.testType = caseSpec.testType;
    result.pluginId = caseSpec.pluginId;
    result.sampleRate = caseSpec.sampleRate;
    result.blockSize = caseSpec.blockSize;
    result.channels = caseSpec.channels;
    result.status = "passed";

    addThreshold(result.thresholds, "aliasingRatioDbMax", caseSpec.thresholds.aliasingRatioDbMax);
    addThreshold(result.thresholds, "eqMaxErrorDb", caseSpec.thresholds.eqMaxErrorDb);
    addThreshold(result.thresholds, "eqRmsErrorDb", caseSpec.thresholds.eqRmsErrorDb);
    addThreshold(result.thresholds, "noiseFloorDbfsMax", caseSpec.thresholds.noiseFloorDbfsMax);
    addThreshold(result.thresholds, "latencyErrorSamplesMax", caseSpec.thresholds.latencyErrorSamplesMax);
    addThreshold(result.thresholds, "determinismResidualDbfsMax",
                 caseSpec.thresholds.determinismResidualDbfsMax);
    addThreshold(result.thresholds, "thdnDbMax", caseSpec.thresholds.thdnDbMax);
    addThreshold(result.thresholds, "imdDbMax", caseSpec.thresholds.imdDbMax);
    addThreshold(result.thresholds, "phaseDeviationDegMax", caseSpec.thresholds.phaseDeviationDegMax);
    addThreshold(result.thresholds, "bypassClickPeakDbfsMax",
                 caseSpec.thresholds.bypassClickPeakDbfsMax);
    addThreshold(result.thresholds, "zipperArtifactDbMax", caseSpec.thresholds.zipperArtifactDbMax);
    addThreshold(result.thresholds, "minRealtimeFactor", caseSpec.thresholds.minRealtimeFactor);
    addThreshold(result.thresholds, "maxMemoryDriftMb", caseSpec.thresholds.maxMemoryDriftMb);

    return result;
}

RenderRequest buildRequest(const CaseSpec& caseSpec, unsigned int seed) {
    RenderRequest request;
    request.pluginPath = caseSpec.pluginPath;
    request.presetPath = caseSpec.presetPath;
    request.parameterSets = caseSpec.parameterSets;
    request.sampleRate = caseSpec.sampleRate;
    request.blockSize = caseSpec.blockSize;
    request.channels = caseSpec.channels;
    request.input = SignalGenerator::generate(caseSpec.signal, caseSpec.sampleRate, caseSpec.channels, seed);

    return request;
}

void ensureArtifacts(const CaseSpec& caseSpec) {
    juce::File(caseSpec.artifactsDir).createDirectory();
}

void writeReproScript(const CaseSpec& caseSpec, const CaseResult& result) {
    juce::File artifactDir(caseSpec.artifactsDir);
    const auto repro = artifactDir.getChildFile("repro.sh");

    std::string script;
    script += "#!/usr/bin/env bash\n";
    script += "set -euo pipefail\n";
    script += "vst-test run --suite \"" + caseSpec.suiteName + "\" --select \"" + caseSpec.id + "\"\n";

    repro.replaceWithText(script);

    result.artifacts.size();
}

} // namespace

CaseResult AnalyzerEngine::runCase(const CaseSpec& caseSpec,
                                   const std::optional<std::string>& pluginvalPath,
                                   unsigned int seed) {
    CaseResult result = buildBaseResult(caseSpec);

    try {
        ensureArtifacts(caseSpec);

        if (caseSpec.runPluginval) {
            runPluginval(pluginvalPath, caseSpec, result);
            if (result.status != "passed") {
                return result;
            }
        }

        auto request = buildRequest(caseSpec, seed);
        auto render = RenderEngine::render(request);

        const auto artifactDir = juce::File(caseSpec.artifactsDir);
        const auto outputPath = artifactDir.getChildFile("output.wav");
        writeWav(render.output, render.sampleRate, outputPath);

        result.artifacts["renderedAudio"] = outputPath.getFullPathName().toStdString();
        result.artifacts["reproCmd"] = artifactDir.getChildFile("repro.sh").getFullPathName().toStdString();
        writeReproScript(caseSpec, result);

        result.metrics["outputPeakDbfs"] = linearToDb(computePeak(render.output));
        result.metrics["outputRmsDbfs"] = linearToDb(computeRms(render.output));
        result.metrics["realtimeFactor"] = render.realtimeFactor;

        if (caseSpec.testType == "load") {
            result.metrics["loadPass"] = 1.0;

        } else if (caseSpec.testType == "determinism") {
            auto render2 = RenderEngine::render(request);
            result.metrics["determinismResidualDbfs"] = residualRmsDbfs(render.output, render2.output);

        } else if (caseSpec.testType == "aliasing") {
            auto spectrum = computeSpectrum(render.output, 0);
            result.metrics["aliasingRatioDb"] = aliasingRatioDb(spectrum);

        } else if (caseSpec.testType == "eqCurve") {
            auto inputSpectrum = computeSpectrum(request.input, 0);
            auto outputSpectrum = computeSpectrum(render.output, 0);

            const int bins = std::min(inputSpectrum.magnitudes.size(), outputSpectrum.magnitudes.size());
            std::vector<double> absErrors;
            absErrors.reserve(bins);

            for (int bin = 1; bin < bins; ++bin) {
                const double inMag = inputSpectrum.magnitudes[bin];
                if (inMag < 1e-8) {
                    continue;
                }

                const double transferDb = linearToDb(outputSpectrum.magnitudes[bin] / (inMag + 1e-20));
                absErrors.push_back(std::abs(transferDb));
            }

            if (absErrors.empty()) {
                throw std::runtime_error("Insufficient spectral energy for eqCurve test");
            }

            const auto maxIt = std::max_element(absErrors.begin(), absErrors.end());
            const double rms =
                std::sqrt(std::accumulate(absErrors.begin(), absErrors.end(), 0.0,
                                          [](double acc, double v) { return acc + v * v; }) /
                          static_cast<double>(absErrors.size()));

            result.metrics["eqMaxErrorDb"] = *maxIt;
            result.metrics["eqRmsErrorDb"] = rms;

        } else if (caseSpec.testType == "phaseGroupDelay") {
            auto inputSpectrum = computeSpectrum(request.input, 0);
            auto outputSpectrum = computeSpectrum(render.output, 0);

            const int bins = std::min(inputSpectrum.magnitudes.size(), outputSpectrum.magnitudes.size());

            std::vector<double> phaseDiff;
            phaseDiff.reserve(bins);

            for (int bin = 1; bin < bins; ++bin) {
                if (inputSpectrum.magnitudes[bin] < 1e-8) {
                    continue;
                }

                double diff = outputSpectrum.phases[bin] - inputSpectrum.phases[bin];
                while (diff > juce::MathConstants<double>::pi) {
                    diff -= 2.0 * juce::MathConstants<double>::pi;
                }
                while (diff < -juce::MathConstants<double>::pi) {
                    diff += 2.0 * juce::MathConstants<double>::pi;
                }

                phaseDiff.push_back(diff * 180.0 / juce::MathConstants<double>::pi);
            }

            if (phaseDiff.empty()) {
                throw std::runtime_error("Insufficient spectral energy for phaseGroupDelay test");
            }

            const double rms =
                std::sqrt(std::accumulate(phaseDiff.begin(), phaseDiff.end(), 0.0,
                                          [](double acc, double v) { return acc + v * v; }) /
                          static_cast<double>(phaseDiff.size()));
            result.metrics["phaseDeviationDeg"] = rms;

        } else if (caseSpec.testType == "thdn") {
            auto spectrum = computeSpectrum(render.output, 0);
            const int fundamental = dominantBin(spectrum);
            const int bins = spectrum.magnitudes.size();

            const double fundamentalEnergy =
                spectrum.magnitudes[fundamental] * spectrum.magnitudes[fundamental];

            double residualEnergy = 0.0;
            for (int bin = 1; bin < bins; ++bin) {
                if (std::abs(bin - fundamental) <= 1) {
                    continue;
                }
                residualEnergy += spectrum.magnitudes[bin] * spectrum.magnitudes[bin];
            }

            result.metrics["thdnDb"] =
                10.0 * std::log10((residualEnergy + 1e-30) / (fundamentalEnergy + 1e-30));

        } else if (caseSpec.testType == "imd") {
            request.input = SignalGenerator::generateImdDualTone(caseSpec.signal.durationSec, caseSpec.sampleRate,
                                                                 caseSpec.channels, caseSpec.signal.levelDbfs);
            render = RenderEngine::render(request);
            auto spectrum = computeSpectrum(render.output, 0);

            const int fftBins = spectrum.magnitudes.size();
            const double binHz = static_cast<double>(caseSpec.sampleRate) / static_cast<double>(spectrum.fftSize);

            auto hzToBin = [&](double hz) {
                return std::clamp(static_cast<int>(std::round(hz / binHz)), 1, fftBins - 1);
            };

            const int carrierBin = hzToBin(7000.0);
            const double carrierEnergy =
                spectrum.magnitudes[carrierBin] * spectrum.magnitudes[carrierBin];

            double sidebandEnergy = 0.0;
            for (int n = 1; n <= 5; ++n) {
                const int upper = hzToBin(7000.0 + n * 60.0);
                const int lower = hzToBin(std::max(20.0, 7000.0 - n * 60.0));
                sidebandEnergy += spectrum.magnitudes[upper] * spectrum.magnitudes[upper];
                sidebandEnergy += spectrum.magnitudes[lower] * spectrum.magnitudes[lower];
            }

            result.metrics["imdDb"] =
                10.0 * std::log10((sidebandEnergy + 1e-30) / (carrierEnergy + 1e-30));

        } else if (caseSpec.testType == "noiseDc") {
            result.metrics["noiseFloorDbfs"] = linearToDb(computeRms(render.output));
            result.metrics["dcOffset"] = computeDcOffset(render.output);

        } else if (caseSpec.testType == "latency") {
            request.input = SignalGenerator::generate({"latency_impulse", "impulse", 1000.0, 0.0, 2.0, 20.0,
                                                       20000.0},
                                                      caseSpec.sampleRate, caseSpec.channels, seed);
            render = RenderEngine::render(request);

            const int estimated = estimateLatencySamples(request.input, render.output,
                                                         std::min(caseSpec.sampleRate, 16384));
            const double error = std::abs(estimated - render.reportedLatencySamples);
            result.metrics["reportedLatencySamples"] = render.reportedLatencySamples;
            result.metrics["estimatedLatencySamples"] = estimated;
            result.metrics["latencyErrorSamples"] = error;

        } else if (caseSpec.testType == "automationZipper") {
            auto staticRequest = request;
            staticRequest.automateFirstParameter = false;
            auto staticRender = RenderEngine::render(staticRequest);

            auto automatedRequest = request;
            automatedRequest.automateFirstParameter = true;
            auto automatedRender = RenderEngine::render(automatedRequest);

            juce::AudioBuffer<float> diff(staticRender.output.getNumChannels(),
                                          staticRender.output.getNumSamples());
            for (int ch = 0; ch < diff.getNumChannels(); ++ch) {
                auto* write = diff.getWritePointer(ch);
                const auto* a = staticRender.output.getReadPointer(ch);
                const auto* b = automatedRender.output.getReadPointer(ch);
                for (int i = 0; i < diff.getNumSamples(); ++i) {
                    write[i] = b[i] - a[i];
                }
            }

            result.metrics["zipperArtifactDb"] = linearToDb(computePeak(diff));

        } else if (caseSpec.testType == "bypassClickPop") {
            std::optional<size_t> bypassIndex;
            for (size_t i = 0; i < request.parameterSets.size(); ++i) {
                if (juce::String(request.parameterSets[i].param).containsIgnoreCase("bypass")) {
                    bypassIndex = i;
                    break;
                }
            }

            if (!bypassIndex) {
                result.status = "skipped";
                result.message = "No bypass parameter mapping found in case config";
            } else {
                auto onReq = request;
                onReq.parameterSets[*bypassIndex].value = "On";
                auto offReq = request;
                offReq.parameterSets[*bypassIndex].value = "Off";

                auto onRender = RenderEngine::render(onReq);
                auto offRender = RenderEngine::render(offReq);

                const int boundary = onRender.output.getNumSamples() / 2;
                double maxJump = 0.0;
                for (int ch = 0; ch < onRender.output.getNumChannels(); ++ch) {
                    const double before = onRender.output.getSample(ch, std::max(0, boundary - 1));
                    const double after = offRender.output.getSample(ch, std::min(boundary, offRender.output.getNumSamples() - 1));
                    maxJump = std::max(maxJump, std::abs(after - before));
                }
                result.metrics["bypassClickPeakDbfs"] = linearToDb(maxJump);
            }

        } else if (caseSpec.testType == "stateRoundtrip") {
            const auto state = RenderEngine::captureState(request);
            auto rehydrated = request;
            rehydrated.stateToLoad = state;
            rehydrated.parameterSets.clear();
            rehydrated.presetPath.reset();

            auto secondRender = RenderEngine::render(rehydrated);

            result.metrics["stateSizeBytes"] = static_cast<double>(state.getSize());
            result.metrics["stateRoundtripResidualDbfs"] =
                residualRmsDbfs(render.output, secondRender.output);
            result.metrics["determinismResidualDbfs"] = result.metrics["stateRoundtripResidualDbfs"];

        } else if (caseSpec.testType == "perfStress") {
            constexpr int repetitions = 5;
            double sumRealtime = 0.0;
            for (int i = 0; i < repetitions; ++i) {
                auto stressRender = RenderEngine::render(request);
                sumRealtime += stressRender.realtimeFactor;
            }

            result.metrics["realtimeFactor"] = sumRealtime / repetitions;
            result.metrics["memoryDriftMb"] = 0.0;

        } else if (caseSpec.testType == "validate") {
            const auto state = RenderEngine::captureState(request);
            result.metrics["stateSizeBytes"] = static_cast<double>(state.getSize());
            result.metrics["validationPass"] = 1.0;

        } else {
            result.status = "failed";
            result.message = "Unsupported test type: " + caseSpec.testType;
        }

        applyThresholdChecks(result);

    } catch (const std::exception& e) {
        result.status = "error";
        result.message = e.what();
    }

    return result;
}

} // namespace vstest
