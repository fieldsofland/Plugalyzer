#pragma once

#include "Config.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <vector>

namespace vstest {

struct RenderRequest {
    std::string pluginPath;
    std::optional<std::string> presetPath;
    std::vector<ParameterAssignment> parameterSets;
    std::optional<juce::MemoryBlock> stateToLoad;
    int sampleRate = 44100;
    int blockSize = 1024;
    int channels = 2;
    juce::AudioBuffer<float> input;
    bool automateFirstParameter = false;
};

struct RenderResult {
    juce::AudioBuffer<float> output;
    int sampleRate = 44100;
    int blockSize = 1024;
    int outputChannels = 2;
    int reportedLatencySamples = 0;
    double processingSeconds = 0.0;
    double realtimeFactor = 0.0;
};

class RenderEngine {
  public:
    static RenderResult render(const RenderRequest& request);
    static juce::MemoryBlock captureState(const RenderRequest& request);
};

} // namespace vstest
