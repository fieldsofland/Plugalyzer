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
#include <random>
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

// Warmup skipped at the head of rendered output before spectral/level analysis, so
// parameter-smoothing settle transients (engine ramping from init to case values)
// do not pollute distortion/aliasing measurements.
double analysisWarmupSeconds(const nlohmann::json& extra) {
    return std::max(0.0, extra.value("analysisWarmupSec", 0.75));
}

juce::AudioBuffer<float> trimWarmup(const juce::AudioBuffer<float>& buffer, int sampleRate,
                                    double warmupSec) {
    constexpr int minAnalysisSamples = 256;
    const int numSamples = buffer.getNumSamples();
    int skip = static_cast<int>(std::round(warmupSec * sampleRate));
    skip = std::clamp(skip, 0, std::max(0, numSamples - minAnalysisSamples));

    juce::AudioBuffer<float> trimmed(buffer.getNumChannels(), numSamples - skip);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        trimmed.copyFrom(ch, 0, buffer, ch, skip, numSamples - skip);
    }

    return trimmed;
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

// Cross-correlates input against output over lags in [-maxLag, +maxLag].
// Positive result: output is late relative to input; negative: output is early.
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

    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        const int start = std::max(0, -lag);
        const int end = maxSamples - std::max(0, lag);
        if (end <= start) {
            continue;
        }

        double corr = 0.0;
        for (int i = start; i < end; ++i) {
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

    // Full-path mkdirs before writing, plus one retry: parallel workers can race on
    // shared parent directory creation, producing transient create failures.
    path.getParentDirectory().createDirectory();
    auto stream = path.createOutputStream();
    if (!stream) {
        juce::Thread::sleep(50);
        path.getParentDirectory().createDirectory();
        stream = path.createOutputStream();
    }

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
    if (metric == "aliasingRatioDb" || metric == "aliasWorstToneDbfs") {
        return "Reduce foldback by moving nonlinear stages to higher oversampling and adding steeper post-nonlinearity low-pass filtering.";
    }
    if (metric == "abxLoudnessDeltaDb") {
        return "Match A/B loudness more tightly (within ~0.2 dB) before subjective comparison to avoid level-bias in listening decisions.";
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
    if (metric == "saturationWorstThdDb") {
        return "Tune nonlinearity drive and tone shaping so distortion grows musically across level without sudden harsh onset.";
    }
    if (metric == "saturationOddEvenImbalanceDb") {
        return "Adjust transfer asymmetry and bias to reach the desired odd/even harmonic balance for the intended analog character.";
    }
    if (metric == "streamToggleClickDbfs") {
        return "Add short crossfades or parameter smoothing on bypass/state toggles so in-stream switching stays click-free.";
    }
    if (metric == "minWindowCorrelation" || metric == "maxSideMidRatioDb" ||
        metric == "phaseCollapseWindows") {
        return "Check stereo path polarity and mid/side balance; inverted or over-wide channels collapse when summed to mono.";
    }
    if (metric == "worstBlockRealtimeFactor" || metric == "p95BlockRealtimeFactor") {
        return "Reduce worst-case block cost (avoid audio-thread allocations, amortize filter/FFT updates) to keep per-block headroom.";
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
    checkUpperBound(result, "abxLoudnessDeltaDb", "abxLoudnessDeltaDbMax");
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
    checkUpperBound(result, "saturationWorstThdDb", "saturationWorstThdDbMax");
    checkUpperBound(result, "saturationOddEvenImbalanceDb", "saturationOddEvenImbalanceDbMax");
    checkUpperBound(result, "aliasWorstToneDbfs", "aliasWorstToneDbfsMax");
    checkUpperBound(result, "streamToggleClickDbfs", "streamToggleClickDbfsMax");
    checkUpperBound(result, "maxSideMidRatioDb", "sideMidRatioDbMax");
    checkUpperBound(result, "phaseCollapseWindows", "phaseCollapseWindowsMax");

    checkLowerBound(result, "realtimeFactor", "minRealtimeFactor");
    checkLowerBound(result, "minWindowCorrelation", "stereoCorrelationMin");
    checkLowerBound(result, "worstBlockRealtimeFactor", "minWorstBlockRealtimeFactor");
    checkLowerBound(result, "p95BlockRealtimeFactor", "minP95BlockRealtimeFactor");
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
    // Never clamp the bin spacing itself: for analysis windows longer than 1 s
    // (fftSize > sampleRate) it drops below 1.0 Hz, and a std::max(1.0, binHz)
    // "guard" here silently remapped every lookup to round(hz) — i.e. the
    // fundamental/harmonic energies were read from unrelated noise bins.
    const double binHz =
        static_cast<double>(sampleRate) / static_cast<double>(std::max(1, spectrum.fftSize));
    return std::clamp(static_cast<int>(std::round(hz / binHz)), 1,
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
    double worstAliasComponentDbfs = -400.0;
    int foldbackCount = 0;
};

// Approximate absolute component level (dBFS) from Hann-windowed main-lobe energy.
// For a sine of amplitude A the Hann main lobe carries energy ~= (A*N/4)^2 * 1.5.
double componentDbfsFromLobeEnergy(double lobeEnergy, int fftSize) {
    const double amplitude =
        4.0 * std::sqrt(std::max(0.0, lobeEnergy) / 1.5) / static_cast<double>(fftSize);
    return linearToDb(amplitude);
}

FoldbackToneMeasurement analyzeFoldbackTone(const Spectrum& spectrum, int sampleRate, double toneHz,
                                            int harmonicMax, int binHalfWidth) {
    const int fundamentalBin = hzToBin(spectrum, sampleRate, toneHz);
    const double fundamentalEnergy = energyAroundBin(spectrum, fundamentalBin, std::max(1, binHalfWidth));

    double foldbackEnergy = 0.0;
    double worstAliasEnergy = 0.0;
    int foldbackCount = 0;
    for (int harmonic = 2; harmonic <= harmonicMax; ++harmonic) {
        const double foldedHz = foldToNyquist(toneHz * static_cast<double>(harmonic), sampleRate);
        const int aliasBin = hzToBin(spectrum, sampleRate, foldedHz);
        if (std::abs(aliasBin - fundamentalBin) <= (binHalfWidth + 1)) {
            continue;
        }

        const double aliasEnergy = energyAroundBin(spectrum, aliasBin, binHalfWidth);
        foldbackEnergy += aliasEnergy;
        worstAliasEnergy = std::max(worstAliasEnergy, aliasEnergy);
        ++foldbackCount;
    }

    FoldbackToneMeasurement measurement;
    measurement.toneHz = toneHz;
    measurement.aliasRatioDb =
        10.0 * std::log10((foldbackEnergy + 1.0e-30) / (fundamentalEnergy + 1.0e-30));
    measurement.foldbackEnergyDb = 10.0 * std::log10(foldbackEnergy + 1.0e-30);
    measurement.worstAliasComponentDbfs =
        componentDbfsFromLobeEnergy(worstAliasEnergy, spectrum.fftSize);
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

    const double warmupSec = analysisWarmupSeconds(caseSpec.extra);

    const auto artifactDir = juce::File(caseSpec.artifactsDir);
    nlohmann::json details;
    details["method"] = "high_freq_foldback_scan_v2";
    details["sampleRate"] = caseSpec.sampleRate;
    details["toneCount"] = toneCount;
    details["toneRangeNyquistRatio"] = {startRatio, endRatio};
    details["harmonicsAnalyzed"] = harmonicMax;
    details["analysisWarmupSec"] = warmupSec;
    details["stimulusLevelDbfs"] = caseSpec.signal.levelDbfs;

    double worstRatioDb = -std::numeric_limits<double>::infinity();
    double sumRatioDb = 0.0;
    double worstToneHz = 0.0;
    double worstAliasDbfs = -400.0;
    double worstAliasVsOutputRmsDb = -400.0;
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
        toneSignal.durationSec = std::max(1.0, caseSpec.signal.durationSec) + warmupSec;

        auto toneRequest = baseRequest;
        toneRequest.input =
            SignalGenerator::generate(toneSignal, caseSpec.sampleRate, caseSpec.channels,
                                      seed + static_cast<unsigned int>(index + 1) * 911u);

        const auto toneRender = RenderEngine::render(toneRequest);
        const auto analysisBuffer = trimWarmup(toneRender.output, caseSpec.sampleRate, warmupSec);
        const auto spectrum = computeSpectrum(analysisBuffer, 0);
        const auto measurement =
            analyzeFoldbackTone(spectrum, caseSpec.sampleRate, toneHz, harmonicMax, binHalfWidth);
        const double toneOutputRmsDbfs = linearToDb(computeRms(analysisBuffer));

        if (measurement.aliasRatioDb > worstRatioDb) {
            worstRatioDb = measurement.aliasRatioDb;
            worstToneHz = toneHz;
        }
        if (measurement.worstAliasComponentDbfs > worstAliasDbfs) {
            worstAliasDbfs = measurement.worstAliasComponentDbfs;
            worstAliasVsOutputRmsDb = measurement.worstAliasComponentDbfs - toneOutputRmsDbfs;
        }
        sumRatioDb += measurement.aliasRatioDb;
        foldbackCount += measurement.foldbackCount;

        nlohmann::json toneJson;
        toneJson["toneHz"] = measurement.toneHz;
        toneJson["aliasRatioDb"] = measurement.aliasRatioDb;
        toneJson["foldbackEnergyDb"] = measurement.foldbackEnergyDb;
        toneJson["worstAliasComponentDbfs"] = measurement.worstAliasComponentDbfs;
        toneJson["outputRmsDbfs"] = toneOutputRmsDbfs;
        toneJson["foldbackCount"] = measurement.foldbackCount;
        tones.push_back(toneJson);
    }

    result.metrics["aliasingRatioDb"] = worstRatioDb;
    result.metrics["aliasingMeanRatioDb"] = sumRatioDb / static_cast<double>(toneCount);
    result.metrics["aliasingWorstToneHz"] = worstToneHz;
    result.metrics["aliasWorstToneDbfs"] = worstAliasDbfs;
    result.metrics["aliasWorstToneVsOutputRmsDb"] = worstAliasVsOutputRmsDb;
    result.metrics["aliasingFoldbackCount"] = static_cast<double>(foldbackCount);
    result.metrics["aliasingScannedToneCount"] = static_cast<double>(toneCount);

    details["tones"] = tones;
    const auto detailPath = artifactDir.getChildFile("aliasing_scan.json");
    detailPath.replaceWithText(details.dump(2));
    result.artifacts["aliasingScan"] = detailPath.getFullPathName().toStdString();
}

juce::AudioBuffer<float> copyWithGain(const juce::AudioBuffer<float>& source, double gainDb) {
    juce::AudioBuffer<float> copy;
    copy.makeCopyOf(source);
    copy.applyGain(juce::Decibels::decibelsToGain(static_cast<float>(gainDb)));
    return copy;
}

std::string formatDb(double value);
std::string csvEscape(const std::string& value);

std::vector<double> makeLevelSeries(double startDbfs, double endDbfs, double stepDb) {
    if (stepDb <= 0.0) {
        throw std::runtime_error("saturationStepDb must be > 0");
    }

    std::vector<double> levels;
    const int maxPoints = 256;
    if (startDbfs <= endDbfs) {
        for (double v = startDbfs; v <= endDbfs + 1e-9; v += stepDb) {
            levels.push_back(v);
            if (static_cast<int>(levels.size()) >= maxPoints) {
                break;
            }
        }
    } else {
        for (double v = startDbfs; v >= endDbfs - 1e-9; v -= stepDb) {
            levels.push_back(v);
            if (static_cast<int>(levels.size()) >= maxPoints) {
                break;
            }
        }
    }

    if (levels.empty()) {
        levels.push_back(startDbfs);
    }
    return levels;
}

void runAbxPreparation(const CaseSpec& caseSpec, const RenderRequest& baseRequest, CaseResult& result,
                       unsigned int seed) {
    const int trialCount = std::clamp(caseSpec.extra.value("abxTrials", 12), 2, 200);
    const bool writeTrialAudio = caseSpec.extra.value("abxWriteTrialAudio", true);
    const bool keepCaseParameterSets = caseSpec.extra.value("abxKeepCaseParameterSets", true);
    const bool useSineInput = caseSpec.extra.value("abxUseSineInput", false);
    const bool hasProgramPair =
        caseSpec.extra.contains("abxProgramA") && caseSpec.extra.contains("abxProgramB");

    SignalDefinition signal = caseSpec.signal;
    if (useSineInput) {
        signal.type = "sine";
        signal.frequencyHz = caseSpec.extra.value("abxFrequencyHz", 1000.0);
        signal.levelDbfs = caseSpec.extra.value("abxLevelDbfs", caseSpec.signal.levelDbfs);
        signal.durationSec = caseSpec.extra.value("abxDurationSec", caseSpec.signal.durationSec);
    }

    auto analysisInput =
        SignalGenerator::generate(signal, caseSpec.sampleRate, caseSpec.channels, seed + 991u);

    juce::AudioBuffer<float> audioA;
    juce::AudioBuffer<float> audioB;
    std::string labelA = "A";
    std::string labelB = "B";

    if (hasProgramPair) {
        RenderRequest reqA = baseRequest;
        RenderRequest reqB = baseRequest;
        reqA.input = analysisInput;
        reqB.input = analysisInput;
        reqA.programIndex = caseSpec.extra.value("abxProgramA", 0);
        reqB.programIndex = caseSpec.extra.value("abxProgramB", 1);
        reqA.presetPath.reset();
        reqB.presetPath.reset();

        if (!keepCaseParameterSets) {
            reqA.parameterSets.clear();
            reqB.parameterSets.clear();
        }

        auto renderedA = RenderEngine::render(reqA);
        auto renderedB = RenderEngine::render(reqB);
        audioA = renderedA.output;
        audioB = renderedB.output;
        labelA = caseSpec.extra.value("abxLabelA", "Program A");
        labelB = caseSpec.extra.value("abxLabelB", "Program B");
    } else {
        RenderRequest wetReq = baseRequest;
        wetReq.input = analysisInput;
        if (!keepCaseParameterSets) {
            wetReq.parameterSets.clear();
        }
        auto renderedWet = RenderEngine::render(wetReq);
        audioA = analysisInput;
        audioB = renderedWet.output;
        labelA = caseSpec.extra.value("abxLabelA", "Dry");
        labelB = caseSpec.extra.value("abxLabelB", "Wet");
    }

    const double warmupSec = std::max(0.0, caseSpec.extra.value("abxWarmupSec", 0.25));
    const double measureSec = std::max(0.0, caseSpec.extra.value("abxMeasureSec", 0.0));

    const int startSample = static_cast<int>(std::round(warmupSec * caseSpec.sampleRate));
    const int maxWindowSamples = std::max(1, audioA.getNumSamples() - startSample);
    const int requestedSamples =
        (measureSec > 0.0)
            ? std::clamp(static_cast<int>(std::round(measureSec * caseSpec.sampleRate)), 1,
                         maxWindowSamples)
            : maxWindowSamples;
    const int windowA = std::clamp(requestedSamples, 1, std::max(1, audioA.getNumSamples() - startSample));
    const int windowB = std::clamp(requestedSamples, 1, std::max(1, audioB.getNumSamples() - startSample));

    const double lufsA =
        computeIntegratedLufsUngated(audioA, caseSpec.sampleRate, startSample, windowA);
    const double lufsBPre =
        computeIntegratedLufsUngated(audioB, caseSpec.sampleRate, startSample, windowB);
    const double gainToMatchDb = lufsA - lufsBPre;
    auto audioBMatched = copyWithGain(audioB, gainToMatchDb);
    const double lufsBPost =
        computeIntegratedLufsUngated(audioBMatched, caseSpec.sampleRate, startSample, windowB);

    const auto artifactDir = juce::File(caseSpec.artifactsDir);
    const auto pathA = artifactDir.getChildFile("abx_A.wav");
    const auto pathB = artifactDir.getChildFile("abx_B.wav");
    writeWav(audioA, caseSpec.sampleRate, pathA);
    writeWav(audioBMatched, caseSpec.sampleRate, pathB);

    result.artifacts["abxA"] = pathA.getFullPathName().toStdString();
    result.artifacts["abxB"] = pathB.getFullPathName().toStdString();

    const auto blindCsvPath = artifactDir.getChildFile("abx_trials_blind.csv");
    const auto answerCsvPath = artifactDir.getChildFile("abx_trials_answers.csv");
    const auto instructionsPath = artifactDir.getChildFile("abx_instructions.md");
    const auto trialDir = artifactDir.getChildFile("abx_trials");
    trialDir.createDirectory();

    std::mt19937 rng(seed + 4242u);
    std::bernoulli_distribution pickB(0.5);

    std::ostringstream blindCsv;
    std::ostringstream answerCsv;
    blindCsv << "trial,x_audio_file,notes\n";
    answerCsv << "trial,answer\n";

    for (int trial = 1; trial <= trialCount; ++trial) {
        const bool isB = pickB(rng);
        const auto trialName =
            "X_" + juce::String(trial).paddedLeft('0', 2).toStdString() + ".wav";
        const auto trialPath = trialDir.getChildFile(trialName);

        if (writeTrialAudio) {
            writeWav(isB ? audioBMatched : audioA, caseSpec.sampleRate, trialPath);
        }

        blindCsv << trial << "," << csvEscape("abx_trials/" + trialName) << ",\n";
        answerCsv << trial << "," << (isB ? "B" : "A") << "\n";
    }

    blindCsvPath.replaceWithText(blindCsv.str());
    answerCsvPath.replaceWithText(answerCsv.str());

    std::ostringstream instructions;
    instructions << "# ABX Listening Pack\n\n";
    instructions << "- `A`: " << labelA << " (`abx_A.wav`)\n";
    instructions << "- `B`: " << labelB << " (`abx_B.wav`, loudness-matched)\n";
    instructions << "- Trials listed in `abx_trials_blind.csv`\n";
    instructions << "- Answer key in `abx_trials_answers.csv` (keep hidden during evaluation)\n\n";
    instructions << "Loudness alignment:\n";
    instructions << "- A LUFS: " << formatDb(lufsA) << "\n";
    instructions << "- B LUFS before match: " << formatDb(lufsBPre) << "\n";
    instructions << "- B gain applied: " << formatDb(gainToMatchDb) << " dB\n";
    instructions << "- B LUFS after match: " << formatDb(lufsBPost) << "\n";
    instructionsPath.replaceWithText(instructions.str());

    result.artifacts["abxTrialsBlind"] = blindCsvPath.getFullPathName().toStdString();
    result.artifacts["abxTrialsAnswers"] = answerCsvPath.getFullPathName().toStdString();
    result.artifacts["abxInstructions"] = instructionsPath.getFullPathName().toStdString();

    result.metrics["abxTrialCount"] = static_cast<double>(trialCount);
    result.metrics["abxLufsA"] = lufsA;
    result.metrics["abxLufsBBeforeMatch"] = lufsBPre;
    result.metrics["abxLufsBAfterMatch"] = lufsBPost;
    result.metrics["abxGainAppliedDb"] = gainToMatchDb;
    result.metrics["abxLoudnessDeltaDb"] = std::abs(lufsA - lufsBPost);
}

struct SaturationPoint {
    double inputDbfs = 0.0;
    double outputRmsDbfs = 0.0;
    double outputPeakDbfs = 0.0;
    double thdDb = 0.0;
    double evenOddBalanceDb = 0.0;
    double h2Db = 0.0;
    double h3Db = 0.0;
    int fundamentalBin = 0;
    int spectrumPeakBin = 0;
    bool fundamentalAtStimulusBin = true;
};

void runSaturationFingerprint(const CaseSpec& caseSpec, const RenderRequest& baseRequest,
                              CaseResult& result, unsigned int seed) {
    const double startDbfs = caseSpec.extra.value("saturationStartDbfs", -36.0);
    const double endDbfs = caseSpec.extra.value("saturationEndDbfs", -6.0);
    const double stepDb = caseSpec.extra.value("saturationStepDb", 3.0);
    const double frequencyHz = caseSpec.extra.value("saturationFrequencyHz", 1000.0);
    const double durationSec =
        std::max(1.0, caseSpec.extra.value("saturationDurationSec", caseSpec.signal.durationSec));
    const int harmonicMax = std::clamp(caseSpec.extra.value("saturationHarmonicMax", 10), 2, 32);
    const double warmupSec = analysisWarmupSeconds(caseSpec.extra);

    // Each level is rendered as its own segment (fresh plugin instance, level set
    // from sample 0). Only the steady-state tail may be analyzed: the head of the
    // segment contains the level step plus parameter-smoothing/amp settle. Skip at
    // least 250 ms or 40% of the segment (whichever is larger), and guarantee at
    // least 1.0 s of steady-state audio per level, lengthening the segment if the
    // configured duration is too short.
    constexpr double minSteadySec = 1.0;
    constexpr double minSkipSec = 0.25;
    const auto steadySkipFor = [warmupSec](double segment) {
        return std::max({minSkipSec, warmupSec, 0.4 * segment});
    };

    double segmentSec = durationSec + warmupSec;
    double steadySkipSec = steadySkipFor(segmentSec);
    if (segmentSec - steadySkipSec < minSteadySec) {
        segmentSec = std::max(minSteadySec / 0.6, std::max(minSkipSec, warmupSec) + minSteadySec);
        steadySkipSec = steadySkipFor(segmentSec);
    }
    const double steadyAnalysisSec = segmentSec - steadySkipSec;

    const auto levels = makeLevelSeries(startDbfs, endDbfs, stepDb);
    std::vector<SaturationPoint> points;
    points.reserve(levels.size());

    for (size_t index = 0; index < levels.size(); ++index) {
        SignalDefinition testSignal;
        testSignal.id = "sat_level_" + std::to_string(index);
        testSignal.type = "sine";
        testSignal.frequencyHz = frequencyHz;
        testSignal.levelDbfs = levels[index];
        testSignal.durationSec = segmentSec;
        testSignal.startHz = caseSpec.signal.startHz;
        testSignal.endHz = caseSpec.signal.endHz;

        RenderRequest request = baseRequest;
        request.input = SignalGenerator::generate(testSignal, caseSpec.sampleRate, caseSpec.channels,
                                                  seed + static_cast<unsigned int>(index + 1) * 313u);

        const auto render = RenderEngine::render(request);
        const auto analysisBuffer = trimWarmup(render.output, caseSpec.sampleRate, steadySkipSec);
        const auto spectrum = computeSpectrum(analysisBuffer, 0);
        // The fundamental is always measured at the stimulus frequency bin — never a
        // searched peak, which can land on junk when the output is noisy. As a
        // sanity check, verify the spectrum's dominant bin sits inside the Hann
        // main lobe around that bin; a mismatch means the output no longer carries
        // the stimulus tone (detune/modulation) and the point is flagged.
        const int fundamentalBin = hzToBin(spectrum, caseSpec.sampleRate, frequencyHz);
        const int spectrumPeakBin = dominantBin(spectrum);
        const bool fundamentalAtStimulusBin = std::abs(spectrumPeakBin - fundamentalBin) <= 2;
        const double fundamentalEnergy = energyAroundBin(spectrum, fundamentalBin, 1);

        double harmonicEnergy = 0.0;
        double evenEnergy = 0.0;
        double oddEnergy = 0.0;
        double h2Energy = 0.0;
        double h3Energy = 0.0;
        for (int harmonic = 2; harmonic <= harmonicMax; ++harmonic) {
            const double harmonicHz = frequencyHz * static_cast<double>(harmonic);
            if (harmonicHz >= (caseSpec.sampleRate * 0.5 * 0.999)) {
                break;
            }

            const int harmonicBin = hzToBin(spectrum, caseSpec.sampleRate, harmonicHz);
            const double energy = energyAroundBin(spectrum, harmonicBin, 1);
            harmonicEnergy += energy;
            if (harmonic % 2 == 0) {
                evenEnergy += energy;
            } else {
                oddEnergy += energy;
            }

            if (harmonic == 2) {
                h2Energy = energy;
            } else if (harmonic == 3) {
                h3Energy = energy;
            }
        }

        SaturationPoint point;
        point.inputDbfs = levels[index];
        point.fundamentalBin = fundamentalBin;
        point.spectrumPeakBin = spectrumPeakBin;
        point.fundamentalAtStimulusBin = fundamentalAtStimulusBin;
        point.outputRmsDbfs = linearToDb(computeRms(analysisBuffer));
        point.outputPeakDbfs = linearToDb(computePeak(analysisBuffer));
        point.thdDb = 10.0 * std::log10((harmonicEnergy + 1e-30) / (fundamentalEnergy + 1e-30));
        point.evenOddBalanceDb = 10.0 * std::log10((evenEnergy + 1e-30) / (oddEnergy + 1e-30));
        point.h2Db = 10.0 * std::log10((h2Energy + 1e-30) / (fundamentalEnergy + 1e-30));
        point.h3Db = 10.0 * std::log10((h3Energy + 1e-30) / (fundamentalEnergy + 1e-30));
        points.push_back(point);
    }

    if (points.empty()) {
        throw std::runtime_error("No saturation points generated");
    }

    const auto artifactDir = juce::File(caseSpec.artifactsDir);
    const auto jsonPath = artifactDir.getChildFile("saturation_fingerprint.json");
    const auto csvPath = artifactDir.getChildFile("saturation_fingerprint.csv");
    const auto markdownPath = artifactDir.getChildFile("saturation_fingerprint.md");

    nlohmann::json report;
    report["method"] = "saturation_fingerprint_v2";
    report["frequencyHz"] = frequencyHz;
    report["durationSec"] = durationSec;
    report["segmentSec"] = segmentSec;
    report["steadySkipSec"] = steadySkipSec;
    report["steadyAnalysisSec"] = steadyAnalysisSec;
    report["harmonicMax"] = harmonicMax;
    report["levelsDbfs"] = levels;

    double worstThdDb = -std::numeric_limits<double>::infinity();
    double sumThdDb = 0.0;
    double sumAbsOddEven = 0.0;
    double maxOutputPeakDbfs = -std::numeric_limits<double>::infinity();
    int fundamentalMismatchCount = 0;

    nlohmann::json pointsJson = nlohmann::json::array();
    std::ostringstream csv;
    csv << "input_dbfs,output_rms_dbfs,output_peak_dbfs,thd_db,even_odd_balance_db,h2_db,h3_db\n";

    std::ostringstream markdown;
    markdown << "# Saturation Fingerprint\n\n";
    markdown << "| Input (dBFS) | Output RMS (dBFS) | Peak (dBFS) | THD (dB) | Even/Odd (dB) | H2/Fund (dB) | H3/Fund (dB) |\n";
    markdown << "|---:|---:|---:|---:|---:|---:|---:|\n";

    for (const auto& point : points) {
        worstThdDb = std::max(worstThdDb, point.thdDb);
        sumThdDb += point.thdDb;
        sumAbsOddEven += std::abs(point.evenOddBalanceDb);
        maxOutputPeakDbfs = std::max(maxOutputPeakDbfs, point.outputPeakDbfs);
        if (!point.fundamentalAtStimulusBin) {
            ++fundamentalMismatchCount;
        }

        nlohmann::json pointJson;
        pointJson["inputDbfs"] = point.inputDbfs;
        pointJson["outputRmsDbfs"] = point.outputRmsDbfs;
        pointJson["outputPeakDbfs"] = point.outputPeakDbfs;
        pointJson["thdDb"] = point.thdDb;
        pointJson["evenOddBalanceDb"] = point.evenOddBalanceDb;
        pointJson["h2Db"] = point.h2Db;
        pointJson["h3Db"] = point.h3Db;
        pointJson["fundamentalBin"] = point.fundamentalBin;
        pointJson["spectrumPeakBin"] = point.spectrumPeakBin;
        pointJson["fundamentalAtStimulusBin"] = point.fundamentalAtStimulusBin;
        pointsJson.push_back(pointJson);

        csv << formatDb(point.inputDbfs) << "," << formatDb(point.outputRmsDbfs) << ","
            << formatDb(point.outputPeakDbfs) << "," << formatDb(point.thdDb) << ","
            << formatDb(point.evenOddBalanceDb) << "," << formatDb(point.h2Db) << ","
            << formatDb(point.h3Db) << "\n";

        markdown << "| " << formatDb(point.inputDbfs) << " | " << formatDb(point.outputRmsDbfs)
                 << " | " << formatDb(point.outputPeakDbfs) << " | " << formatDb(point.thdDb)
                 << " | " << formatDb(point.evenOddBalanceDb) << " | " << formatDb(point.h2Db)
                 << " | " << formatDb(point.h3Db) << " |\n";
    }

    report["points"] = pointsJson;
    jsonPath.replaceWithText(report.dump(2));
    csvPath.replaceWithText(csv.str());
    markdownPath.replaceWithText(markdown.str());

    result.artifacts["saturationFingerprintJson"] = jsonPath.getFullPathName().toStdString();
    result.artifacts["saturationFingerprintCsv"] = csvPath.getFullPathName().toStdString();
    result.artifacts["saturationFingerprintMd"] = markdownPath.getFullPathName().toStdString();

    result.metrics["saturationInputPointCount"] = static_cast<double>(points.size());
    result.metrics["saturationFundamentalMismatchCount"] =
        static_cast<double>(fundamentalMismatchCount);
    result.metrics["saturationWorstThdDb"] = worstThdDb;
    result.metrics["saturationMeanThdDb"] =
        sumThdDb / static_cast<double>(points.size());
    result.metrics["saturationOddEvenImbalanceDb"] =
        sumAbsOddEven / static_cast<double>(points.size());
    result.metrics["saturationMaxOutputPeakDbfs"] = maxOutputPeakDbfs;
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

void writePresetTrimPlan(const juce::File& artifactDir, const std::vector<PresetGainRow>& rows,
                         const CaseSpec& caseSpec, CaseResult& result) {
    const auto planPath = artifactDir.getChildFile("preset_gain_trim_plan.json");
    nlohmann::json plan;
    plan["schemaVersion"] = 1;
    plan["targetParameter"] = caseSpec.extra.value("presetTrimParameter", "Master Output");
    plan["recommendedTrimClampDb"] = {-12.0, 12.0};
    plan["entries"] = nlohmann::json::array();

    for (const auto& row : rows) {
        nlohmann::json entry;
        entry["preset"] = row.variant.label;
        entry["source"] = row.variant.source;
        entry["presetPath"] = row.variant.presetPath.value_or("");
        entry["programIndex"] =
            row.variant.programIndex ? nlohmann::json(*row.variant.programIndex) : nlohmann::json();
        entry["recommendedTrimDb"] = row.recommendedTrimDb;
        entry["targetOutputLufs"] = row.targetOutputLufs;
        entry["currentOutputLufs"] = row.outputLufs;
        plan["entries"].push_back(entry);
    }

    planPath.replaceWithText(plan.dump(2));
    result.artifacts["presetGainTrimPlan"] = planPath.getFullPathName().toStdString();

    const auto scriptPath = artifactDir.getChildFile("apply_chorus80_master_output_trims.py");
    std::ostringstream script;
    script << "#!/usr/bin/env python3\n";
    script << "import argparse, json, pathlib, re\n\n";
    script << "def clamp(v, lo, hi):\n";
    script << "    return max(lo, min(hi, v))\n\n";
    script << "def main():\n";
    script << "    ap = argparse.ArgumentParser(description='Apply preset master output trims to Chorus80 PresetManager.cpp')\n";
    script << "    ap.add_argument('--plan', default='" << planPath.getFileName().toStdString() << "')\n";
    script << "    ap.add_argument('--source', required=True)\n";
    script << "    ap.add_argument('--out', default='')\n";
    script << "    args = ap.parse_args()\n\n";
    script << "    plan_path = pathlib.Path(args.plan)\n";
    script << "    if not plan_path.is_absolute():\n";
    script << "        plan_path = pathlib.Path(__file__).resolve().parent / plan_path\n";
    script << "    plan = json.loads(plan_path.read_text())\n";
    script << "    trims = {e['preset']: float(e['recommendedTrimDb']) for e in plan.get('entries', [])}\n";
    script << "    src_path = pathlib.Path(args.source)\n";
    script << "    text = src_path.read_text()\n";
    script << "    lines = text.splitlines()\n";
    script << "    preset_re = re.compile(r'^\\s*//\\s*Preset\\s+\\d+:\\s*(.+?)\\s*$')\n";
    script << "    mo_re = re.compile(r'(\\{\\s*ParamIDs::masterOutput\\s*,\\s*)([-+]?\\d+(?:\\.\\d+)?)f(\\s*\\},?)')\n";
    script << "    i = 0\n";
    script << "    changed = 0\n";
    script << "    while i < len(lines):\n";
    script << "        m = preset_re.match(lines[i])\n";
    script << "        if not m:\n";
    script << "            i += 1\n";
    script << "            continue\n";
    script << "        preset = m.group(1).strip()\n";
    script << "        if preset not in trims:\n";
    script << "            i += 1\n";
    script << "            continue\n";
    script << "        trim = trims[preset]\n";
    script << "        j = i + 1\n";
    script << "        while j < len(lines) and '{' not in lines[j]:\n";
    script << "            j += 1\n";
    script << "        if j >= len(lines):\n";
    script << "            break\n";
    script << "        k = j\n";
    script << "        while k < len(lines) and '};' not in lines[k]:\n";
    script << "            k += 1\n";
    script << "        if k >= len(lines):\n";
    script << "            break\n";
    script << "        found = False\n";
    script << "        for p in range(j, k + 1):\n";
    script << "            mm = mo_re.search(lines[p])\n";
    script << "            if mm:\n";
    script << "                current = float(mm.group(2))\n";
    script << "                new_v = clamp(current + trim, -12.0, 12.0)\n";
    script << "                lines[p] = mo_re.sub(lambda m2: f\"{m2.group(1)}{new_v:.2f}f{m2.group(3)}\", lines[p])\n";
    script << "                found = True\n";
    script << "                changed += 1\n";
    script << "                break\n";
    script << "        if not found:\n";
    script << "            new_v = clamp(trim, -12.0, 12.0)\n";
    script << "            indent = '    '\n";
    script << "            lines.insert(k, f\"{indent}{{ ParamIDs::masterOutput, {new_v:.2f}f }},\")\n";
    script << "            changed += 1\n";
    script << "            i = k + 1\n";
    script << "            continue\n";
    script << "        i = k + 1\n";
    script << "    out_path = pathlib.Path(args.out) if args.out else src_path\n";
    script << "    out_path.write_text('\\n'.join(lines) + '\\n')\n";
    script << "    print(f'Updated presets: {changed}')\n\n";
    script << "if __name__ == '__main__':\n";
    script << "    main()\n";

    scriptPath.replaceWithText(script.str());
    result.artifacts["presetGainTrimScript"] = scriptPath.getFullPathName().toStdString();
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
    writePresetTrimPlan(artifactDir, rows, caseSpec, result);
}

// Appends the initial toggle-parameter state and schedules in-stream toggles on the request.
// Returns the toggle positions (in samples) for analysis after rendering.
std::vector<int> configureBypassToggleStream(const CaseSpec& caseSpec, RenderRequest& request) {
    const auto paramName = caseSpec.extra.value("paramName", std::string("Bypass"));
    const auto valueA = caseSpec.extra.value("valueA", std::string("Off"));
    const auto valueB = caseSpec.extra.value("valueB", std::string("On"));

    const int totalSamples = request.input.getNumSamples();
    const double durationSec =
        static_cast<double>(totalSamples) / static_cast<double>(caseSpec.sampleRate);

    std::vector<double> toggleTimesSec;
    if (caseSpec.extra.contains("toggleAtSec") && caseSpec.extra["toggleAtSec"].is_array()) {
        for (const auto& item : caseSpec.extra["toggleAtSec"]) {
            if (item.is_number()) {
                toggleTimesSec.push_back(item.get<double>());
            }
        }
    }

    if (toggleTimesSec.empty()) {
        toggleTimesSec = {0.4 * durationSec, 0.7 * durationSec};
    }

    std::sort(toggleTimesSec.begin(), toggleTimesSec.end());

    request.parameterSets.push_back({paramName, valueA});

    std::vector<int> toggleSamples;
    toggleSamples.reserve(toggleTimesSec.size());

    bool toggleToB = true;
    for (const auto timeSec : toggleTimesSec) {
        const int atSample = std::clamp(
            static_cast<int>(std::round(timeSec * caseSpec.sampleRate)), 0, totalSamples - 1);
        request.scheduledParameterChanges.push_back(
            {atSample, paramName, toggleToB ? valueB : valueA});
        toggleSamples.push_back(atSample);
        toggleToB = !toggleToB;
    }

    return toggleSamples;
}

void analyzeBypassToggleStream(const CaseSpec& caseSpec, const std::vector<int>& toggleSamples,
                               const juce::AudioBuffer<float>& output, CaseResult& result) {
    const int numSamples = output.getNumSamples();
    const int windowHalf =
        std::max(1, static_cast<int>(std::round(0.05 * caseSpec.sampleRate))); // +-50 ms
    const int warmupSamples =
        std::min(numSamples / 4, static_cast<int>(std::round(0.25 * caseSpec.sampleRate)));

    auto maxStepInRange = [&output](int start, int end) {
        double maxStep = 0.0;
        for (int ch = 0; ch < output.getNumChannels(); ++ch) {
            const auto* read = output.getReadPointer(ch);
            for (int i = std::max(1, start); i < end; ++i) {
                maxStep = std::max(maxStep,
                                   std::abs(static_cast<double>(read[i]) - read[i - 1]));
            }
        }
        return maxStep;
    };

    auto insideToggleWindow = [&toggleSamples, windowHalf](int sample) {
        for (const auto toggle : toggleSamples) {
            if (sample >= toggle - windowHalf && sample <= toggle + windowHalf) {
                return true;
            }
        }
        return false;
    };

    // Baseline: worst sample-to-sample step in steady-state regions (outside all
    // toggle windows, after a short warmup).
    double baselineStep = 0.0;
    for (int ch = 0; ch < output.getNumChannels(); ++ch) {
        const auto* read = output.getReadPointer(ch);
        for (int i = std::max(1, warmupSamples); i < numSamples; ++i) {
            if (insideToggleWindow(i) || insideToggleWindow(i - 1)) {
                continue;
            }
            baselineStep =
                std::max(baselineStep, std::abs(static_cast<double>(read[i]) - read[i - 1]));
        }
    }

    double worstToggleStep = 0.0;
    nlohmann::json toggles = nlohmann::json::array();
    for (const auto toggle : toggleSamples) {
        const double toggleStep =
            maxStepInRange(toggle - windowHalf, std::min(numSamples, toggle + windowHalf + 1));
        worstToggleStep = std::max(worstToggleStep, toggleStep);

        nlohmann::json toggleJson;
        toggleJson["atSample"] = toggle;
        toggleJson["atSec"] = static_cast<double>(toggle) / caseSpec.sampleRate;
        toggleJson["maxStepDbfs"] = linearToDb(toggleStep);
        toggles.push_back(toggleJson);
    }

    const double clickAboveBaseline = std::max(0.0, worstToggleStep - baselineStep);

    result.metrics["streamToggleClickDbfs"] = linearToDb(clickAboveBaseline);
    result.metrics["streamToggleClickRawDbfs"] = linearToDb(worstToggleStep);
    result.metrics["streamToggleBaselineStepDbfs"] = linearToDb(baselineStep);
    result.metrics["streamToggleCount"] = static_cast<double>(toggleSamples.size());

    // Default gate when the threshold profile does not specify one.
    result.thresholds.try_emplace("streamToggleClickDbfsMax", -30.0);

    nlohmann::json details;
    details["method"] = "in_stream_toggle_click_v1";
    details["paramName"] = caseSpec.extra.value("paramName", std::string("Bypass"));
    details["windowHalfSec"] = 0.05;
    details["toggles"] = toggles;
    details["baselineStepDbfs"] = linearToDb(baselineStep);

    const auto artifactDir = juce::File(caseSpec.artifactsDir);
    const auto detailPath = artifactDir.getChildFile("bypass_toggle_stream.json");
    detailPath.replaceWithText(details.dump(2));
    result.artifacts["bypassToggleStream"] = detailPath.getFullPathName().toStdString();
}

void runStereoPhaseAnalysis(const CaseSpec& caseSpec, const juce::AudioBuffer<float>& output,
                            CaseResult& result) {
    if (output.getNumChannels() < 2) {
        result.status = "skipped";
        result.message = "stereoPhase requires a stereo output layout";
        return;
    }

    const int numSamples = output.getNumSamples();
    const int windowLength =
        std::max(16, static_cast<int>(std::round(0.1 * caseSpec.sampleRate))); // 100 ms
    const int hopLength =
        std::max(8, static_cast<int>(std::round(0.05 * caseSpec.sampleRate))); // 50 ms hop
    constexpr double silenceGateDbfs = -60.0;

    const auto* left = output.getReadPointer(0);
    const auto* right = output.getReadPointer(1);

    double minCorrelation = 1.0;
    double maxSideMidRatioDb = -std::numeric_limits<double>::infinity();
    int collapseWindows = 0;
    int analyzedWindows = 0;

    for (int start = 0; start + windowLength <= numSamples; start += hopLength) {
        double sumL = 0.0, sumR = 0.0, sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;
        double sumMidSq = 0.0, sumSideSq = 0.0;

        for (int i = start; i < start + windowLength; ++i) {
            const double l = left[i];
            const double r = right[i];
            sumL += l;
            sumR += r;
            sumLL += l * l;
            sumRR += r * r;
            sumLR += l * r;
            const double mid = 0.5 * (l + r);
            const double side = 0.5 * (l - r);
            sumMidSq += mid * mid;
            sumSideSq += side * side;
        }

        const auto n = static_cast<double>(windowLength);
        const double totalRms = std::sqrt((sumLL + sumRR) / (2.0 * n));
        if (linearToDb(totalRms) < silenceGateDbfs) {
            continue;
        }

        const double meanL = sumL / n;
        const double meanR = sumR / n;
        const double varL = std::max(0.0, sumLL / n - meanL * meanL);
        const double varR = std::max(0.0, sumRR / n - meanR * meanR);
        const double covariance = sumLR / n - meanL * meanR;

        constexpr double varianceEpsilon = 1.0e-18;
        double correlation = 1.0; // treat (near-)constant channels as fully correlated
        if (varL > varianceEpsilon && varR > varianceEpsilon) {
            correlation = std::clamp(covariance / std::sqrt(varL * varR), -1.0, 1.0);
        }

        const double midRms = std::sqrt(sumMidSq / n);
        const double sideRms = std::sqrt(sumSideSq / n);
        const double sideMidRatioDb =
            20.0 * std::log10((sideRms + 1.0e-12) / (midRms + 1.0e-12));

        minCorrelation = std::min(minCorrelation, correlation);
        maxSideMidRatioDb = std::max(maxSideMidRatioDb, sideMidRatioDb);
        if (sideRms > 4.0 * midRms && correlation < -0.5) {
            ++collapseWindows;
        }
        ++analyzedWindows;
    }

    if (analyzedWindows == 0) {
        minCorrelation = 1.0;
        maxSideMidRatioDb = -120.0;
        collapseWindows = 0;
    }

    result.metrics["minWindowCorrelation"] = minCorrelation;
    result.metrics["maxSideMidRatioDb"] = maxSideMidRatioDb;
    result.metrics["phaseCollapseWindows"] = static_cast<double>(collapseWindows);
    result.metrics["stereoWindowsAnalyzed"] = static_cast<double>(analyzedWindows);

    // Default gates when the threshold profile does not specify them.
    result.thresholds.try_emplace("stereoCorrelationMin", -0.8);
    result.thresholds.try_emplace("sideMidRatioDbMax", 18.0);
    result.thresholds.try_emplace("phaseCollapseWindowsMax", 0.0);
}

bool runPluginval(const std::optional<std::string>& pluginvalPath, const CaseSpec& caseSpec,
                  CaseResult& result) {
    const auto resolvedPath = pluginvalPath.value_or("pluginval");
    const juce::String executable = juce::String::fromUTF8(resolvedPath.c_str()).trim();

    auto setMissingToolResult = [&]() {
        if (caseSpec.pluginvalRequired) {
            result.status = "failed";
            result.message = "pluginval executable missing or not runnable";
        } else {
            result.status = "skipped";
            result.message = "pluginval not available; skipped optional validation";
        }
    };

    auto isCommandAvailable = [&](const juce::String& commandName) -> bool {
        if (commandName.isEmpty()) {
            return false;
        }

        const juce::String dequoted = commandName.unquoted();
        const juce::File directPath(dequoted);
        if (dequoted.containsChar('/') || dequoted.containsChar('\\') ||
            juce::File::isAbsolutePath(dequoted)) {
            return directPath.existsAsFile();
        }

        juce::ChildProcess probe;
#if JUCE_WINDOWS
        const juce::String probeCommand = "where " + commandName;
#else
        const juce::String probeCommand = "command -v " + commandName + " >/dev/null 2>&1";
#endif
        if (!probe.start(probeCommand)) {
            return false;
        }
        probe.waitForProcessToFinish(5000);
        return probe.getExitCode() == 0;
    };

    if (!isCommandAvailable(executable)) {
        setMissingToolResult();
        return false;
    }

    juce::ChildProcess process;
    const juce::String command =
        executable + " --strictness-level " +
        juce::String(caseSpec.pluginvalStrictness) + " --validate-in-process --output-dir \"" +
        juce::String(caseSpec.artifactsDir) + "\" \"" + juce::String(caseSpec.pluginPath) + "\"";

    if (!process.start(command)) {
        setMissingToolResult();
        return false;
    }

    process.waitForProcessToFinish(caseSpec.timeoutSec * 1000);
    const auto output = process.readAllProcessOutput().toStdString();

    juce::File(caseSpec.artifactsDir).createDirectory();
    const auto logFile = juce::File(caseSpec.artifactsDir).getChildFile("pluginval.log");
    logFile.replaceWithText(output);
    result.artifacts["pluginvalLog"] = logFile.getFullPathName().toStdString();

    const int exitCode = process.getExitCode();
    result.metrics["pluginvalExitCode"] = static_cast<double>(exitCode);

    if (exitCode != 0) {
        if (!caseSpec.pluginvalRequired && exitCode == 127) {
            result.status = "skipped";
            result.message = "pluginval not available; skipped optional validation";
            return false;
        }

        result.status = "failed";
        result.message = "pluginval reported failures (exit code " + std::to_string(exitCode) + ")";
        addRecommendation(result, "Inspect pluginval.log for validator output and rerun the same command directly in a shell for detailed diagnostics.");
        return false;
    }
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
    addThreshold(result.thresholds, "abxLoudnessDeltaDbMax", caseSpec.thresholds.abxLoudnessDeltaDbMax);
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
    addThreshold(result.thresholds, "saturationWorstThdDbMax",
                 caseSpec.thresholds.saturationWorstThdDbMax);
    addThreshold(result.thresholds, "saturationOddEvenImbalanceDbMax",
                 caseSpec.thresholds.saturationOddEvenImbalanceDbMax);
    // 0.0 means "disabled" for the absolute alias gate, so only propagate real values.
    if (caseSpec.thresholds.aliasWorstToneDbfsMax &&
        *caseSpec.thresholds.aliasWorstToneDbfsMax != 0.0) {
        result.thresholds["aliasWorstToneDbfsMax"] = *caseSpec.thresholds.aliasWorstToneDbfsMax;
    }
    addThreshold(result.thresholds, "streamToggleClickDbfsMax",
                 caseSpec.thresholds.streamToggleClickDbfsMax);
    addThreshold(result.thresholds, "stereoCorrelationMin", caseSpec.thresholds.stereoCorrelationMin);
    addThreshold(result.thresholds, "sideMidRatioDbMax", caseSpec.thresholds.sideMidRatioDbMax);
    addThreshold(result.thresholds, "phaseCollapseWindowsMax",
                 caseSpec.thresholds.phaseCollapseWindowsMax);
    addThreshold(result.thresholds, "minWorstBlockRealtimeFactor",
                 caseSpec.thresholds.minWorstBlockRealtimeFactor);
    addThreshold(result.thresholds, "minP95BlockRealtimeFactor",
                 caseSpec.thresholds.minP95BlockRealtimeFactor);

    if (caseSpec.thresholds.requireLayoutHonored.value_or(false)) {
        result.thresholds["requireLayoutHonored"] = 1.0;
    }

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
    const juce::File dir(caseSpec.artifactsDir);
    if (!dir.createDirectory().wasOk()) {
        juce::Thread::sleep(50);
        dir.createDirectory();
    }
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

        if (caseSpec.testType == "abx") {
            runAbxPreparation(caseSpec, request, result, seed);
            applyThresholdChecks(result);
            ensureFailureRecommendations(result);
            return result;
        }

        if (caseSpec.testType == "saturationFingerprint") {
            runSaturationFingerprint(caseSpec, request, result, seed);
            applyThresholdChecks(result);
            ensureFailureRecommendations(result);
            return result;
        }

        std::vector<int> streamToggleSamples;
        if (caseSpec.testType == "bypassToggleStream") {
            streamToggleSamples = configureBypassToggleStream(caseSpec, request);
        }

        auto render = RenderEngine::render(request);
        const auto outputPath = artifactDir.getChildFile("output.wav");
        writeWav(render.output, render.sampleRate, outputPath);

        result.artifacts["renderedAudio"] = outputPath.getFullPathName().toStdString();

        result.metrics["outputPeakDbfs"] = linearToDb(computePeak(render.output));
        result.metrics["outputRmsDbfs"] = linearToDb(computeRms(render.output));
        result.metrics["realtimeFactor"] = render.realtimeFactor;

        result.layoutHonored = render.layoutHonored;
        result.metrics["layoutHonored"] = render.layoutHonored ? 1.0 : 0.0;
        if (caseSpec.thresholds.requireLayoutHonored.value_or(false) && !render.layoutHonored) {
            result.status = "failed";
            result.message = "requested channel layout (" + channelLayoutName(caseSpec.channels) +
                             ") not honored; plugin fell back to its default bus layout";
            addRecommendation(result,
                              "Support the requested bus layout in isBusesLayoutSupported or remove that layout from the suite matrix.");
        }

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
            const auto analysisBuffer = trimWarmup(
                render.output, caseSpec.sampleRate, analysisWarmupSeconds(caseSpec.extra));
            auto spectrum = computeSpectrum(analysisBuffer, 0);
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
            const auto analysisBuffer = trimWarmup(
                render.output, caseSpec.sampleRate, analysisWarmupSeconds(caseSpec.extra));
            auto spectrum = computeSpectrum(analysisBuffer, 0);

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

            // RenderEngine already trims the plugin-reported latency from the output,
            // so a correctly reporting plugin yields a residual of ~0 here. The error
            // metric is the residual misalignment on the trimmed output (signed:
            // positive = output late, negative = output early).
            const int residual = estimateLatencySamples(request.input, render.output,
                                                        std::min(caseSpec.sampleRate, 16384));
            result.metrics["reportedLatencySamples"] = render.reportedLatencySamples;
            result.metrics["estimatedLatencySamples"] = residual;
            result.metrics["latencyResidualSamples"] = residual;
            result.metrics["latencyErrorSamples"] = std::abs(residual);

        } else if (caseSpec.testType == "automationZipper") {
            auto staticRequest = request;
            staticRequest.automateFirstParameter = false;
            auto staticRender = RenderEngine::render(staticRequest);

            auto automatedRequest = request;
            const auto rampParamName = caseSpec.extra.value("paramName", std::string{});
            const auto rampStartValue = caseSpec.extra.value("rampStartValue", std::string{});
            const auto rampEndValue = caseSpec.extra.value("rampEndValue", std::string{});

            if (!rampParamName.empty() || !rampStartValue.empty() || !rampEndValue.empty()) {
                ParameterRampSpec ramp;
                ramp.paramName = rampParamName;
                if (!rampStartValue.empty()) {
                    ramp.startValue = rampStartValue;
                }
                if (!rampEndValue.empty()) {
                    ramp.endValue = rampEndValue;
                }
                automatedRequest.parameterRamp = ramp;
            } else {
                automatedRequest.automateFirstParameter = true;
            }

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

        } else if (caseSpec.testType == "bypassToggleStream") {
            analyzeBypassToggleStream(caseSpec, streamToggleSamples, render.output, result);

        } else if (caseSpec.testType == "stereoPhase") {
            runStereoPhaseAnalysis(caseSpec, render.output, result);

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
            double worstBlockFactor = std::numeric_limits<double>::max();
            double worstP95Factor = std::numeric_limits<double>::max();
            for (int i = 0; i < repetitions; ++i) {
                auto stressRender = RenderEngine::render(request);
                sumRealtime += stressRender.realtimeFactor;
                worstBlockFactor = std::min(worstBlockFactor, stressRender.worstBlockRealtimeFactor);
                worstP95Factor = std::min(worstP95Factor, stressRender.p95BlockRealtimeFactor);
            }

            result.metrics["realtimeFactor"] = sumRealtime / repetitions;
            result.metrics["worstBlockRealtimeFactor"] = worstBlockFactor;
            result.metrics["p95BlockRealtimeFactor"] = worstP95Factor;
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
