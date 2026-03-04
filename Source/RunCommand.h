#pragma once

#include "CLICommand.h"
#include <juce_core/juce_core.h>
#include <optional>

class RunCommand : public CLICommand {
  public:
    std::shared_ptr<CLI::App> createApp() override;
    void execute() override;

  private:
    juce::File suiteFile;
    std::vector<std::string> selectFilters;
    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile(".vst-test/runs");
    int jobs = 1;
    bool jsonOutput = false;
    bool jsonSummaryOutput = false;
    int maxSummaryCases = 20;
    std::optional<std::string> pluginvalPath;
    unsigned int seed = 1337;
    bool strictBaseline = false;
    std::optional<double> baselineTolerance;
};
