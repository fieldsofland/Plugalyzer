#pragma once

#include "Config.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <vector>

namespace vstest {

struct ParameterRampSpec {
    std::string paramName;          // empty => first plugin parameter
    std::string startValue = "0:n"; // text-or-normalized semantics like CLI --param
    std::string endValue = "1:n";
};

struct ScheduledParameterChange {
    int atSample = 0; // absolute sample offset in the render timeline (applied at block boundary)
    std::string paramName;
    std::string value; // text-or-normalized semantics like CLI --param
};

struct RenderRequest {
    std::string pluginPath;
    std::optional<std::string> presetPath;
    std::optional<int> programIndex;
    std::vector<ParameterAssignment> parameterSets;
    std::optional<juce::MemoryBlock> stateToLoad;
    int sampleRate = 44100;
    int blockSize = 1024;
    int channels = 2;
    juce::AudioBuffer<float> input;
    bool automateFirstParameter = false;
    std::optional<ParameterRampSpec> parameterRamp; // takes precedence over automateFirstParameter
    std::vector<ScheduledParameterChange> scheduledParameterChanges;
};

struct RenderResult {
    juce::AudioBuffer<float> output;
    int sampleRate = 44100;
    int blockSize = 1024;
    int outputChannels = 2;
    int reportedLatencySamples = 0;
    double processingSeconds = 0.0;
    double realtimeFactor = 0.0;
    double worstBlockRealtimeFactor = 0.0;
    double p95BlockRealtimeFactor = 0.0;
    bool layoutHonored = true;
};

struct ProgramInfo {
    int index = 0;
    std::string name;
};

class RenderEngine {
  public:
    static RenderResult render(const RenderRequest& request);
    static juce::MemoryBlock captureState(const RenderRequest& request);
    static std::vector<ProgramInfo> listPrograms(const std::string& pluginPath, int sampleRate,
                                                 int blockSize);
};

} // namespace vstest
