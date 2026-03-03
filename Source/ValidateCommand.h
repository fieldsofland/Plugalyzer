#pragma once

#include "CLICommand.h"
#include <juce_core/juce_core.h>
#include <optional>

class ValidateCommand : public CLICommand {
  public:
    std::shared_ptr<CLI::App> createApp() override;
    void execute() override;

  private:
    juce::String pluginPath;
    std::vector<int> sampleRates = {44100, 48000, 96000};
    std::vector<int> blockSizes = {64, 256, 1024};
    int strictness = 5;
    std::optional<std::string> pluginvalPath;
    bool requirePluginval = false;
    bool jsonOutput = false;
};
