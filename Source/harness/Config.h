#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace vstest {

struct ParameterAssignment {
    std::string param;
    std::string value;
};

struct AdapterIsolation {
    std::string module;
    std::vector<ParameterAssignment> set;
};

struct AdapterModeVariant {
    std::string id;
    std::string module;
    std::vector<ParameterAssignment> set;
};

struct AdapterConfig {
    int schemaVersion = 1;
    std::string pluginId;
    std::vector<std::string> moduleOrder;
    std::vector<ParameterAssignment> globalInit;
    std::vector<AdapterIsolation> moduleIsolation;
    std::vector<AdapterModeVariant> modeVariants;
};

struct PluginDefinition {
    std::string id;
    std::string path;
    std::string format;
    std::optional<std::string> adapter;
    std::optional<std::string> defaultPreset;
};

struct MatrixDefinition {
    std::vector<int> sampleRates = {44100};
    std::vector<int> blockSizes = {1024};
    std::vector<std::string> channels = {"stereo"};
};

struct SignalDefinition {
    std::string id;
    std::string type;
    double frequencyHz = 1000.0;
    double levelDbfs = -18.0;
    double durationSec = 5.0;
    double startHz = 20.0;
    double endHz = 20000.0;
    int toneCount = 10;       // multitone only
    double intervalSec = 1.5; // pluck only
};

struct ThresholdProfile {
    std::optional<double> aliasingRatioDbMax;
    std::optional<double> abxLoudnessDeltaDbMax;
    std::optional<double> eqMaxErrorDb;
    std::optional<double> eqRmsErrorDb;
    std::optional<double> noiseFloorDbfsMax;
    std::optional<double> latencyErrorSamplesMax;
    std::optional<double> determinismResidualDbfsMax;
    std::optional<double> thdnDbMax;
    std::optional<double> imdDbMax;
    std::optional<double> phaseDeviationDegMax;
    std::optional<double> bypassClickPeakDbfsMax;
    std::optional<double> zipperArtifactDbMax;
    std::optional<double> minRealtimeFactor;
    std::optional<double> maxMemoryDriftMb;
    std::optional<double> baselineMetricDeltaMax;
    std::optional<double> presetGainSpreadDbMax;
    std::optional<double> saturationWorstThdDbMax;
    std::optional<double> saturationOddEvenImbalanceDbMax;
    std::optional<double> streamToggleClickDbfsMax;
    std::optional<double> stereoCorrelationMin;
    std::optional<double> sideMidRatioDbMax;
    std::optional<double> phaseCollapseWindowsMax;
    std::optional<double> minWorstBlockRealtimeFactor;
    std::optional<double> minP95BlockRealtimeFactor;
    std::optional<bool> requireLayoutHonored;
};

struct TestDefinition {
    std::string id;
    std::string type;
    std::optional<std::string> plugin;
    std::optional<std::string> signal;
    std::optional<std::string> thresholdProfile;
    bool includeModuleIsolation = true;
    int timeoutSec = 120;
    nlohmann::json extra;
};

struct BaselineOptions {
    bool strict = false;
    double metricTolerance = 0.5;
};

struct PluginvalOptions {
    bool required = false;
    int strictness = 5;
};

struct SuiteConfig {
    int schemaVersion = 1;
    std::string name;
    std::vector<PluginDefinition> plugins;
    MatrixDefinition matrix;
    std::vector<SignalDefinition> signals;
    std::vector<TestDefinition> tests;
    std::string defaultThresholdProfile = "default";
    std::vector<std::pair<std::string, ThresholdProfile>> thresholdProfiles;
    BaselineOptions baseline;
    PluginvalOptions pluginval;
};

struct CaseSpec {
    int schemaVersion = 1;
    std::string id;
    std::string suiteName;
    std::string testId;
    std::string testType;
    std::string pluginId;
    std::string pluginPath;
    std::string pluginFormat;
    std::optional<std::string> presetPath;
    int sampleRate = 44100;
    int blockSize = 1024;
    int channels = 2;
    SignalDefinition signal;
    ThresholdProfile thresholds;
    std::vector<ParameterAssignment> parameterSets;
    std::optional<std::string> module;
    std::optional<std::string> modeVariant;
    int timeoutSec = 120;
    bool runPluginval = false;
    bool pluginvalRequired = false;
    int pluginvalStrictness = 5;
    std::string artifactsDir;
    nlohmann::json extra;
};

SuiteConfig loadSuiteConfig(const std::string& path);
AdapterConfig loadAdapterConfig(const std::string& path);

std::vector<CaseSpec> expandCases(const SuiteConfig& suite, const std::string& suiteFilePath,
                                  const std::string& runOutDir);

nlohmann::json caseSpecToJson(const CaseSpec& caseSpec);
CaseSpec caseSpecFromJson(const nlohmann::json& j);

std::string channelLayoutName(int channels);

} // namespace vstest
