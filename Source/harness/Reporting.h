#pragma once

#include "Results.h"
#include <juce_core/juce_core.h>
#include <string>

namespace vstest {

class Reporter {
  public:
    static void writeJson(const RunResult& result, const juce::File& outputFile);
    static void writeJunit(const RunResult& result, const juce::File& outputFile);
    static void writeHtml(const RunResult& result, const juce::File& outputFile);
};

} // namespace vstest
