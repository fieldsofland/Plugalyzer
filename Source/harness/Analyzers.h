#pragma once

#include "Config.h"
#include "Results.h"
#include <optional>
#include <string>

namespace vstest {

class AnalyzerEngine {
  public:
    static CaseResult runCase(const CaseSpec& caseSpec,
                              const std::optional<std::string>& pluginvalPath,
                              unsigned int seed);
};

} // namespace vstest
