#include "Baseline.h"

#include "Results.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace vstest {
namespace {

juce::File currentBaselineFile(const juce::File& repoRoot, const std::string& suiteName) {
    return repoRoot.getChildFile(".vst-test")
        .getChildFile("baselines")
        .getChildFile(suiteName)
        .getChildFile("current")
        .getChildFile("results.json");
}

} // namespace

std::optional<RunResult> BaselineManager::loadCurrentBaseline(const std::string& suiteName,
                                                              const juce::File& repoRoot) {
    const auto baselinePath = currentBaselineFile(repoRoot, suiteName);
    if (!baselinePath.existsAsFile()) {
        return std::nullopt;
    }

    const auto parsed = nlohmann::json::parse(baselinePath.loadFileAsString().toStdString());
    return runResultFromJson(parsed);
}

void BaselineManager::compareAgainstBaseline(CaseResult& caseResult,
                                             const std::optional<RunResult>& baseline,
                                             double metricTolerance, bool strictMissing) {
    if (!baseline) {
        caseResult.baselineCompared = false;
        caseResult.baselinePassed = !strictMissing;

        if (strictMissing) {
            caseResult.status = "failed";
            if (!caseResult.message.empty()) {
                caseResult.message += " | ";
            }
            caseResult.message += "missing baseline in strict mode";
        }

        return;
    }

    auto baselineCase = std::find_if(baseline->cases.begin(), baseline->cases.end(),
                                     [&caseResult](const CaseResult& item) {
                                         return item.id == caseResult.id;
                                     });

    caseResult.baselineCompared = true;

    if (baselineCase == baseline->cases.end()) {
        caseResult.baselinePassed = !strictMissing;
        if (strictMissing) {
            caseResult.status = "failed";
            if (!caseResult.message.empty()) {
                caseResult.message += " | ";
            }
            caseResult.message += "missing baseline case in strict mode";
        }
        return;
    }

    bool passed = true;

    for (const auto& [metricName, metricValue] : caseResult.metrics) {
        auto it = baselineCase->metrics.find(metricName);
        if (it == baselineCase->metrics.end()) {
            continue;
        }

        const double delta = std::abs(metricValue - it->second);
        if (delta > metricTolerance) {
            passed = false;
            if (!caseResult.message.empty()) {
                caseResult.message += " | ";
            }
            caseResult.message += "baseline drift in " + metricName + ": " +
                                  std::to_string(delta);
        }
    }

    caseResult.baselinePassed = passed;
    if (!passed) {
        caseResult.status = "failed";
    }
}

void BaselineManager::approveBaseline(const std::string& suiteName, const std::string& runId,
                                      const juce::File& runsRoot, const juce::File& repoRoot,
                                      const std::string& notes) {
    const auto runDir = runsRoot.getChildFile(runId);
    const auto runResults = runDir.getChildFile("results.json");

    if (!runResults.existsAsFile()) {
        throw std::runtime_error("Run results not found: " +
                                 runResults.getFullPathName().toStdString());
    }

    const auto baselineRoot =
        repoRoot.getChildFile(".vst-test").getChildFile("baselines").getChildFile(suiteName);
    const auto approvedDir = baselineRoot.getChildFile("approved").getChildFile(runId);
    const auto currentDir = baselineRoot.getChildFile("current");

    approvedDir.createDirectory();
    currentDir.createDirectory();

    const auto approvedResults = approvedDir.getChildFile("results.json");
    runResults.copyFileTo(approvedResults);

    const auto currentResults = currentDir.getChildFile("results.json");
    runResults.copyFileTo(currentResults);

    nlohmann::json manifest;
    manifest["schemaVersion"] = 1;
    manifest["suite"] = suiteName;
    manifest["approvedRunId"] = runId;
    manifest["notes"] = notes;
    manifest["approvedAt"] = juce::Time::getCurrentTime().toString(true, true).toStdString();

    const auto manifestFile = baselineRoot.getChildFile("manifest.json");
    manifestFile.replaceWithText(manifest.dump(2));
}

} // namespace vstest
