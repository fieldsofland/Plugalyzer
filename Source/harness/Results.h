#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace vstest {

enum class CaseStatus {
    passed,
    failed,
    crashed,
    skipped,
    error,
};

std::string caseStatusToString(CaseStatus status);
CaseStatus caseStatusFromString(const std::string& value);

struct CaseResult {
    std::string id;
    std::string testType;
    std::string status = "failed";
    std::string message;
    std::string pluginId;
    int sampleRate = 0;
    int blockSize = 0;
    int channels = 0;
    std::map<std::string, double> metrics;
    std::map<std::string, double> thresholds;
    std::map<std::string, std::string> artifacts;
    std::vector<std::string> recommendations;
    bool baselineCompared = false;
    bool baselinePassed = true;
};

struct RunSummary {
    int total = 0;
    int passed = 0;
    int failed = 0;
    int crashed = 0;
    int skipped = 0;
};

struct RunResult {
    int schemaVersion = 1;
    std::string runId;
    std::string suite;
    std::string toolVersion;
    std::string os;
    std::string arch;
    RunSummary summary;
    std::vector<CaseResult> cases;
};

nlohmann::json toJson(const CaseResult& result);
CaseResult caseResultFromJson(const nlohmann::json& j);

nlohmann::json toJson(const RunResult& result);
RunResult runResultFromJson(const nlohmann::json& j);

} // namespace vstest
