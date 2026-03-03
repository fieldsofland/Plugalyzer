#pragma once

#include "CLICommand.h"
#include <juce_core/juce_core.h>

class BaselineCommand : public CLICommand {
  public:
    std::shared_ptr<CLI::App> createApp() override;
    void execute() override;

  private:
    std::string action;
    std::string suite;
    std::string runId;
    juce::File runsRoot = juce::File::getCurrentWorkingDirectory().getChildFile(".vst-test/runs");
    std::string notes;
};
