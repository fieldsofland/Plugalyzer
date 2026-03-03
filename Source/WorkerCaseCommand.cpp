#include "WorkerCaseCommand.h"

#include "ExitCodes.h"
#include "Utils.h"
#include "harness/Analyzers.h"
#include "harness/Config.h"
#include "harness/Results.h"
#include <nlohmann/json.hpp>

std::shared_ptr<CLI::App> WorkerCaseCommand::createApp() {
    auto app = std::make_shared<CLI::App>("Run a single test case in worker isolation", "worker");

    app->add_option("--case-file", caseFile, "Path to serialized case spec json")
        ->required()
        ->check(CLI::ExistingFile);
    app->add_option("--result-file", resultFile, "Path to write case result json")->required();
    app->add_option("--pluginval-path", pluginvalPath,
                    "Path to pluginval executable (optional)");
    app->add_option("--seed", seed, "Deterministic RNG seed for generated signals");

    return app;
}

void WorkerCaseCommand::execute() {
    try {
        auto caseJson = nlohmann::json::parse(caseFile.loadFileAsString().toStdString());
        auto caseSpec = vstest::caseSpecFromJson(caseJson);

        const auto result = vstest::AnalyzerEngine::runCase(caseSpec, pluginvalPath, seed);

        resultFile.getParentDirectory().createDirectory();
        resultFile.replaceWithText(vstest::toJson(result).dump(2));

        if (result.status == "failed" || result.status == "error") {
            throw CLIException("Worker case failed", ExitCode::TestFailure);
        }

        if (result.status == "crashed") {
            throw CLIException("Worker case crashed", ExitCode::CrashOrTimeout);
        }
    } catch (const CLIException&) {
        throw;
    } catch (const std::exception& e) {
        throw CLIException(e.what(), ExitCode::InfrastructureError);
    }
}
