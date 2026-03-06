#pragma once

#include "CLICommand.h"
#include <juce_core/juce_core.h>

class UiServerCommand : public CLICommand {
  public:
    std::shared_ptr<CLI::App> createApp() override;
    void execute() override;

  private:
    int port = 47555;
    std::string sessionId = "default";
    std::vector<std::string> roots;
};
