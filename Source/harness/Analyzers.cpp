#include "Analyzers.h"

#include "RenderEngine.h"
#include "SignalGenerator.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <set>
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

double computeRmsRange(const juce::AudioBuffer<float>& buffer, int startSample, int sampleCount) {
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0 || sampleCount <= 0) {
        return 0.0;
    }

    const int start = std::clamp(startSample, 0, buffer.getNumSamples());
    const int end = std::clamp(start + sampleCount, start, buffer.getNumSamples());
    const int countPerChannel = end - start;
    if (countPerChannel <= 0) {
        return 0.0;
    }

    long double sumSquares = 0.0;
    const auto count = static_cast<long double>(buffer.getNumChannels() * countPerChannel);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* read = buffer.getReadPointer(ch);
        for (int i = start; i < end; ++i) {
            const auto sample = static_cast<long double>(read[i]);
            sumSquares += sample * sample;
        }
    }

    return std::sqrt(static_cast<double>(sumSquares / count));
}

double computeIntegratedLufsUngated(const juce::AudioBuffer<float>& buffer, int sampleRate,
                                    int startSample, int sampleCount) {
    if (sampleRate <= 0 || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0 ||
        sampleCount <= 0) {
        return -400.0;
    }

    const int start = std::clamp(startSample, 0, buffer.getNumSamples());
    const int end = std::clamp(start + sampleCount, start, buffer.getNumSamples());
    const int countPerChannel = end - start;
    if (countPerChannel <= 0) {
        return -400.0;
    }

    const auto hpCoefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        static_cast<double>(sampleRate), 38.13547087602444, 0.5003270373238773);
    const auto shelfCoefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        static_cast<double>(sampleRate), 1681.974450955533, 0.7071752369554196,
        juce::Decibels::decibelsToGain(4.0f));

    double weightedMeanSquareSum = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        juce::dsp::IIR::Filter<float> hp;
        juce::dsp::IIR::Filter<float> shelf;
        hp.coefficients = hpCoefficients;
        shelf.coefficients = shelfCoefficients;
        hp.reset();
        shelf.reset();

        const auto* read = buffer.getReadPointer(ch);
        long double channelEnergy = 0.0;
        for (int sample = start; sample < end; ++sample) {
            float value = hp.processSample(read[sample]);
            value = shelf.processSample(value);
            channelEnergy += static_cast<long double>(value) * static_cast<long double>(value);
        }

        const double channelMeanSquare = static_cast<double>(channelEnergy) /
                                         static_cast<double>(countPerChannel);
        weightedMeanSquareSum += channelMeanSquare;
    }

    const double channelAverageMeanSquare =
        weightedMeanSquareSum / static_cast<double>(buffer.getNumChannels());
    return -0.691 + 10.0 * std::log10(channelAverageMeanSquare + 1e-30);
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

void addRecommendation(CaseResult& result, const std::string& recommendation) {
    if (recommendation.empty()) {
        return;
    }

    if (std::find(result.recommendations.begin(), result.recommendations.end(), recommendation) ==
        result.recommendations.end()) {
        result.recommendations.push_back(recommendation);
    }
}

std::string recommendationForMetric(const std::string& metric) {
    if (metric == "aliasingRatioDb") {
        return "Reduce foldback by moving nonlinear stages to higher oversampling and adding steeper post-nonlinearity low-pass filtering.";
    }
    if (metric == "eqMaxErrorDb" || metric == "eqRmsErrorDb") {
        return "Check EQ coefficient design, sample-rate compensation, and gain/Q mapping against expected transfer curves.";
    }
    if (metric == "noiseFloorDbfs") {
        return "Lower idle noise by checking denormal handling, noise injection paths, and silence-state reset behavior.";
    }
    if (metric == "latencyErrorSamples") {
        return "Align internal delay compensation and report accurate latency via getLatencySamples().";
    }
    if (metric == "determinismResidualDbfs") {
        return "Remove nondeterministic state updates (unseeded random, time-dependent modulation) or reset state before render.";
    }
    if (metric == "thdnDb") {
        return "Reduce distortion products via lower nonlinear drive, improved oversampling, and stronger anti-alias filtering.";
    }
    if (metric == "imdDb") {
        return "Tune nonlinear stages for intermod reduction and validate gain staging under two-tone inputs.";
    }
    if (metric == "phaseDeviationDeg") {
        return "Re-check filter topology and phase response targets; avoid accidental extra phase-wrapping or delay ripple.";
    }
    if (metric == "bypassClickPeakDbfs") {
        return "Implement short crossfades or zero-crossing-aware switching when toggling bypass/module states.";
    }
    if (metric == "zipperArtifactDb") {
        return "Smooth parameter automation with slews/ramps and avoid step discontinuities inside processBlock.";
    }
    if (metric == "realtimeFactor") {
        return "Improve throughput by reducing per-block allocations, denormal costs, and expensive oversampling/filter paths.";
    }
    if (metric == "memoryDriftMb") {
        return "Investigate repeated allocations and lifetime leaks across render loops and plugin instances.";
    }
    if (metric == "presetGainSpreadDb") {
        return "Align preset output trims/makeup gain to a loudness target so preset switching stays level-matched.";
    }

    return "";
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
        addRecommendation(result, recommendationForMetric(metric));
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
        addRecommendation(result, recommendationForMetric(metric));
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
    checkUpperBound(result, "presetGainSpreadDb", "presetGainSpreadDbMax");

    checkLowerBound(result, "realtimeFactor", "minRealtimeFactor");
}

std::set<std::string> parsePresetExtensions(const nlohmann::json& extra) {
    std::set<std::string> extensions;
    if (extra.contains("presetExtensions") && extra["presetExtensions"].is_array()) {
        for (const auto& item : extra["presetExtensions"]) {
            if (!item.is_string()) {
                continue;
            }

            auto ext = juce::String(item.get<std::string>()).trim().toLowerCase().toStdString();
            if (ext.empty()) {
                continue;
            }

            if (ext.front() != '.') {
                ext.insert(ext.begin(), '.');
            }

            extensions.insert(ext);
        }
    }

    if (extensions.empty()) {
        extensions.insert(".vstpreset");
        extensions.insert(".aupreset");
        extensions.insert(".fxp");
        extensions.insert(".fxb");
    }

    return extensions;
}

std::vector<juce::File> collectPresetFiles(const nlohmann::json& extra) {
    std::vector<juce::File> files;

    if (extra.contains("presetFiles") && extra["presetFiles"].is_array()) {
        for (const auto& item : extra["presetFiles"]) {
            if (!item.is_string()) {
                continue;
            }

            juce::File preset(item.get<std::string>());
            if (!preset.existsAsFile()) {
                throw std::runtime_error("Preset file not found: " + preset.getFullPathName().toStdString());
            }
            files.push_back(preset);
        }
    }

    if (extra.contains("presetDirectory") && extra["presetDirectory"].is_string()) {
        juce::File presetDirectory(extra["presetDirectory"].get<std::string>());
        if (!presetDirectory.isDirectory()) {
            throw std::runtime_error("Preset directory not found: " +
                                     presetDirectory.getFullPathName().toStdString());
        }

        const auto recursive = extra.value("presetRecursive", true);
        const auto extensions = parsePresetExtensions(extra);
        for (const auto& entry :
             juce::RangedDirectoryIterator(presetDirectory, recursive, "*", juce::File::findFiles)) {
            auto ext = entry.getFile().getFileExtension().toLowerCase().toStdString();
            if (extensions.contains(ext)) {
                files.push_back(entry.getFile());
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFullPathName().toStdString() < b.getFullPathName().toStdString();
    });
    files.erase(std::unique(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
                    return a.getFullPathName() == b.getFullPathName();
                }),
                files.end());

    if (files.empty()) {
        throw std::runtime_error(
            "presetGain requires presetFiles[] or presetDirectory with at least one preset");
    }

    return files;
}

double energyAroundBin(const Spectrum& spectrum, int centerBin, int halfWidth) {
    const int start = std::max(0, centerBin - halfWidth);
    const int end = std::min(static_cast<int>(spectrum.magnitudes.size()) - 1, centerBin + halfWidth);
    double energy = 0.0;
    for (int bin = start; bin <= end; ++bin) {
        const auto magnitude = spectrum.magnitudes[bin];
        energy += magnitude * magnitude;
    }
    return energy;
}

int hzToBin(const Spectrum& spectrum, int sampleRate, double hz) {
    const double binHz = static_cast<double>(sampleRate) / static_cast<double>(spectrum.fftSize);
    return std::clamp(static_cast<int>(std::round(hz / std::max(1.0, binHz))), 1,
                      static_cast<int>(spectrum.magnitudes.size()) - 1);
}

double foldToNyquist(double hz, int sampleRate) {
    const double fs = static_cast<double>(sampleRate);
    const double nyquist = fs * 0.5;
    double folded = std::fmod(std::abs(hz), fs);
    if (folded > nyquist) {
        folded = fs - folded;
    }
    return folded;
}

struct FoldbackToneMeasurement {
    double toneHz = 0.0;
    double aliasRatioDb = 0.0;
    double foldbackEnergyDb = 0.0;
    int foldbackCount = 0;
};

FoldbackToneMeasurement analyzeFoldbackTone(const Spectrum& spectrum, int sampleRate, double toneHz,
                                            int harmonicMax, int binHalfWidth) {
    const int fundamentalBin = hzToBin(spectrum, sampleRate, toneHz);
    const double fundamentalEnergy = energyAroundBin(spectrum, fundamentalBin, std::max(1, binHalfWidth));

    double foldbackEnergy = 0.0;
    int foldbackCount = 0;
    for (int harmonic = 2; harmonic <= harmonicMax; ++harmonic) {
        const double foldedHz = foldToNyquist(toneHz * static_cast<double>(harmonic), sampleRate);
        const int aliasBin = hzToBin(spectrum, sampleRate, foldedHz);
        if (std::abs(aliasBin - fundamentalBin) <= (binHalfWidth + 1)) {
            continue;
        }

        foldbackEnergy += energyAroundBin(spectrum, aliasBin, binHalfWidth);
        ++foldbackCount;
    }

    FoldbackToneMeasurement measurement;
    measurement.toneHz = toneHz;
    measurement.aliasRatioDb =
        10.0 * std::log10((foldbackEnergy + 1.0e-30) / (fundamentalEnergy + 1.0e-30));
    measurement.foldbackEnergyDb = 10.0 * std::log10(foldbackEnergy + 1.0e-30);
    measurement.foldbackCount = foldbackCount;
    return measurement;
}

void runAliasingFoldbackScan(const CaseSpec& caseSpec, const RenderRequest& baseRequest,
                             CaseResult& result, unsigned int seed) {
    const int toneCount = std::clamp(caseSpec.extra.value("aliasToneCount", 8), 1, 32);
    const int harmonicMax = std::clamp(caseSpec.extra.value("aliasHarmonicsMax", 12), 2, 64);
    const int binHalfWidth = std::clamp(caseSpec.extra.value("aliasBinHalfWidth", 1), 0, 8);
    const double startRatio = std::clamp(caseSpec.extra.value("aliasStartNyquistRatio", 0.55), 0.05, 0.99);
    const double endRatio = std::clamp(caseSpec.extra.value("aliasEndNyquistRatio", 0.95), 0.06, 0.995);
    if (endRatio <= startRatio) {
        throw std::runtime_error("aliasEndNyquistRatio must be greater than aliasStartNyquistRatio");
    }

    const auto artifactDir = juce::File(caseSpec.artifactsDir);
    nlohmann::json details;
    details["method"] = "high_freq_foldback_scan_v1";
    details["sampleRate"] = caseSpec.sampleRate;
    details["toneCount"] = toneCount;
    details["toneRangeNyquistRatio"] = {startRatio, endRatio};
    details["harmonicsAnalyzed"] = harmonicMax;

    double worstRatioDb = -std::numeric_limits<double>::infinity();
    double sumRatioDb = 0.0;
    double worstToneHz = 0.0;
    int foldbackCount = 0;

    nlohmann::json tones = nlohmann::json::array();
    for (int index = 0; index < toneCount; ++index) {
        const double nyquist = static_cast<double>(caseSpec.sampleRate) * 0.5;
        const double t = (toneCount == 1) ? 1.0 : static_cast<double>(index) / (toneCount - 1);
        const double toneHz = std::clamp((startRatio + (endRatio - startRatio) * t) * nyquist, 20.0,
                                         nyquist * 0.995);

        SignalDefinition toneSignal = caseSpec.signal;
        toneSignal.type = "sine";
        toneSignal.frequencyHz = toneHz;
        toneSignal.durationSec = std::max(1.0, caseSpec.signal.durationSec);

        auto toneRequest = baseRequest;
        toneRequest.input =
            SignalGenerator::generate(toneSignal, caseSpec.sampleRate, caseSpec.channels,
                                      seed + static_cast<unsigned int>(index + 1) * 911u);

        const auto toneRender = RenderEngine::render(toneRequest);
        const auto spectrum = computeSpectrum(toneRender.output, 0);
        const auto measurement =
            analyzeFoldbackTone(spectrum, caseSpec.sampleRate, toneHz, harmonicMax, binHalfWidth);

        if (measurement.aliasRatioDb > worstRatioDb) {
            worstRatioDb = measurement.aliasRatioDb;
            worstToneHz = toneHz;
        }
        sumRatioDb += measurement.aliasRatioDb;
        foldbackCount += measurement.foldbackCount;

        nlohmann::json toneJson;
        toneJson["toneHz"] = measurement.toneHz;
        toneJson["aliasRatioDb"] = measurement.aliasRatioDb;
        toneJson["foldbackEnergyDb"] = measurement.foldbackEnergyDb;
        toneJson["foldbackCount"] = measurement.foldbackCount;
        tones.push_back(toneJson);
    }

    result.metrics["aliasingRatioDb"] = worstRatioDb;
    result.metrics["aliasingMeanRatioDb"] = sumRatioDb / static_cast<double>(toneCount);
    result.metrics["aliasingWorstToneHz"] = worstToneHz;
    result.metrics["aliasingFoldbackCount"] = static_cast<double>(foldbackCount);
    result.metrics["aliasingScannedToneCount"] = static_cast<double>(toneCount);

    details["tones"] = tones;
    const auto detailPath = artifactDir.getChildFile("aliasing_scan.json");
    detailPath.replaceWithText(details.dump(2));
    result.artifacts["aliasingScan"] = detailPath.getFullPathName().toStdString();
}

std::string sanitizeFileStem(const juce::String& value) {
    std::string out;
    out.reserve(static_cast<size_t>(value.length()));
    for (auto c : value.toStdString()) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_';
        out.push_back(safe ? c : '_');
    }

    if (out.empty()) {
        out = "preset";
    }
    return out;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string jsonValueToString(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }

    if (value.is_number_float()) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << value.get<double>();
        return out.str();
    }

    if (value.is_number_integer() || value.is_number_unsigned()) {
        return std::to_string(value.get<long long>());
    }

    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }

    return value.dump();
}

std::string formatDb(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }

    std::string escaped = "\"";
    for (const char c : value) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('"');
    return escaped;
}

bool hasPresetFileConfig(const nlohmann::json& extra) {
    if (extra.contains("presetDirectory") && extra["presetDirectory"].is_string() &&
        !extra["presetDirectory"].get<std::string>().empty()) {
        return true;
    }

    if (extra.contains("presetFiles") && extra["presetFiles"].is_array() &&
        !extra["presetFiles"].empty()) {
        return true;
    }

    return false;
}

struct PresetVariant {
    std::string id;
    std::string label;
    std::string source;
    std::optional<std::string> presetPath;
    std::optional<int> programIndex;
    std::vector<ParameterAssignment> parameterOverrides;
};

struct PresetGainRow {
    PresetVariant variant;
    double inputLufs = 0.0;
    double outputLufs = 0.0;
    double deltaVsInputDb = 0.0;
    double targetOutputLufs = 0.0;
    double recommendedTrimDb = 0.0;
    double outputPeakDbfs = 0.0;
    double outputRmsDbfs = 0.0;
    double realtimeFactor = 0.0;
};

std::vector<PresetVariant> collectPresetVariants(const CaseSpec& caseSpec) {
    std::vector<PresetVariant> variants;
    auto mode = toLower(caseSpec.extra.value("presetSource", "auto"));

    if (mode == "auto") {
        if (hasPresetFileConfig(caseSpec.extra)) {
            mode = "files";
        } else if (caseSpec.extra.contains("presetParamName")) {
            mode = "parameter";
        } else if (!RenderEngine::listPrograms(caseSpec.pluginPath, caseSpec.sampleRate,
                                               caseSpec.blockSize)
                        .empty()) {
            mode = "programs";
        } else {
            throw std::runtime_error(
                "presetGain auto mode could not find preset files, parameter selector, or plugin programs");
        }
    }

    if (mode == "files") {
        const auto presetFiles = collectPresetFiles(caseSpec.extra);
        for (size_t index = 0; index < presetFiles.size(); ++index) {
            PresetVariant variant;
            variant.id = "file_" + std::to_string(index);
            variant.label = presetFiles[index].getFileNameWithoutExtension().toStdString();
            variant.source = "file";
            variant.presetPath = presetFiles[index].getFullPathName().toStdString();
            variants.push_back(variant);
        }
        return variants;
    }

    if (mode == "programs") {
        const auto programs =
            RenderEngine::listPrograms(caseSpec.pluginPath, caseSpec.sampleRate, caseSpec.blockSize);
        if (programs.empty()) {
            throw std::runtime_error(
                "presetGain requested presetSource=programs but plugin exposes no programs");
        }

        std::set<int> allowedProgramIndices;
        if (caseSpec.extra.contains("programIndices") && caseSpec.extra["programIndices"].is_array()) {
            for (const auto& indexJson : caseSpec.extra["programIndices"]) {
                if (indexJson.is_number_integer()) {
                    allowedProgramIndices.insert(indexJson.get<int>());
                }
            }
        }

        const int programLimit = std::max(0, caseSpec.extra.value("programLimit", 0));
        int added = 0;
        for (const auto& program : programs) {
            if (!allowedProgramIndices.empty() &&
                !allowedProgramIndices.contains(program.index)) {
                continue;
            }

            PresetVariant variant;
            variant.id = "program_" + std::to_string(program.index);
            variant.label = program.name;
            variant.source = "program";
            variant.programIndex = program.index;
            variants.push_back(variant);
            ++added;

            if (programLimit > 0 && added >= programLimit) {
                break;
            }
        }

        if (variants.empty()) {
            throw std::runtime_error("presetGain program selection produced no matching programs");
        }

        return variants;
    }

    if (mode == "parameter") {
        const auto paramName = caseSpec.extra.value("presetParamName", "");
        if (paramName.empty()) {
            throw std::runtime_error("presetGain parameter mode requires presetParamName");
        }

        if (!caseSpec.extra.contains("presetParamValues") ||
            !caseSpec.extra["presetParamValues"].is_array() ||
            caseSpec.extra["presetParamValues"].empty()) {
            throw std::runtime_error(
                "presetGain parameter mode requires non-empty presetParamValues[]");
        }

        size_t index = 0;
        for (const auto& valueJson : caseSpec.extra["presetParamValues"]) {
            const std::string valueString = jsonValueToString(valueJson);

            PresetVariant variant;
            variant.id = "param_" + std::to_string(index);
            variant.label = paramName + "=" + valueString;
            variant.source = "parameter";
            variant.parameterOverrides.push_back({paramName, valueString});
            variants.push_back(variant);
            ++index;
        }

        return variants;
    }

    throw std::runtime_error("Unsupported presetSource for presetGain: " + mode);
}

void writePresetGainTables(const juce::File& artifactDir, const std::vector<PresetGainRow>& rows,
                           CaseResult& result) {
    const auto csvPath = artifactDir.getChildFile("preset_gain_adjustments.csv");
    std::ostringstream csv;
    csv << "preset,source,input_lufs,output_lufs,delta_vs_input_db,target_output_lufs,"
           "recommended_output_trim_db,output_peak_dbfs,output_rms_dbfs,realtime_factor,preset_path,program_index\n";

    for (const auto& row : rows) {
        csv << csvEscape(row.variant.label) << ",";
        csv << csvEscape(row.variant.source) << ",";
        csv << formatDb(row.inputLufs) << ",";
        csv << formatDb(row.outputLufs) << ",";
        csv << formatDb(row.deltaVsInputDb) << ",";
        csv << formatDb(row.targetOutputLufs) << ",";
        csv << formatDb(row.recommendedTrimDb) << ",";
        csv << formatDb(row.outputPeakDbfs) << ",";
        csv << formatDb(row.outputRmsDbfs) << ",";
        csv << formatDb(row.realtimeFactor) << ",";
        csv << csvEscape(row.variant.presetPath.value_or("")) << ",";
        csv << (row.variant.programIndex ? std::to_string(*row.variant.programIndex) : "") << "\n";
    }

    csvPath.replaceWithText(csv.str());
    result.artifacts["presetGainAdjustmentsCsv"] = csvPath.getFullPathName().toStdString();

    const auto markdownPath = artifactDir.getChildFile("preset_gain_adjustments.md");
    std::ostringstream markdown;
    markdown << "# Preset Gain Adjustments\n\n";
    markdown << "| Preset | Source | Output LUFS | Input LUFS | Delta (dB) | Target LUFS | Recommended Trim (dB) | Peak (dBFS) |\n";
    markdown << "|---|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& row : rows) {
        markdown << "| " << row.variant.label << " | " << row.variant.source << " | "
                 << formatDb(row.outputLufs) << " | " << formatDb(row.inputLufs) << " | "
                 << formatDb(row.deltaVsInputDb) << " | " << formatDb(row.targetOutputLufs)
                 << " | " << formatDb(row.recommendedTrimDb) << " | "
                 << formatDb(row.outputPeakDbfs) << " |\n";
    }

    markdown << "\n";
    markdown << "Recommended Trim (dB) is the per-preset output gain change needed to hit the target loudness.\n";

    markdownPath.replaceWithText(markdown.str());
    result.artifacts["presetGainAdjustmentsMd"] =
        markdownPath.getFullPathName().toStdString();
}

void runPresetGainSpread(const CaseSpec& caseSpec, const RenderRequest& baseRequest, CaseResult& result,
                         unsigned int seed) {
    const auto variants = collectPresetVariants(caseSpec);
    const bool keepCaseParameterSets = caseSpec.extra.value("presetApplyParameterSets", false);
    const bool writePerPresetAudio = caseSpec.extra.value("writePresetAudio", false);

    const bool presetUseSineInput = caseSpec.extra.value("presetUseSineInput", true);
    const double targetOutputDeltaDb = caseSpec.extra.value("targetOutputDeltaDb", 1.0);
    const double warmupSec = std::max(0.0, caseSpec.extra.value("measurementWarmupSec", 0.5));
    const double measurementDurationSec = std::max(0.0, caseSpec.extra.value("measurementDurationSec", 0.0));

    SignalDefinition analysisSignal = caseSpec.signal;
    if (presetUseSineInput || analysisSignal.type != "sine") {
        analysisSignal.type = "sine";
        analysisSignal.frequencyHz = caseSpec.extra.value("presetFrequencyHz", 1000.0);
        analysisSignal.durationSec =
            caseSpec.extra.value("presetDurationSec", std::max(3.0, caseSpec.signal.durationSec));
        analysisSignal.levelDbfs =
            caseSpec.extra.value("presetLevelDbfs", caseSpec.signal.levelDbfs);
    }

    const auto baseInput =
        SignalGenerator::generate(analysisSignal, caseSpec.sampleRate, caseSpec.channels, seed);
    const int measurementStartSample = static_cast<int>(std::round(warmupSec * caseSpec.sampleRate));
    const int inputRemainingSamples =
        std::max(1, baseInput.getNumSamples() - measurementStartSample);
    const int requestedMeasurementSamples =
        (measurementDurationSec > 0.0)
            ? std::clamp(
                  static_cast<int>(std::round(measurementDurationSec * caseSpec.sampleRate)), 1,
                  inputRemainingSamples)
            : inputRemainingSamples;

    const double inputRmsDbfs =
        linearToDb(computeRmsRange(baseInput, measurementStartSample, requestedMeasurementSamples));
    const double inputLufs = computeIntegratedLufsUngated(
        baseInput, caseSpec.sampleRate, measurementStartSample, requestedMeasurementSamples);
    const double targetOutputLufs = inputLufs + targetOutputDeltaDb;

    const auto artifactDir = juce::File(caseSpec.artifactsDir);
    nlohmann::json detail;
    detail["method"] = "preset_loudness_alignment_v2";
    detail["sampleRate"] = caseSpec.sampleRate;
    detail["blockSize"] = caseSpec.blockSize;
    detail["channels"] = caseSpec.channels;
    detail["signal"] = {
        {"type", analysisSignal.type},
        {"frequencyHz", analysisSignal.frequencyHz},
        {"levelDbfs", analysisSignal.levelDbfs},
        {"durationSec", analysisSignal.durationSec},
    };
    detail["warmupSec"] = warmupSec;
    detail["measurementDurationSec"] = measurementDurationSec;
    detail["targetOutputDeltaDb"] = targetOutputDeltaDb;
    detail["inputRmsDbfs"] = inputRmsDbfs;
    detail["inputLufs"] = inputLufs;
    detail["targetOutputLufs"] = targetOutputLufs;

    double minLufs = std::numeric_limits<double>::infinity();
    double maxLufs = -std::numeric_limits<double>::infinity();
    double sumLufs = 0.0;
    double sumRecommendedTrimDb = 0.0;
    double sumRealtimeFactor = 0.0;
    std::string minPreset;
    std::string maxPreset;

    std::vector<PresetGainRow> rows;
    rows.reserve(variants.size());

    nlohmann::json variantsJson = nlohmann::json::array();
    for (size_t index = 0; index < variants.size(); ++index) {
        auto presetRequest = baseRequest;
        presetRequest.input = baseInput;
        presetRequest.presetPath = variants[index].presetPath;
        presetRequest.programIndex = variants[index].programIndex;

        if (!keepCaseParameterSets) {
            presetRequest.parameterSets.clear();
        }
        presetRequest.parameterSets.insert(presetRequest.parameterSets.end(),
                                           variants[index].parameterOverrides.begin(),
                                           variants[index].parameterOverrides.end());

        const auto rendered = RenderEngine::render(presetRequest);
        const int outputRemainingSamples =
            std::max(1, rendered.output.getNumSamples() - measurementStartSample);
        const int outputMeasurementSamples =
            std::clamp(requestedMeasurementSamples, 1, outputRemainingSamples);

        const double outputRmsDbfs = linearToDb(
            computeRmsRange(rendered.output, measurementStartSample, outputMeasurementSamples));
        const double outputLufs = computeIntegratedLufsUngated(
            rendered.output, caseSpec.sampleRate, measurementStartSample,
            outputMeasurementSamples);
        const double peakDbfs = linearToDb(computePeak(rendered.output));

        if (outputLufs < minLufs) {
            minLufs = outputLufs;
            minPreset = variants[index].label;
        }
        if (outputLufs > maxLufs) {
            maxLufs = outputLufs;
            maxPreset = variants[index].label;
        }

        const double deltaVsInputDb = outputLufs - inputLufs;
        const double recommendedTrimDb = targetOutputLufs - outputLufs;

        sumLufs += outputLufs;
        sumRecommendedTrimDb += recommendedTrimDb;
        sumRealtimeFactor += rendered.realtimeFactor;

        PresetGainRow row;
        row.variant = variants[index];
        row.inputLufs = inputLufs;
        row.outputLufs = outputLufs;
        row.deltaVsInputDb = deltaVsInputDb;
        row.targetOutputLufs = targetOutputLufs;
        row.recommendedTrimDb = recommendedTrimDb;
        row.outputPeakDbfs = peakDbfs;
        row.outputRmsDbfs = outputRmsDbfs;
        row.realtimeFactor = rendered.realtimeFactor;
        rows.push_back(row);

        nlohmann::json variantJson;
        variantJson["id"] = variants[index].id;
        variantJson["label"] = variants[index].label;
        variantJson["source"] = variants[index].source;
        variantJson["presetPath"] = variants[index].presetPath.value_or("");
        variantJson["programIndex"] =
            variants[index].programIndex ? nlohmann::json(*variants[index].programIndex)
                                         : nlohmann::json();
        variantJson["outputPeakDbfs"] = peakDbfs;
        variantJson["outputRmsDbfs"] = outputRmsDbfs;
        variantJson["outputLufs"] = outputLufs;
        variantJson["deltaVsInputDb"] = deltaVsInputDb;
        variantJson["recommendedTrimDb"] = recommendedTrimDb;
        variantJson["realtimeFactor"] = rendered.realtimeFactor;
        variantsJson.push_back(variantJson);

        if (writePerPresetAudio) {
            const auto presetOutput = artifactDir.getChildFile("presets")
                                        .getChildFile(std::to_string(index + 1) + "_" +
                                                      sanitizeFileStem(juce::String(variants[index].label)) +
                                                      ".wav");
            presetOutput.getParentDirectory().createDirectory();
            writeWav(rendered.output, rendered.sampleRate, presetOutput);
        }
    }

    std::sort(rows.begin(), rows.end(), [](const PresetGainRow& a, const PresetGainRow& b) {
        return a.recommendedTrimDb > b.recommendedTrimDb;
    });

    const double spreadDb = maxLufs - minLufs;
    result.metrics["presetCount"] = static_cast<double>(rows.size());
    result.metrics["presetGainSpreadDb"] = spreadDb;
    result.metrics["presetInputRmsDbfs"] = inputRmsDbfs;
    result.metrics["presetInputLufs"] = inputLufs;
    result.metrics["presetTargetLufs"] = targetOutputLufs;
    result.metrics["presetGainMinLufs"] = minLufs;
    result.metrics["presetGainMaxLufs"] = maxLufs;
    result.metrics["presetGainMeanLufs"] = sumLufs / static_cast<double>(rows.size());
    result.metrics["presetRecommendedTrimMeanDb"] =
        sumRecommendedTrimDb / static_cast<double>(rows.size());
    result.metrics["presetRealtimeFactorMean"] =
        sumRealtimeFactor / static_cast<double>(rows.size());

    detail["spreadDb"] = spreadDb;
    detail["quietestPreset"] = minPreset;
    detail["loudestPreset"] = maxPreset;
    detail["variants"] = variantsJson;

    const auto detailPath = artifactDir.getChildFile("preset_gain_summary.json");
    detailPath.replaceWithText(detail.dump(2));
    result.artifacts["presetGainSummary"] = detailPath.getFullPathName().toStdString();
    writePresetGainTables(artifactDir, rows, result);
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
    addThreshold(result.thresholds, "presetGainSpreadDbMax", caseSpec.thresholds.presetGainSpreadDbMax);

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

void ensureFailureRecommendations(CaseResult& result) {
    if (result.status == "passed" || result.status == "skipped") {
        return;
    }

    if (result.status == "crashed") {
        addRecommendation(
            result,
            "Use artifacts/repro.sh and worker.log to isolate the crash path, then add input/state guards around the failing processing stage.");
    } else if (result.status == "error") {
        addRecommendation(
            result,
            "Fix the runtime/setup error first (plugin path, preset loading, parameter mapping, or file permissions), then rerun the same case id.");
    }

    addRecommendation(
        result,
        "Re-run the failing case with `vst-test run --suite <suite.json> --select <case-id>` and inspect its artifact bundle for root cause.");
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
                ensureFailureRecommendations(result);
                return result;
            }
        }

        auto request = buildRequest(caseSpec, seed);
        const auto artifactDir = juce::File(caseSpec.artifactsDir);
        result.artifacts["reproCmd"] = artifactDir.getChildFile("repro.sh").getFullPathName().toStdString();
        writeReproScript(caseSpec, result);

        if (caseSpec.testType == "presetGain") {
            runPresetGainSpread(caseSpec, request, result, seed);
            applyThresholdChecks(result);
            ensureFailureRecommendations(result);
            return result;
        }

        auto render = RenderEngine::render(request);
        const auto outputPath = artifactDir.getChildFile("output.wav");
        writeWav(render.output, render.sampleRate, outputPath);

        result.artifacts["renderedAudio"] = outputPath.getFullPathName().toStdString();

        result.metrics["outputPeakDbfs"] = linearToDb(computePeak(render.output));
        result.metrics["outputRmsDbfs"] = linearToDb(computeRms(render.output));
        result.metrics["realtimeFactor"] = render.realtimeFactor;

        if (caseSpec.testType == "load") {
            result.metrics["loadPass"] = 1.0;

        } else if (caseSpec.testType == "determinism") {
            auto render2 = RenderEngine::render(request);
            result.metrics["determinismResidualDbfs"] = residualRmsDbfs(render.output, render2.output);

        } else if (caseSpec.testType == "aliasing") {
            runAliasingFoldbackScan(caseSpec, request, result, seed);

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
        ensureFailureRecommendations(result);

    } catch (const std::exception& e) {
        result.status = "error";
        result.message = e.what();
        ensureFailureRecommendations(result);
    }

    return result;
}

} // namespace vstest
