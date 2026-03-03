#include "ValidateCommand.h"

#include "ExitCodes.h"
#include "Utils.h"
#include <nlohmann/json.hpp>

namespace {

bool runPluginval(const juce::String& pluginPath, int strictness,
                  const std::optional<std::string>& pluginvalPath, std::string& output) {
    const auto executable = pluginvalPath.value_or("pluginval");

    juce::ChildProcess process;
    const juce::String command =
        juce::String::fromUTF8(executable.c_str()) + " --strictness-level " + juce::String(strictness) +
        " --validate-in-process \"" + pluginPath + "\"";

    if (!process.start(command)) {
        output = "pluginval executable missing or not runnable";
        return false;
    }

    process.waitForProcessToFinish(15 * 60 * 1000);
    output = process.readAllProcessOutput().toStdString();

    return process.getExitCode() == 0;
}

} // namespace

std::shared_ptr<CLI::App> ValidateCommand::createApp() {
    auto app =
        std::make_shared<CLI::App>("Validate plugin lifecycle and compatibility checks", "validate");

    app->add_option("-p,--plugin", pluginPath, "Plugin path")->required()->check(CLI::ExistingPath);
    app->add_option("--sample-rate", sampleRates,
                    "Sample rates to validate (can be repeated, defaults to 44.1/48/96k)");
    app->add_option("--block-size", blockSizes,
                    "Block sizes to validate (can be repeated, defaults to 64/256/1024)");
    app->add_option("--strictness", strictness, "pluginval strictness level (1-10)");
    app->add_option("--pluginval-path", pluginvalPath,
                    "Path to pluginval executable. Defaults to pluginval in PATH.");
    app->add_flag("--require-pluginval", requirePluginval,
                  "Fail if pluginval cannot be launched.");
    app->add_flag("--json", jsonOutput, "Print machine-readable JSON output");

    return app;
}

void ValidateCommand::execute() {
    nlohmann::json report;
    report["plugin"] = pluginPath.toStdString();
    report["nativeChecks"] = nlohmann::json::array();

    bool passed = true;

    for (const auto sampleRate : sampleRates) {
        for (const auto blockSize : blockSizes) {
            nlohmann::json check;
            check["sampleRate"] = sampleRate;
            check["blockSize"] = blockSize;

            try {
                auto plugin =
                    PluginUtils::createPluginInstance(pluginPath, sampleRate, static_cast<int>(blockSize));
                plugin->prepareToPlay(sampleRate, blockSize);

                juce::MemoryBlock state;
                plugin->getStateInformation(state);

                auto plugin2 =
                    PluginUtils::createPluginInstance(pluginPath, sampleRate, static_cast<int>(blockSize));
                plugin2->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

                plugin->releaseResources();
                plugin2->releaseResources();

                check["status"] = "passed";
                check["stateBytes"] = static_cast<int>(state.getSize());
            } catch (const std::exception& e) {
                check["status"] = "failed";
                check["message"] = e.what();
                passed = false;
            }

            report["nativeChecks"].push_back(check);
        }
    }

    std::string pluginvalOutput;
    nlohmann::json pluginvalReport;
    pluginvalReport["requested"] = true;
    pluginvalReport["strictness"] = strictness;

    const auto pluginvalPassed = runPluginval(pluginPath, strictness, pluginvalPath, pluginvalOutput);

    pluginvalReport["output"] = pluginvalOutput;
    pluginvalReport["passed"] = pluginvalPassed;

    if (!pluginvalPassed && requirePluginval) {
        throw CLIException("pluginval is required but failed/missing", ExitCode::MissingDependency);
    }

    if (!pluginvalPassed) {
        if (!requirePluginval &&
            pluginvalOutput.find("missing or not runnable") != std::string::npos) {
            pluginvalReport["status"] = "skipped";
        } else {
            passed = false;
            pluginvalReport["status"] = "failed";
        }
    } else {
        pluginvalReport["status"] = "passed";
    }

    report["pluginval"] = pluginvalReport;
    report["status"] = passed ? "passed" : "failed";

    if (jsonOutput) {
        std::cout << report.dump(2) << std::endl;
    } else {
        std::cout << "Validation status: " << report["status"] << std::endl;
    }

    if (!passed) {
        throw CLIException("Validation checks failed", ExitCode::TestFailure);
    }
}
