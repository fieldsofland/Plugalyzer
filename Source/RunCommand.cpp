#include "RunCommand.h"

#include "ExitCodes.h"
#include "Utils.h"
#include "harness/Baseline.h"
#include "harness/Config.h"
#include "harness/Reporting.h"
#include "harness/Results.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <nlohmann/json.hpp>
#include <thread>

namespace {

std::string nowRunId() {
    return juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H-%M-%S").toStdString();
}

bool filterMatches(const std::vector<std::string>& filters, const vstest::CaseSpec& caseSpec) {
    if (filters.empty()) {
        return true;
    }

    for (const auto& filter : filters) {
        if (caseSpec.id.find(filter) != std::string::npos ||
            caseSpec.testId.find(filter) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string osName() {
#if JUCE_MAC
    return "macos";
#elif JUCE_WINDOWS
    return "windows";
#elif JUCE_LINUX
    return "linux";
#else
    return "unknown";
#endif
}

std::string archName() {
#if JUCE_64BIT
    return "x64";
#else
    return "x86";
#endif
}

vstest::CaseResult makeCrashedResult(const vstest::CaseSpec& caseSpec, const std::string& message,
                                     const std::string& status) {
    vstest::CaseResult result;
    result.id = caseSpec.id;
    result.testType = caseSpec.testType;
    result.pluginId = caseSpec.pluginId;
    result.sampleRate = caseSpec.sampleRate;
    result.blockSize = caseSpec.blockSize;
    result.channels = caseSpec.channels;
    result.status = status;
    result.message = message;
    return result;
}

vstest::CaseResult runOneWorker(const vstest::CaseSpec& caseSpec, const juce::File& runRoot,
                                const std::optional<std::string>& pluginvalPath,
                                unsigned int seed) {
    const auto caseDir = runRoot.getChildFile("cases");
    caseDir.createDirectory();

    const auto caseFile = caseDir.getChildFile(caseSpec.id + ".case.json");
    const auto resultFile = caseDir.getChildFile(caseSpec.id + ".result.json");
    caseFile.replaceWithText(vstest::caseSpecToJson(caseSpec).dump(2));

    const auto executable =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();

    juce::String command = "\"" + executable + "\" worker --case-file \"" +
                           caseFile.getFullPathName() + "\" --result-file \"" +
                           resultFile.getFullPathName() + "\" --seed " + juce::String(seed);

    if (pluginvalPath) {
        command += " --pluginval-path \"" + juce::String(*pluginvalPath) + "\"";
    }

    juce::ChildProcess process;
    if (!process.start(command)) {
        return makeCrashedResult(caseSpec, "Failed to launch worker process", "crashed");
    }

    const bool finished = process.waitForProcessToFinish(caseSpec.timeoutSec * 1000);
    if (!finished) {
        process.kill();
        return makeCrashedResult(caseSpec, "Worker timed out", "crashed");
    }

    const auto workerOutput = process.readAllProcessOutput();
    const auto workerLogFile = runRoot.getChildFile("artifacts")
                                   .getChildFile(caseSpec.id)
                                   .getChildFile("worker.log");
    workerLogFile.getParentDirectory().createDirectory();
    workerLogFile.replaceWithText(workerOutput);

    if (!resultFile.existsAsFile()) {
        return makeCrashedResult(caseSpec, "Worker exited without writing result file", "crashed");
    }

    try {
        auto parsed = nlohmann::json::parse(resultFile.loadFileAsString().toStdString());
        auto result = vstest::caseResultFromJson(parsed);
        result.artifacts["workerLog"] = workerLogFile.getFullPathName().toStdString();
        if (process.getExitCode() == ExitCode::CrashOrTimeout) {
            result.status = "crashed";
            if (result.message.empty()) {
                result.message = "Worker process reported crash/timeout";
            }
        }
        return result;
    } catch (const std::exception& e) {
        return makeCrashedResult(caseSpec, std::string("Failed to parse worker result: ") + e.what(),
                                 "error");
    }
}

} // namespace

std::shared_ptr<CLI::App> RunCommand::createApp() {
    auto app = std::make_shared<CLI::App>("Execute a plugin test suite", "run");

    app->add_option("--suite", suiteFile, "Path to suite json config")->required()->check(CLI::ExistingFile);
    app->add_option("--select", selectFilters,
                    "Run only matching case/test identifiers (can be repeated)");
    app->add_option("--out-dir", outDir, "Output root directory for run artifacts");
    app->add_option("--jobs", jobs, "Parallel worker processes (default 1)");
    app->add_option("--pluginval-path", pluginvalPath,
                    "Path to pluginval executable (optional)");
    app->add_option("--seed", seed, "Deterministic RNG seed");
    app->add_flag("--strict-baseline", strictBaseline,
                  "Fail cases with missing baseline or drift beyond tolerance");
    app->add_option("--baseline-tolerance", baselineTolerance,
                    "Override baseline metric delta tolerance");
    app->add_flag("--json", jsonOutput, "Print machine-readable JSON output");

    return app;
}

void RunCommand::execute() {
    if (jobs <= 0) {
        throw CLIException("--jobs must be >= 1", ExitCode::CliUsageError);
    }

    const auto runId = nowRunId();
    const auto runRoot = outDir.getChildFile(runId);
    runRoot.createDirectory();

    auto suite = vstest::loadSuiteConfig(suiteFile.getFullPathName().toStdString());
    auto cases = vstest::expandCases(suite, suiteFile.getFullPathName().toStdString(),
                                     runRoot.getFullPathName().toStdString());

    std::vector<vstest::CaseSpec> selected;
    for (const auto& caseSpec : cases) {
        if (filterMatches(selectFilters, caseSpec)) {
            selected.push_back(caseSpec);
        }
    }

    if (selected.empty()) {
        throw CLIException("No cases selected by suite/filter configuration", ExitCode::CliUsageError);
    }

    const auto repoRoot = juce::File::getCurrentWorkingDirectory();
    const auto baseline = vstest::BaselineManager::loadCurrentBaseline(suite.name, repoRoot);

    const bool useStrictBaseline = strictBaseline || suite.baseline.strict;
    const double baselineMetricTolerance = baselineTolerance.value_or(suite.baseline.metricTolerance);

    std::vector<vstest::CaseResult> results;
    std::vector<std::future<vstest::CaseResult>> inflight;
    size_t nextIndex = 0;

    auto launchWorker = [&](const vstest::CaseSpec& caseSpec, size_t index) {
        return std::async(std::launch::async, [&, caseSpec, index]() {
            return runOneWorker(caseSpec, runRoot, pluginvalPath, seed + static_cast<unsigned int>(index));
        });
    };

    while (nextIndex < selected.size() || !inflight.empty()) {
        while (nextIndex < selected.size() && static_cast<int>(inflight.size()) < jobs) {
            inflight.push_back(launchWorker(selected[nextIndex], nextIndex));
            ++nextIndex;
        }

        for (size_t i = 0; i < inflight.size();) {
            auto status = inflight[i].wait_for(std::chrono::milliseconds(25));
            if (status == std::future_status::ready) {
                auto result = inflight[i].get();
                vstest::BaselineManager::compareAgainstBaseline(result, baseline, baselineMetricTolerance,
                                                                useStrictBaseline);
                results.push_back(std::move(result));
                inflight.erase(inflight.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }

        if (!inflight.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    vstest::RunResult runResult;
    runResult.runId = runId;
    runResult.suite = suite.name;
    runResult.toolVersion = JUCE_APPLICATION_VERSION_STRING;
    runResult.os = osName();
    runResult.arch = archName();
    runResult.cases = results;

    runResult.summary.total = static_cast<int>(results.size());
    for (const auto& result : results) {
        if (result.status == "passed") {
            ++runResult.summary.passed;
        } else if (result.status == "failed" || result.status == "error") {
            ++runResult.summary.failed;
        } else if (result.status == "crashed") {
            ++runResult.summary.crashed;
        } else if (result.status == "skipped") {
            ++runResult.summary.skipped;
        }
    }

    const auto jsonFile = runRoot.getChildFile("results.json");
    const auto junitFile = runRoot.getChildFile("junit.xml");
    const auto htmlFile = runRoot.getChildFile("report.html");

    vstest::Reporter::writeJson(runResult, jsonFile);
    vstest::Reporter::writeJunit(runResult, junitFile);
    vstest::Reporter::writeHtml(runResult, htmlFile);

    if (jsonOutput) {
        std::cout << vstest::toJson(runResult).dump(2) << std::endl;
    } else {
        std::cout << "Run complete: " << runRoot.getFullPathName() << std::endl;
        std::cout << "Summary: total=" << runResult.summary.total
                  << ", passed=" << runResult.summary.passed
                  << ", failed=" << runResult.summary.failed
                  << ", crashed=" << runResult.summary.crashed
                  << ", skipped=" << runResult.summary.skipped << std::endl;
    }

    if (runResult.summary.failed > 0) {
        throw CLIException("One or more test cases failed", ExitCode::TestFailure);
    }

    if (runResult.summary.crashed > 0) {
        throw CLIException("One or more test cases crashed/timed out", ExitCode::CrashOrTimeout);
    }
}
