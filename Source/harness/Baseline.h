#pragma once

#include "Config.h"
#include "Results.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <string>

namespace vstest {

class BaselineManager {
  public:
    static std::optional<RunResult> loadCurrentBaseline(const std::string& suiteName,
                                                        const juce::File& repoRoot);

    static void compareAgainstBaseline(CaseResult& caseResult,
                                       const std::optional<RunResult>& baseline,
                                       double metricTolerance, bool strictMissing);

    static void approveBaseline(const std::string& suiteName, const std::string& runId,
                                const juce::File& runsRoot, const juce::File& repoRoot,
                                const std::string& notes);
};

} // namespace vstest
