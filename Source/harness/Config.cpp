#include "Config.h"

#include "../Utils.h"

#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <stdexcept>

namespace vstest {
namespace {

template <typename T>
T getRequired(const nlohmann::json& j, const std::string& key) {
    if (!j.contains(key)) {
        throw std::runtime_error("Missing required key: " + key);
    }

    return j[key].get<T>();
}

std::vector<ParameterAssignment> parseParameterAssignments(const nlohmann::json& j) {
    std::vector<ParameterAssignment> assignments;
    if (!j.is_array()) {
        throw std::runtime_error("Parameter assignment set must be an array");
    }

    for (const auto& entry : j) {
        ParameterAssignment item;
        item.param = getRequired<std::string>(entry, "param");
        item.value = getRequired<std::string>(entry, "value");
        assignments.push_back(item);
    }

    return assignments;
}

ThresholdProfile parseThresholdProfile(const nlohmann::json& j) {
    ThresholdProfile p;

    auto setOptional = [&j](const char* key, std::optional<double>& target) {
        if (j.contains(key)) {
            target = j[key].get<double>();
        }
    };

    setOptional("aliasingRatioDbMax", p.aliasingRatioDbMax);
    setOptional("abxLoudnessDeltaDbMax", p.abxLoudnessDeltaDbMax);
    setOptional("eqMaxErrorDb", p.eqMaxErrorDb);
    setOptional("eqRmsErrorDb", p.eqRmsErrorDb);
    setOptional("noiseFloorDbfsMax", p.noiseFloorDbfsMax);
    setOptional("latencyErrorSamplesMax", p.latencyErrorSamplesMax);
    setOptional("determinismResidualDbfsMax", p.determinismResidualDbfsMax);
    setOptional("thdnDbMax", p.thdnDbMax);
    setOptional("imdDbMax", p.imdDbMax);
    setOptional("phaseDeviationDegMax", p.phaseDeviationDegMax);
    setOptional("bypassClickPeakDbfsMax", p.bypassClickPeakDbfsMax);
    setOptional("zipperArtifactDbMax", p.zipperArtifactDbMax);
    setOptional("minRealtimeFactor", p.minRealtimeFactor);
    setOptional("maxMemoryDriftMb", p.maxMemoryDriftMb);
    setOptional("baselineMetricDeltaMax", p.baselineMetricDeltaMax);
    setOptional("presetGainSpreadDbMax", p.presetGainSpreadDbMax);
    setOptional("saturationWorstThdDbMax", p.saturationWorstThdDbMax);
    setOptional("saturationOddEvenImbalanceDbMax", p.saturationOddEvenImbalanceDbMax);
    setOptional("streamToggleClickDbfsMax", p.streamToggleClickDbfsMax);
    setOptional("stereoCorrelationMin", p.stereoCorrelationMin);
    setOptional("sideMidRatioDbMax", p.sideMidRatioDbMax);
    setOptional("phaseCollapseWindowsMax", p.phaseCollapseWindowsMax);
    setOptional("minWorstBlockRealtimeFactor", p.minWorstBlockRealtimeFactor);
    setOptional("minP95BlockRealtimeFactor", p.minP95BlockRealtimeFactor);

    if (j.contains("requireLayoutHonored")) {
        p.requireLayoutHonored = j["requireLayoutHonored"].get<bool>();
    }

    return p;
}

int parseChannelCount(const std::string& value) {
    if (value == "mono") {
        return 1;
    }

    if (value == "stereo") {
        return 2;
    }

    try {
        const auto parsed = parseULongStrict(value);
        if (parsed == 0) {
            throw std::runtime_error("Channel count must be greater than zero");
        }

        return static_cast<int>(parsed);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid channel layout: " + value);
    }
}

std::string sanitizeId(std::string value) {
    for (auto& c : value) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '@' || c == '.';
        if (!safe) {
            c = '_';
        }
    }

    return value;
}

nlohmann::json thresholdToJson(const ThresholdProfile& p) {
    nlohmann::json j;

    auto setOptional = [&j](const char* key, const std::optional<double>& value) {
        if (value) {
            j[key] = *value;
        }
    };

    setOptional("aliasingRatioDbMax", p.aliasingRatioDbMax);
    setOptional("abxLoudnessDeltaDbMax", p.abxLoudnessDeltaDbMax);
    setOptional("eqMaxErrorDb", p.eqMaxErrorDb);
    setOptional("eqRmsErrorDb", p.eqRmsErrorDb);
    setOptional("noiseFloorDbfsMax", p.noiseFloorDbfsMax);
    setOptional("latencyErrorSamplesMax", p.latencyErrorSamplesMax);
    setOptional("determinismResidualDbfsMax", p.determinismResidualDbfsMax);
    setOptional("thdnDbMax", p.thdnDbMax);
    setOptional("imdDbMax", p.imdDbMax);
    setOptional("phaseDeviationDegMax", p.phaseDeviationDegMax);
    setOptional("bypassClickPeakDbfsMax", p.bypassClickPeakDbfsMax);
    setOptional("zipperArtifactDbMax", p.zipperArtifactDbMax);
    setOptional("minRealtimeFactor", p.minRealtimeFactor);
    setOptional("maxMemoryDriftMb", p.maxMemoryDriftMb);
    setOptional("baselineMetricDeltaMax", p.baselineMetricDeltaMax);
    setOptional("presetGainSpreadDbMax", p.presetGainSpreadDbMax);
    setOptional("saturationWorstThdDbMax", p.saturationWorstThdDbMax);
    setOptional("saturationOddEvenImbalanceDbMax", p.saturationOddEvenImbalanceDbMax);
    setOptional("streamToggleClickDbfsMax", p.streamToggleClickDbfsMax);
    setOptional("stereoCorrelationMin", p.stereoCorrelationMin);
    setOptional("sideMidRatioDbMax", p.sideMidRatioDbMax);
    setOptional("phaseCollapseWindowsMax", p.phaseCollapseWindowsMax);
    setOptional("minWorstBlockRealtimeFactor", p.minWorstBlockRealtimeFactor);
    setOptional("minP95BlockRealtimeFactor", p.minP95BlockRealtimeFactor);

    if (p.requireLayoutHonored) {
        j["requireLayoutHonored"] = *p.requireLayoutHonored;
    }

    return j;
}

void resolveExtraPaths(const juce::File& suiteDir, nlohmann::json& extra) {
    if (extra.contains("presetDirectory") && extra["presetDirectory"].is_string()) {
        const auto rawPresetDir = juce::String(extra["presetDirectory"].get<std::string>());
        if (!juce::File::isAbsolutePath(rawPresetDir)) {
            extra["presetDirectory"] =
                suiteDir.getChildFile(rawPresetDir).getFullPathName().toStdString();
        }
    }

    if (extra.contains("presetFiles") && extra["presetFiles"].is_array()) {
        nlohmann::json resolved = nlohmann::json::array();
        for (const auto& fileJson : extra["presetFiles"]) {
            if (!fileJson.is_string()) {
                resolved.push_back(fileJson);
                continue;
            }

            const auto rawPresetFile = juce::String(fileJson.get<std::string>());
            if (juce::File::isAbsolutePath(rawPresetFile)) {
                resolved.push_back(juce::File(rawPresetFile).getFullPathName().toStdString());
            } else {
                resolved.push_back(
                    suiteDir.getChildFile(rawPresetFile).getFullPathName().toStdString());
            }
        }
        extra["presetFiles"] = resolved;
    }
}

SignalDefinition signalFromJson(const nlohmann::json& j) {
    SignalDefinition signal;
    signal.id = getRequired<std::string>(j, "id");
    signal.type = getRequired<std::string>(j, "type");
    signal.frequencyHz = j.value("frequencyHz", signal.type == "pluck" ? 110.0 : signal.frequencyHz);
    signal.levelDbfs = j.value("levelDbfs", signal.levelDbfs);
    signal.durationSec = j.value("durationSec", signal.durationSec);
    signal.startHz = j.value("startHz", signal.startHz);
    signal.endHz = j.value("endHz", signal.endHz);
    signal.toneCount = j.value("toneCount", signal.toneCount);
    signal.intervalSec = j.value("intervalSec", signal.intervalSec);

    return signal;
}

nlohmann::json signalToJson(const SignalDefinition& signal) {
    nlohmann::json j;
    j["id"] = signal.id;
    j["type"] = signal.type;
    j["frequencyHz"] = signal.frequencyHz;
    j["levelDbfs"] = signal.levelDbfs;
    j["durationSec"] = signal.durationSec;
    j["startHz"] = signal.startHz;
    j["endHz"] = signal.endHz;
    j["toneCount"] = signal.toneCount;
    j["intervalSec"] = signal.intervalSec;
    return j;
}

nlohmann::json parameterAssignmentsToJson(const std::vector<ParameterAssignment>& assignments) {
    nlohmann::json out = nlohmann::json::array();

    for (const auto& entry : assignments) {
        nlohmann::json item;
        item["param"] = entry.param;
        item["value"] = entry.value;
        out.push_back(item);
    }

    return out;
}

std::vector<ParameterAssignment> parameterAssignmentsFromJson(const nlohmann::json& j) {
    if (!j.is_array()) {
        return {};
    }

    std::vector<ParameterAssignment> assignments;
    for (const auto& item : j) {
        ParameterAssignment assignment;
        assignment.param = getRequired<std::string>(item, "param");
        assignment.value = getRequired<std::string>(item, "value");
        assignments.push_back(assignment);
    }

    return assignments;
}

std::vector<ParameterAssignment>
appendParams(const std::vector<ParameterAssignment>& a,
             const std::vector<ParameterAssignment>& b) {
    std::vector<ParameterAssignment> merged = a;
    merged.insert(merged.end(), b.begin(), b.end());
    return merged;
}

} // namespace

SuiteConfig loadSuiteConfig(const std::string& path) {
    juce::File input(path);
    if (!input.existsAsFile()) {
        throw std::runtime_error("Suite config not found: " + path);
    }

    nlohmann::json j = nlohmann::json::parse(input.loadFileAsString().toStdString());

    SuiteConfig suite;
    suite.schemaVersion = j.value("schemaVersion", 1);
    if (suite.schemaVersion != 1) {
        throw std::runtime_error("Unsupported suite schema version: " +
                                 std::to_string(suite.schemaVersion));
    }

    suite.name = getRequired<std::string>(j, "name");

    if (!j.contains("plugins") || !j["plugins"].is_array() || j["plugins"].empty()) {
        throw std::runtime_error("Suite config must contain at least one plugin");
    }

    for (const auto& pluginJson : j["plugins"]) {
        PluginDefinition plugin;
        plugin.id = getRequired<std::string>(pluginJson, "id");
        plugin.path = getRequired<std::string>(pluginJson, "path");
        plugin.format = pluginJson.value("format", "");
        if (pluginJson.contains("adapter")) {
            plugin.adapter = pluginJson["adapter"].get<std::string>();
        }

        if (pluginJson.contains("defaultPreset")) {
            plugin.defaultPreset = pluginJson["defaultPreset"].get<std::string>();
        }

        suite.plugins.push_back(plugin);
    }

    if (j.contains("matrix")) {
        const auto& matrixJson = j["matrix"];

        if (matrixJson.contains("sampleRates")) {
            suite.matrix.sampleRates = matrixJson["sampleRates"].get<std::vector<int>>();
        }

        if (matrixJson.contains("blockSizes")) {
            suite.matrix.blockSizes = matrixJson["blockSizes"].get<std::vector<int>>();
        }

        if (matrixJson.contains("channels")) {
            suite.matrix.channels = matrixJson["channels"].get<std::vector<std::string>>();
        }
    }

    if (!j.contains("signals") || !j["signals"].is_array()) {
        throw std::runtime_error("Suite config must contain a signals array");
    }

    for (const auto& signalJson : j["signals"]) {
        suite.signals.push_back(signalFromJson(signalJson));
    }

    if (suite.signals.empty()) {
        throw std::runtime_error("Suite config must contain at least one signal");
    }

    if (!j.contains("tests") || !j["tests"].is_array() || j["tests"].empty()) {
        throw std::runtime_error("Suite config must contain at least one test");
    }

    for (const auto& testJson : j["tests"]) {
        TestDefinition test;
        test.id = getRequired<std::string>(testJson, "id");
        test.type = getRequired<std::string>(testJson, "type");

        if (testJson.contains("plugin")) {
            test.plugin = testJson["plugin"].get<std::string>();
        }

        if (testJson.contains("signal")) {
            test.signal = testJson["signal"].get<std::string>();
        }

        if (testJson.contains("thresholdProfile")) {
            test.thresholdProfile = testJson["thresholdProfile"].get<std::string>();
        }

        test.includeModuleIsolation = testJson.value("includeModuleIsolation", true);
        test.timeoutSec = testJson.value("timeoutSec", 120);
        test.extra = testJson;

        suite.tests.push_back(test);
    }

    suite.defaultThresholdProfile = "default";

    if (j.contains("thresholdProfiles") && j["thresholdProfiles"].is_object()) {
        for (const auto& [name, profileJson] : j["thresholdProfiles"].items()) {
            suite.thresholdProfiles.emplace_back(name, parseThresholdProfile(profileJson));
        }
    } else {
        suite.thresholdProfiles.emplace_back("default", ThresholdProfile{});
    }

    if (j.contains("defaultThresholdProfile")) {
        suite.defaultThresholdProfile = j["defaultThresholdProfile"].get<std::string>();
    }

    if (j.contains("baseline") && j["baseline"].is_object()) {
        const auto& baseline = j["baseline"];
        suite.baseline.strict = baseline.value("strict", false);
        suite.baseline.metricTolerance = baseline.value("metricTolerance", 0.5);
    }

    if (j.contains("pluginval") && j["pluginval"].is_object()) {
        const auto& pluginval = j["pluginval"];
        suite.pluginval.required = pluginval.value("required", false);
        suite.pluginval.strictness = pluginval.value("strictness", 5);
    }

    return suite;
}

AdapterConfig loadAdapterConfig(const std::string& path) {
    juce::File input(path);
    if (!input.existsAsFile()) {
        throw std::runtime_error("Adapter config not found: " + path);
    }

    nlohmann::json j = nlohmann::json::parse(input.loadFileAsString().toStdString());

    AdapterConfig adapter;
    adapter.schemaVersion = j.value("schemaVersion", 1);
    if (adapter.schemaVersion != 1) {
        throw std::runtime_error("Unsupported adapter schema version: " +
                                 std::to_string(adapter.schemaVersion));
    }

    adapter.pluginId = getRequired<std::string>(j, "pluginId");

    if (j.contains("moduleOrder")) {
        adapter.moduleOrder = j["moduleOrder"].get<std::vector<std::string>>();
    }

    if (j.contains("globalInit")) {
        adapter.globalInit = parseParameterAssignments(j["globalInit"]);
    }

    if (j.contains("moduleIsolation")) {
        for (const auto& item : j["moduleIsolation"]) {
            AdapterIsolation isolation;
            isolation.module = getRequired<std::string>(item, "module");
            isolation.set = parseParameterAssignments(getRequired<nlohmann::json>(item, "set"));
            adapter.moduleIsolation.push_back(isolation);
        }
    }

    if (j.contains("modeVariants")) {
        for (const auto& item : j["modeVariants"]) {
            AdapterModeVariant variant;
            variant.id = getRequired<std::string>(item, "id");
            variant.module = getRequired<std::string>(item, "module");
            variant.set = parseParameterAssignments(getRequired<nlohmann::json>(item, "set"));
            adapter.modeVariants.push_back(variant);
        }
    }

    return adapter;
}

std::vector<CaseSpec> expandCases(const SuiteConfig& suite, const std::string& suiteFilePath,
                                  const std::string& runOutDir) {
    std::vector<CaseSpec> cases;

    std::map<std::string, PluginDefinition> plugins;
    std::map<std::string, AdapterConfig> adapters;

    const juce::File suiteFile(suiteFilePath);
    const juce::File suiteDir = suiteFile.getParentDirectory();

    for (const auto& plugin : suite.plugins) {
        plugins[plugin.id] = plugin;

        if (plugin.adapter) {
            const juce::File adapterPath = suiteDir.getChildFile(*plugin.adapter);
            adapters[plugin.id] = loadAdapterConfig(adapterPath.getFullPathName().toStdString());
        }
    }

    std::map<std::string, SignalDefinition> signals;
    for (const auto& signal : suite.signals) {
        signals[signal.id] = signal;
    }

    std::map<std::string, ThresholdProfile> profiles;
    for (const auto& entry : suite.thresholdProfiles) {
        profiles[entry.first] = entry.second;
    }

    auto resolveThreshold = [&](const TestDefinition& test) {
        const auto& profileName = test.thresholdProfile.value_or(suite.defaultThresholdProfile);
        auto it = profiles.find(profileName);
        if (it == profiles.end()) {
            throw std::runtime_error("Unknown threshold profile: " + profileName);
        }
        return it->second;
    };

    for (const auto& test : suite.tests) {
        const std::string pluginId = test.plugin.value_or(suite.plugins[0].id);

        auto pluginIt = plugins.find(pluginId);
        if (pluginIt == plugins.end()) {
            throw std::runtime_error("Unknown plugin id in test '" + test.id + "': " + pluginId);
        }

        const auto signalId = test.signal.value_or(suite.signals[0].id);
        auto signalIt = signals.find(signalId);
        if (signalIt == signals.end()) {
            throw std::runtime_error("Unknown signal id in test '" + test.id + "': " + signalId);
        }

        const ThresholdProfile thresholdProfile = resolveThreshold(test);

        std::vector<AdapterIsolation> moduleIsolation = {{"", {}}};
        std::vector<AdapterModeVariant> modeVariants = {{"", "", {}}};
        std::vector<ParameterAssignment> globalInit;

        const auto adapterIt = adapters.find(pluginId);
        if (adapterIt != adapters.end()) {
            globalInit = adapterIt->second.globalInit;

            if (test.includeModuleIsolation && !adapterIt->second.moduleIsolation.empty()) {
                moduleIsolation = adapterIt->second.moduleIsolation;
            }

            if (test.includeModuleIsolation && !adapterIt->second.modeVariants.empty()) {
                modeVariants = adapterIt->second.modeVariants;
            }
        }

        std::set<std::string> moduleAllowList;
        if (test.extra.contains("moduleAllowList") && test.extra["moduleAllowList"].is_array()) {
            for (const auto& module : test.extra["moduleAllowList"]) {
                if (module.is_string()) {
                    moduleAllowList.insert(module.get<std::string>());
                }
            }
        }

        std::set<std::string> moduleDenyList;
        if (test.extra.contains("moduleDenyList") && test.extra["moduleDenyList"].is_array()) {
            for (const auto& module : test.extra["moduleDenyList"]) {
                if (module.is_string()) {
                    moduleDenyList.insert(module.get<std::string>());
                }
            }
        }

        if (!moduleAllowList.empty() || !moduleDenyList.empty()) {
            std::vector<AdapterIsolation> filteredModules;
            for (const auto& module : moduleIsolation) {
                if (module.module.empty()) {
                    filteredModules.push_back(module);
                    continue;
                }

                if (!moduleAllowList.empty() && !moduleAllowList.contains(module.module)) {
                    continue;
                }

                if (moduleDenyList.contains(module.module)) {
                    continue;
                }

                filteredModules.push_back(module);
            }

            moduleIsolation = filteredModules;
            if (moduleIsolation.empty()) {
                throw std::runtime_error("No module isolation entries remain after moduleAllowList/moduleDenyList filtering for test '" +
                                         test.id + "'");
            }
        }

        for (const auto sampleRate : suite.matrix.sampleRates) {
            for (const auto blockSize : suite.matrix.blockSizes) {
                for (const auto& channelLayout : suite.matrix.channels) {
                    const int channels = parseChannelCount(channelLayout);

                    for (const auto& module : moduleIsolation) {
                        std::vector<AdapterModeVariant> matchingModes;
                        if (module.module.empty()) {
                            matchingModes = {{"", "", {}}};
                        } else {
                            for (const auto& mode : modeVariants) {
                                if (mode.module.empty() || mode.module == module.module) {
                                    matchingModes.push_back(mode);
                                }
                            }

                            if (matchingModes.empty()) {
                                matchingModes = {{"", module.module, {}}};
                            }
                        }

                        for (const auto& modeVariant : matchingModes) {
                            CaseSpec caseSpec;
                            caseSpec.suiteName = suite.name;
                            caseSpec.testId = test.id;
                            caseSpec.testType = test.type;
                            caseSpec.pluginId = pluginId;
                            caseSpec.pluginPath = pluginIt->second.path;
                            caseSpec.pluginFormat = pluginIt->second.format;
                            caseSpec.sampleRate = sampleRate;
                            caseSpec.blockSize = blockSize;
                            caseSpec.channels = channels;
                            caseSpec.signal = signalIt->second;
                            caseSpec.thresholds = thresholdProfile;
                            caseSpec.timeoutSec = test.timeoutSec;
                            caseSpec.pluginvalRequired = suite.pluginval.required;
                            caseSpec.pluginvalStrictness = suite.pluginval.strictness;
                            caseSpec.runPluginval =
                                test.extra.value("runPluginval", test.type == "validate");
                            caseSpec.extra = test.extra;
                            resolveExtraPaths(suiteDir, caseSpec.extra);

                            if (pluginIt->second.defaultPreset) {
                                const auto presetPath =
                                    suiteDir.getChildFile(*pluginIt->second.defaultPreset);
                                caseSpec.presetPath = presetPath.getFullPathName().toStdString();
                            }

                            std::vector<ParameterAssignment> parameterSets = globalInit;
                            parameterSets = appendParams(parameterSets, module.set);
                            parameterSets = appendParams(parameterSets, modeVariant.set);
                            caseSpec.parameterSets = parameterSets;

                            if (!module.module.empty()) {
                                caseSpec.module = module.module;
                            }

                            if (!modeVariant.id.empty()) {
                                caseSpec.modeVariant = modeVariant.id;
                            }

                            auto caseId = test.id + "@" + std::to_string(sampleRate) + "@" +
                                          std::to_string(blockSize) + "@" +
                                          channelLayoutName(channels);
                            if (caseSpec.module) {
                                caseId += "@" + *caseSpec.module;
                            }
                            if (caseSpec.modeVariant) {
                                caseId += "@" + *caseSpec.modeVariant;
                            }

                            caseSpec.id = sanitizeId(caseId);

                            const juce::File artifactDir(runOutDir);
                            caseSpec.artifactsDir =
                                artifactDir.getChildFile("artifacts")
                                    .getChildFile(caseSpec.id)
                                    .getFullPathName()
                                    .toStdString();

                            cases.push_back(caseSpec);
                        }
                    }
                }
            }
        }
    }

    return cases;
}

nlohmann::json caseSpecToJson(const CaseSpec& caseSpec) {
    nlohmann::json j;
    j["schemaVersion"] = caseSpec.schemaVersion;
    j["id"] = caseSpec.id;
    j["suiteName"] = caseSpec.suiteName;
    j["testId"] = caseSpec.testId;
    j["testType"] = caseSpec.testType;
    j["pluginId"] = caseSpec.pluginId;
    j["pluginPath"] = caseSpec.pluginPath;
    j["pluginFormat"] = caseSpec.pluginFormat;
    j["sampleRate"] = caseSpec.sampleRate;
    j["blockSize"] = caseSpec.blockSize;
    j["channels"] = caseSpec.channels;
    j["signal"] = signalToJson(caseSpec.signal);
    j["thresholds"] = thresholdToJson(caseSpec.thresholds);
    j["parameterSets"] = parameterAssignmentsToJson(caseSpec.parameterSets);
    j["timeoutSec"] = caseSpec.timeoutSec;
    j["runPluginval"] = caseSpec.runPluginval;
    j["pluginvalRequired"] = caseSpec.pluginvalRequired;
    j["pluginvalStrictness"] = caseSpec.pluginvalStrictness;
    j["artifactsDir"] = caseSpec.artifactsDir;
    j["extra"] = caseSpec.extra;

    if (caseSpec.presetPath) {
        j["presetPath"] = *caseSpec.presetPath;
    }

    if (caseSpec.module) {
        j["module"] = *caseSpec.module;
    }

    if (caseSpec.modeVariant) {
        j["modeVariant"] = *caseSpec.modeVariant;
    }

    return j;
}

CaseSpec caseSpecFromJson(const nlohmann::json& j) {
    CaseSpec caseSpec;
    caseSpec.schemaVersion = j.value("schemaVersion", 1);
    caseSpec.id = getRequired<std::string>(j, "id");
    caseSpec.suiteName = getRequired<std::string>(j, "suiteName");
    caseSpec.testId = getRequired<std::string>(j, "testId");
    caseSpec.testType = getRequired<std::string>(j, "testType");
    caseSpec.pluginId = getRequired<std::string>(j, "pluginId");
    caseSpec.pluginPath = getRequired<std::string>(j, "pluginPath");
    caseSpec.pluginFormat = j.value("pluginFormat", "");
    caseSpec.sampleRate = j.value("sampleRate", 44100);
    caseSpec.blockSize = j.value("blockSize", 1024);
    caseSpec.channels = j.value("channels", 2);
    caseSpec.signal = signalFromJson(getRequired<nlohmann::json>(j, "signal"));
    caseSpec.thresholds = parseThresholdProfile(j.value("thresholds", nlohmann::json::object()));
    caseSpec.parameterSets = parameterAssignmentsFromJson(j.value("parameterSets", nlohmann::json::array()));
    caseSpec.timeoutSec = j.value("timeoutSec", 120);
    caseSpec.runPluginval = j.value("runPluginval", false);
    caseSpec.pluginvalRequired = j.value("pluginvalRequired", false);
    caseSpec.pluginvalStrictness = j.value("pluginvalStrictness", 5);
    caseSpec.artifactsDir = j.value("artifactsDir", "");
    caseSpec.extra = j.value("extra", nlohmann::json::object());

    if (j.contains("presetPath")) {
        caseSpec.presetPath = j["presetPath"].get<std::string>();
    }

    if (j.contains("module")) {
        caseSpec.module = j["module"].get<std::string>();
    }

    if (j.contains("modeVariant")) {
        caseSpec.modeVariant = j["modeVariant"].get<std::string>();
    }

    return caseSpec;
}

std::string channelLayoutName(int channels) {
    if (channels == 1) {
        return "mono";
    }

    if (channels == 2) {
        return "stereo";
    }

    return std::to_string(channels) + "ch";
}

} // namespace vstest
