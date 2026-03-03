#pragma once

#include "Config.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace vstest {

class SignalGenerator {
  public:
    static juce::AudioBuffer<float> generate(const SignalDefinition& def, int sampleRate,
                                             int channels, unsigned int seed);

    static juce::AudioBuffer<float> generateImdDualTone(double durationSec, int sampleRate,
                                                        int channels, double levelDbfs,
                                                        double lowHz = 60.0,
                                                        double highHz = 7000.0);
};

} // namespace vstest
