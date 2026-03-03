#pragma once

#include "CLICommand.h"
#include <juce_core/juce_core.h>
#include <optional>

class ScanCommand : public CLICommand {
  public:
    std::shared_ptr<CLI::App> createApp() override;
    void execute() override;

  private:
    std::vector<juce::File> searchPaths;
    std::optional<std::string> formatFilter;
    bool jsonOutput = false;
};
