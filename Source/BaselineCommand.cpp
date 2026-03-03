#include "BaselineCommand.h"

#include "ExitCodes.h"
#include "Utils.h"
#include "harness/Baseline.h"

std::shared_ptr<CLI::App> BaselineCommand::createApp() {
    auto app = std::make_shared<CLI::App>("Manage baseline artifacts", "baseline");

    auto* approve = app->add_subcommand("approve", "Approve a run as the active baseline");
    approve->add_option("--suite", suite, "Suite name")->required();
    approve->add_option("--run-id", runId, "Run identifier to approve")->required();
    approve->add_option("--runs-root", runsRoot, "Root directory containing run folders");
    approve->add_option("--notes", notes, "Optional approval notes");
    approve->callback([this]() { action = "approve"; });

    app->require_subcommand(1);
    return app;
}

void BaselineCommand::execute() {
    if (action != "approve") {
        throw CLIException("Unsupported baseline action", ExitCode::CliUsageError);
    }

    const auto repoRoot = juce::File::getCurrentWorkingDirectory();
    vstest::BaselineManager::approveBaseline(suite, runId, runsRoot, repoRoot, notes);

    std::cout << "Approved baseline for suite '" << suite << "' from run '" << runId << "'."
              << std::endl;
}
