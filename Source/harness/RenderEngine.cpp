#include "RenderEngine.h"

#include "../PresetLoadingExtensionsVisitor.h"
#include "../Utils.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace vstest {
namespace {

void applyPreset(juce::AudioPluginInstance& plugin, const std::optional<std::string>& presetPath) {
    if (!presetPath) {
        return;
    }

    juce::File presetFile(*presetPath);
    if (!presetFile.existsAsFile()) {
        throw std::runtime_error("Preset file not found: " + *presetPath);
    }

    juce::MemoryBlock presetData;
    auto presetInputStream = presetFile.createInputStream();
    if (!presetInputStream) {
        throw std::runtime_error("Unable to open preset file: " + *presetPath);
    }

    presetInputStream->readIntoMemoryBlock(presetData);

    PresetLoadingExtensionsVisitor presetLoader(presetData);
    plugin.getExtensions(presetLoader);
}

float parseNormalizedValue(const std::string& value) {
    std::string trimmed = value;
    juce::String juceValue(trimmed);
    juceValue = juceValue.trim();

    if (juceValue.endsWith(":n")) {
        juceValue = juceValue.dropLastCharacters(2).trim();
    }

    const auto normalized = parseFloatStrict(juceValue.toStdString());
    if (normalized < 0.0f || normalized > 1.0f) {
        throw std::runtime_error("Normalized value out of range [0,1]: " + juceValue.toStdString());
    }

    return normalized;
}

float parameterValueFromString(juce::AudioProcessorParameter* parameter, const std::string& value) {
    juce::String juceValue(value);
    juceValue = juceValue.trim();

    if (juceValue.endsWith(":n")) {
        return parseNormalizedValue(juceValue.toStdString());
    }

    // Prefer text conversion so values like "50%", "On", "4x" work naturally.
    const auto fromText = parameter->getValueForText(juceValue);
    if (std::isfinite(fromText) && fromText >= 0.0f && fromText <= 1.0f) {
        return fromText;
    }

    // Fall back to raw numeric parsing as normalized value.
    return parseNormalizedValue(juceValue.toStdString());
}

void applyParameters(juce::AudioPluginInstance& plugin,
                     const std::vector<ParameterAssignment>& assignments) {
    for (const auto& assignment : assignments) {
        auto* parameter = PluginUtils::getPluginParameterByName(plugin, assignment.param);
        const auto normalized = parameterValueFromString(parameter, assignment.value);
        parameter->setValueNotifyingHost(normalized);
    }
}

void applyProgram(juce::AudioPluginInstance& plugin, const std::optional<int>& programIndex) {
    if (!programIndex) {
        return;
    }

    const int numPrograms = plugin.getNumPrograms();
    if (numPrograms <= 0) {
        throw std::runtime_error("Plugin does not expose programs for program-index selection");
    }

    if (*programIndex < 0 || *programIndex >= numPrograms) {
        throw std::runtime_error("Program index out of range: " + std::to_string(*programIndex) +
                                 " (numPrograms=" + std::to_string(numPrograms) + ")");
    }

    plugin.setCurrentProgram(*programIndex);
}

juce::AudioPluginInstance::BusesLayout createLayout(const juce::AudioPluginInstance& plugin,
                                                    int channels, int& inputChannelsOut,
                                                    int& outputChannelsOut) {
    juce::AudioPluginInstance::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::canonicalChannelSet(channels));
    layout.outputBuses.add(juce::AudioChannelSet::canonicalChannelSet(channels));

    if (plugin.checkBusesLayoutSupported(layout)) {
        inputChannelsOut = channels;
        outputChannelsOut = channels;
        return layout;
    }

    auto fallbackLayout = plugin.getBusesLayout();
    inputChannelsOut = std::max(1, fallbackLayout.getMainInputChannels());
    outputChannelsOut = std::max(1, fallbackLayout.getMainOutputChannels());
    return fallbackLayout;
}

RenderResult renderWithPlugin(juce::AudioPluginInstance& plugin, const RenderRequest& request) {
    int inputChannels = std::max(1, request.channels);
    int outputChannels = std::max(1, request.channels);

    auto layout = createLayout(plugin, request.channels, inputChannels, outputChannels);
    if (!plugin.setBusesLayout(layout)) {
        throw std::runtime_error("Plugin does not support requested bus layout");
    }

    plugin.prepareToPlay(request.sampleRate, request.blockSize);
    const auto latency = plugin.getLatencySamples();

    const int inputSamples = request.input.getNumSamples();
    const int totalSamplesToProcess = inputSamples + std::max(0, latency);

    juce::AudioBuffer<float> output(outputChannels, inputSamples);
    output.clear();

    juce::AudioBuffer<float> processingBuffer(std::max(inputChannels, outputChannels),
                                              request.blockSize);
    juce::MidiBuffer midi;

    auto startTime = std::chrono::steady_clock::now();

    auto* firstParam = plugin.getParameters().isEmpty() ? nullptr : plugin.getParameters()[0];

    for (int sampleIndex = 0; sampleIndex < totalSamplesToProcess; sampleIndex += request.blockSize) {
        processingBuffer.clear();

        const int blockLength = std::min(request.blockSize, totalSamplesToProcess - sampleIndex);

        for (int channel = 0; channel < inputChannels; ++channel) {
            for (int i = 0; i < blockLength; ++i) {
                const int sourceSample = sampleIndex + i;
                const int sourceChannel = std::min(channel, request.input.getNumChannels() - 1);
                float value = 0.0f;
                if (sourceSample < inputSamples) {
                    value = request.input.getSample(sourceChannel, sourceSample);
                }

                processingBuffer.setSample(channel, i, value);
            }
        }

        if (request.automateFirstParameter && firstParam != nullptr) {
            const double progress =
                static_cast<double>(sampleIndex) / static_cast<double>(std::max(1, totalSamplesToProcess - 1));
            firstParam->setValueNotifyingHost(static_cast<float>(progress));
        }

        midi.clear();
        plugin.processBlock(processingBuffer, midi);

        for (int i = 0; i < blockLength; ++i) {
            const int outputSample = sampleIndex + i - latency;
            if (outputSample < 0 || outputSample >= output.getNumSamples()) {
                continue;
            }

            for (int channel = 0; channel < outputChannels; ++channel) {
                output.setSample(channel, outputSample, processingBuffer.getSample(channel, i));
            }
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    const double processingSec =
        std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime).count();
    const double audioSec = static_cast<double>(inputSamples) / static_cast<double>(request.sampleRate);

    RenderResult result;
    result.output = std::move(output);
    result.sampleRate = request.sampleRate;
    result.blockSize = request.blockSize;
    result.outputChannels = outputChannels;
    result.reportedLatencySamples = latency;
    result.processingSeconds = processingSec;
    result.realtimeFactor = processingSec <= 0.0 ? 0.0 : audioSec / processingSec;

    plugin.releaseResources();

    return result;
}

std::unique_ptr<juce::AudioPluginInstance> createConfiguredPlugin(const RenderRequest& request) {
    auto plugin = PluginUtils::createPluginInstance(request.pluginPath, request.sampleRate, request.blockSize);

    if (request.stateToLoad) {
        plugin->setStateInformation(request.stateToLoad->getData(),
                                    static_cast<int>(request.stateToLoad->getSize()));
    } else {
        applyProgram(*plugin, request.programIndex);
        applyPreset(*plugin, request.presetPath);
        applyParameters(*plugin, request.parameterSets);
    }

    return plugin;
}

} // namespace

RenderResult RenderEngine::render(const RenderRequest& request) {
    if (request.input.getNumChannels() <= 0 || request.input.getNumSamples() <= 0) {
        throw std::runtime_error("Render request input buffer must not be empty");
    }

    auto plugin = createConfiguredPlugin(request);
    return renderWithPlugin(*plugin, request);
}

juce::MemoryBlock RenderEngine::captureState(const RenderRequest& request) {
    auto plugin = createConfiguredPlugin(request);
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    return state;
}

std::vector<ProgramInfo> RenderEngine::listPrograms(const std::string& pluginPath, int sampleRate,
                                                    int blockSize) {
    std::vector<ProgramInfo> programs;
    auto plugin = PluginUtils::createPluginInstance(pluginPath, sampleRate, blockSize);

    const int numPrograms = plugin->getNumPrograms();
    for (int index = 0; index < numPrograms; ++index) {
        ProgramInfo info;
        info.index = index;
        auto name = plugin->getProgramName(index).trim();
        if (name.isEmpty()) {
            name = "Program " + juce::String(index);
        }
        info.name = name.toStdString();
        programs.push_back(info);
    }

    return programs;
}

} // namespace vstest
