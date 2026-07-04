#include "Results.h"

#include <stdexcept>

namespace vstest {

std::string caseStatusToString(CaseStatus status) {
    switch (status) {
    case CaseStatus::passed:
        return "passed";
    case CaseStatus::failed:
        return "failed";
    case CaseStatus::crashed:
        return "crashed";
    case CaseStatus::skipped:
        return "skipped";
    case CaseStatus::error:
        return "error";
    }

    return "error";
}

CaseStatus caseStatusFromString(const std::string& value) {
    if (value == "passed") {
        return CaseStatus::passed;
    }

    if (value == "failed") {
        return CaseStatus::failed;
    }

    if (value == "crashed") {
        return CaseStatus::crashed;
    }

    if (value == "skipped") {
        return CaseStatus::skipped;
    }

    if (value == "error") {
        return CaseStatus::error;
    }

    throw std::runtime_error("Unknown case status: " + value);
}

nlohmann::json toJson(const CaseResult& result) {
    nlohmann::json j;
    j["id"] = result.id;
    j["testType"] = result.testType;
    j["status"] = result.status;
    j["message"] = result.message;
    j["pluginId"] = result.pluginId;
    j["sampleRate"] = result.sampleRate;
    j["blockSize"] = result.blockSize;
    j["channels"] = result.channels;
    j["metrics"] = result.metrics;
    j["thresholds"] = result.thresholds;
    j["artifacts"] = result.artifacts;
    j["recommendations"] = result.recommendations;
    j["baselineCompared"] = result.baselineCompared;
    j["baselinePassed"] = result.baselinePassed;
    j["layoutHonored"] = result.layoutHonored;

    return j;
}

CaseResult caseResultFromJson(const nlohmann::json& j) {
    CaseResult result;
    result.id = j.value("id", "");
    result.testType = j.value("testType", "");
    result.status = j.value("status", "failed");
    result.message = j.value("message", "");
    result.pluginId = j.value("pluginId", "");
    result.sampleRate = j.value("sampleRate", 0);
    result.blockSize = j.value("blockSize", 0);
    result.channels = j.value("channels", 0);

    if (j.contains("metrics") && j["metrics"].is_object()) {
        result.metrics = j["metrics"].get<std::map<std::string, double>>();
    }

    if (j.contains("thresholds") && j["thresholds"].is_object()) {
        result.thresholds = j["thresholds"].get<std::map<std::string, double>>();
    }

    if (j.contains("artifacts") && j["artifacts"].is_object()) {
        result.artifacts = j["artifacts"].get<std::map<std::string, std::string>>();
    }

    if (j.contains("recommendations") && j["recommendations"].is_array()) {
        result.recommendations = j["recommendations"].get<std::vector<std::string>>();
    }

    result.baselineCompared = j.value("baselineCompared", false);
    result.baselinePassed = j.value("baselinePassed", true);
    result.layoutHonored = j.value("layoutHonored", true);

    return result;
}

nlohmann::json toJson(const RunResult& result) {
    nlohmann::json j;
    j["schemaVersion"] = result.schemaVersion;
    j["runId"] = result.runId;
    j["suite"] = result.suite;

    nlohmann::json env;
    env["os"] = result.os;
    env["arch"] = result.arch;
    env["toolVersion"] = result.toolVersion;
    j["environment"] = env;

    nlohmann::json summary;
    summary["total"] = result.summary.total;
    summary["passed"] = result.summary.passed;
    summary["failed"] = result.summary.failed;
    summary["crashed"] = result.summary.crashed;
    summary["skipped"] = result.summary.skipped;
    j["summary"] = summary;

    nlohmann::json cases = nlohmann::json::array();
    for (const auto& caseResult : result.cases) {
        cases.push_back(toJson(caseResult));
    }
    j["cases"] = cases;

    return j;
}

RunResult runResultFromJson(const nlohmann::json& j) {
    RunResult result;
    result.schemaVersion = j.value("schemaVersion", 1);
    result.runId = j.value("runId", "");
    result.suite = j.value("suite", "");

    if (j.contains("environment") && j["environment"].is_object()) {
        const auto& env = j["environment"];
        result.os = env.value("os", "");
        result.arch = env.value("arch", "");
        result.toolVersion = env.value("toolVersion", "");
    }

    if (j.contains("summary") && j["summary"].is_object()) {
        const auto& summary = j["summary"];
        result.summary.total = summary.value("total", 0);
        result.summary.passed = summary.value("passed", 0);
        result.summary.failed = summary.value("failed", 0);
        result.summary.crashed = summary.value("crashed", 0);
        result.summary.skipped = summary.value("skipped", 0);
    }

    if (j.contains("cases") && j["cases"].is_array()) {
        for (const auto& item : j["cases"]) {
            result.cases.push_back(caseResultFromJson(item));
        }
    }

    return result;
}

} // namespace vstest
