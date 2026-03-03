#pragma once

#include "CLICommand.h"
#include <juce_core/juce_core.h>

class ReportCommand : public CLICommand {
  public:
    std::shared_ptr<CLI::App> createApp() override;
    void execute() override;

  private:
    juce::File resultsFile;
    juce::String formats = "json,junit,html";
};
