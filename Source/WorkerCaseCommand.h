#pragma once

#include "CLICommand.h"
#include <juce_core/juce_core.h>
#include <optional>

class WorkerCaseCommand : public CLICommand {
  public:
    std::shared_ptr<CLI::App> createApp() override;
    void execute() override;

  private:
    juce::File caseFile;
    juce::File resultFile;
    std::optional<std::string> pluginvalPath;
    unsigned int seed = 1337;
};
