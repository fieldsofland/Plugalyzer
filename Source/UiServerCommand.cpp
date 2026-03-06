#include "UiServerCommand.h"

#include "ExitCodes.h"
#include "Utils.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <set>
#include <thread>

namespace {

using json = nlohmann::json;

struct PluginVersionInfo {
    std::string pluginId;
    std::string displayName;
    std::string format;
    std::string buildType;
    std::string version;
    std::string path;
    int64_t mtimeMs = 0;
};

struct EngineState {
    std::string sessionId;
    std::optional<PluginVersionInfo> activePlugin;
    std::map<std::string, double> parameterSnapshot;

    double sampleRate = 48000.0;
    int blockSize = 256;
    int channels = 2;

    bool playing = false;
    bool bypass = false;
    double playheadSec = 0.0;
    double loopStartSec = 0.0;
    double loopEndSec = 0.0;
    bool loopEnabled = false;
    double outputGainDb = 0.0;
    std::string inputFile;
};

std::string inferBuildType(const juce::String& path) {
    if (path.contains("/Debug/")) {
        return "Debug";
    }
    if (path.contains("/RelWithDebInfo/")) {
        return "RelWithDebInfo";
    }
    if (path.contains("/Release/")) {
        return "Release";
    }
    return "Unknown";
}

std::string inferFormat(const juce::File& file) {
    if (file.hasFileExtension(".vst3")) {
        return "vst3";
    }
    if (file.hasFileExtension(".component")) {
        return "au";
    }
    return "unknown";
}

std::string inferPluginId(const juce::String& path) {
    auto tokens = juce::StringArray::fromTokens(path, "/", "");
    for (int i = 0; i < tokens.size() - 1; ++i) {
        if (tokens[i] == "vst") {
            return tokens[i + 1].toStdString();
        }
    }
    return "unknown";
}

std::string inferVersion(const juce::String& displayName) {
    static const std::regex versionRegex("v([0-9]+(?:\\.[0-9]+)*)");
    std::smatch match;
    const std::string name = displayName.toStdString();
    if (std::regex_search(name, match, versionRegex)) {
        return match[1].str();
    }
    return "unknown";
}

std::vector<PluginVersionInfo> scanPlugins(const std::vector<std::string>& roots) {
    std::set<std::string> seenPaths;
    std::vector<PluginVersionInfo> out;

    for (const auto& rootPath : roots) {
        juce::File root(rootPath);
        if (!root.exists() || !root.isDirectory()) {
            continue;
        }

        juce::Array<juce::File> vst3Dirs;
        root.findChildFiles(vst3Dirs, juce::File::findDirectories, true, "*.vst3");

        juce::Array<juce::File> auDirs;
        root.findChildFiles(auDirs, juce::File::findDirectories, true, "*.component");

        auto consume = [&](const juce::Array<juce::File>& dirs) {
            for (const auto& dir : dirs) {
                const auto fullPath = dir.getFullPathName().toStdString();
                if (seenPaths.contains(fullPath)) {
                    continue;
                }

                const auto fullPathStr = dir.getFullPathName();
                if (!fullPathStr.contains("/build/") || !fullPathStr.contains("_artefacts/")) {
                    continue;
                }

                seenPaths.insert(fullPath);

                PluginVersionInfo info;
                info.path = fullPath;
                info.displayName = dir.getFileNameWithoutExtension().toStdString();
                info.format = inferFormat(dir);
                info.buildType = inferBuildType(fullPathStr);
                info.pluginId = inferPluginId(fullPathStr);
                info.version = inferVersion(dir.getFileNameWithoutExtension());
                info.mtimeMs = dir.getLastModificationTime().toMilliseconds();
                out.push_back(std::move(info));
            }
        };

        consume(vst3Dirs);
        consume(auDirs);
    }

    std::sort(out.begin(), out.end(), [](const PluginVersionInfo& a, const PluginVersionInfo& b) {
        if (a.pluginId != b.pluginId) {
            return a.pluginId < b.pluginId;
        }
        if (a.format != b.format) {
            return a.format < b.format;
        }
        if (a.version != b.version) {
            return a.version > b.version;
        }
        if (a.buildType != b.buildType) {
            return a.buildType < b.buildType;
        }
        return a.path < b.path;
    });

    return out;
}

json pluginToJson(const PluginVersionInfo& plugin) {
    return {
        {"pluginId", plugin.pluginId},   {"displayName", plugin.displayName},
        {"format", plugin.format},       {"buildType", plugin.buildType},
        {"version", plugin.version},     {"path", plugin.path},
        {"mtime", plugin.mtimeMs},
    };
}

std::vector<std::string> extractParameterNames(const std::string& pluginPath, double sampleRate,
                                               int blockSize) {
    auto plugin = PluginUtils::createPluginInstance(pluginPath, sampleRate, blockSize);
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(plugin->getParameters().size()));
    for (auto* param : plugin->getParameters()) {
        names.push_back(param->getName(1024).toStdString());
    }
    return names;
}

std::optional<json> extractJsonObject(const std::string& output) {
    const auto first = output.find('{');
    const auto last = output.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last <= first) {
        return std::nullopt;
    }

    try {
        return json::parse(output.substr(first, last - first + 1));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> createSuiteWithPluginOverride(const std::string& suitePath,
                                                         const std::string& pluginPath) {
    if (pluginPath.empty()) {
        return suitePath;
    }

    juce::File file(suitePath);
    if (!file.existsAsFile()) {
        return std::nullopt;
    }

    json suite;
    try {
        suite = json::parse(file.loadFileAsString().toStdString());
    } catch (...) {
        return std::nullopt;
    }

    if (!suite.contains("plugins") || !suite["plugins"].is_array()) {
        return std::nullopt;
    }

    for (auto& plugin : suite["plugins"]) {
        plugin["path"] = pluginPath;
    }

    const auto tempName =
        file.getFileNameWithoutExtension() + ".ui." + juce::String(juce::Time::currentTimeMillis()) + ".json";
    const auto tempFile = file.getSiblingFile(tempName);
    tempFile.replaceWithText(suite.dump(2));
    return tempFile.getFullPathName().toStdString();
}

class UiRpcServer {
  public:
    UiRpcServer(int p, std::string sid, std::vector<std::string> scanRoots)
        : port(p), roots(std::move(scanRoots)) {
        state.sessionId = std::move(sid);
        debugEnabled = juce::SystemStats::getEnvironmentVariable("VST_TEST_UI_DEBUG", "0") == "1";
    }

    ~UiRpcServer() { stop(); }

    void run() {
        if (!listener.createListener(port, "127.0.0.1")) {
            throw CLIException("ui-server failed to bind to 127.0.0.1:" + std::to_string(port),
                               ExitCode::InfrastructureError);
        }

        running = true;
        roots = roots.empty() ? std::vector<std::string>{"/Users/matt/dev/vst"} : roots;
        plugins = scanPlugins(roots);

        acceptThread = std::thread([this]() { acceptLoop(); });
        meterThread = std::thread([this]() { meterLoop(); });

        std::cout << "ui-server listening on 127.0.0.1:" << port << std::endl;
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }

    void stop() {
        if (!running) {
            return;
        }

        running = false;
        listener.close();

        {
            std::lock_guard<std::mutex> lock(testMutex);
            if (activeTestProcess) {
                activeTestProcess->kill();
            }
        }

        if (acceptThread.joinable()) {
            acceptThread.join();
        }
        if (meterThread.joinable()) {
            meterThread.join();
        }
    }

  private:
    int port = 0;
    std::vector<std::string> roots;

    std::atomic<bool> running{false};
    juce::StreamingSocket listener;
    std::thread acceptThread;
    std::thread meterThread;

    std::mutex clientMutex;
    std::shared_ptr<juce::StreamingSocket> clientSocket;

    std::mutex stateMutex;
    EngineState state;
    std::vector<PluginVersionInfo> plugins;

    std::mutex testMutex;
    std::unique_ptr<juce::ChildProcess> activeTestProcess;
    std::atomic<bool> cancelRequested{false};
    bool debugEnabled = false;

    void debugLog(const std::string& message) const {
        if (debugEnabled) {
            std::cerr << "[ui-server-debug] " << message << std::endl;
        }
    }

    void acceptLoop() {
        while (running) {
            juce::StreamingSocket* raw = listener.waitForNextConnection();
            if (!running) {
                delete raw;
                break;
            }
            if (raw == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            auto socket = std::shared_ptr<juce::StreamingSocket>(raw);
            debugLog("client connected");
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                clientSocket = socket;
            }

            std::thread([this, socket]() { readLoop(socket); }).detach();
        }
    }

    void readLoop(const std::shared_ptr<juce::StreamingSocket>& socket) {
        std::string buffered;
        buffered.reserve(4096);

        char temp[2048];
        while (running) {
            const int ready = socket->waitUntilReady(true, 100);
            if (ready < 0) {
                break;
            }
            if (ready == 0) {
                continue;
            }

            const int read = socket->read(temp, static_cast<int>(sizeof(temp)), false);
            if (read < 0) {
                continue;
            }
            if (read == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            debugLog("read bytes=" + std::to_string(read));

            buffered.append(temp, static_cast<size_t>(read));

            for (;;) {
                const auto pos = buffered.find('\n');
                if (pos == std::string::npos) {
                    break;
                }
                std::string line = buffered.substr(0, pos);
                buffered.erase(0, pos + 1);
                line = juce::String(line).trim().toStdString();
                if (!line.empty()) {
                    debugLog("line=" + line);
                    handleLine(line);
                }
            }
        }

        std::lock_guard<std::mutex> lock(clientMutex);
        if (clientSocket == socket) {
            clientSocket.reset();
        }
    }

    void sendJson(const json& message) {
        std::shared_ptr<juce::StreamingSocket> socket;
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            socket = clientSocket;
        }

        if (!socket) {
            return;
        }

        const auto payload = message.dump() + "\n";
        debugLog("send=" + payload);
        const auto bytes = socket->write(payload.c_str(), static_cast<int>(payload.size()));
        debugLog("sent-bytes=" + std::to_string(bytes));
    }

    void sendEvent(const std::string& method, const json& params) {
        sendJson({
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", params},
        });
    }

    void sendResult(const json& id, const json& result) {
        sendJson({
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result},
        });
    }

    void sendError(const json& id, int code, const std::string& message) {
        sendJson({
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {{"code", code}, {"message", message}}},
        });
    }

    json engineStateToJson() {
        std::lock_guard<std::mutex> lock(stateMutex);
        json out = {
            {"sessionId", state.sessionId},
            {"sampleRate", state.sampleRate},
            {"blockSize", state.blockSize},
            {"channels", state.channels},
            {"playing", state.playing},
            {"bypass", state.bypass},
            {"playheadSec", state.playheadSec},
            {"loopEnabled", state.loopEnabled},
            {"loopStartSec", state.loopStartSec},
            {"loopEndSec", state.loopEndSec},
            {"outputGainDb", state.outputGainDb},
            {"inputFile", state.inputFile},
        };
        if (state.activePlugin) {
            out["activePlugin"] = pluginToJson(*state.activePlugin);
        }
        return out;
    }

    json pluginListToJson() {
        json items = json::array();
        for (const auto& plugin : plugins) {
            items.push_back(pluginToJson(plugin));
        }
        return items;
    }

    void emitPluginUpdated() {
        sendEvent("plugins.updated", {{"plugins", pluginListToJson()}});
    }

    void handleLine(const std::string& line) {
        json request;
        try {
            request = json::parse(line);
        } catch (const std::exception& e) {
            sendError(nullptr, -32700, std::string("Invalid JSON: ") + e.what());
            return;
        }

        const json id = request.contains("id") ? request["id"] : nullptr;
        const auto method = request.value("method", "");
        const json params = request.value("params", json::object());

        try {
            if (method == "server.ping") {
                sendResult(id, {{"ok", true}, {"session", state.sessionId}});
                return;
            }
            if (method == "server.shutdown") {
                sendResult(id, {{"ok", true}});
                running = false;
                return;
            }

            if (method == "plugins.scan") {
                std::vector<std::string> scanRoots = roots;
                if (params.contains("roots") && params["roots"].is_array()) {
                    scanRoots.clear();
                    for (const auto& item : params["roots"]) {
                        if (item.is_string()) {
                            scanRoots.push_back(item.get<std::string>());
                        }
                    }
                }
                plugins = scanPlugins(scanRoots);
                sendResult(id, {{"plugins", pluginListToJson()}});
                emitPluginUpdated();
                return;
            }
            if (method == "plugins.list") {
                sendResult(id, {{"plugins", pluginListToJson()}});
                return;
            }
            if (method == "plugins.activateVersion") {
                const auto path = params.value("path", "");
                if (path.empty()) {
                    sendError(id, -32602, "plugins.activateVersion requires params.path");
                    return;
                }

                auto it =
                    std::find_if(plugins.begin(), plugins.end(),
                                 [&](const PluginVersionInfo& p) { return p.path == path; });
                if (it == plugins.end()) {
                    sendError(id, -32602, "Plugin path not found in scanned registry");
                    return;
                }

                std::vector<std::string> oldParams;
                std::vector<std::string> newParams;

                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    if (state.activePlugin) {
                        try {
                            oldParams = extractParameterNames(state.activePlugin->path, state.sampleRate,
                                                              state.blockSize);
                        } catch (...) {
                            oldParams.clear();
                        }
                    }
                }

                try {
                    newParams = extractParameterNames(it->path, engineStateToJson().value("sampleRate", 48000.0),
                                                      engineStateToJson().value("blockSize", 256));
                } catch (...) {
                    newParams.clear();
                }

                std::set<std::string> oldSet(oldParams.begin(), oldParams.end());
                std::set<std::string> newSet(newParams.begin(), newParams.end());

                json missing = json::array();
                json added = json::array();
                int shared = 0;

                for (const auto& oldName : oldSet) {
                    if (!newSet.contains(oldName)) {
                        missing.push_back(oldName);
                    } else {
                        ++shared;
                    }
                }
                for (const auto& newName : newSet) {
                    if (!oldSet.contains(newName)) {
                        added.push_back(newName);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.activePlugin = *it;
                }

                sendResult(id,
                           {
                               {"activePlugin", pluginToJson(*it)},
                               {"compatibilityDiff",
                                {
                                    {"sharedParamCount", shared},
                                    {"missingParams", missing},
                                    {"addedParams", added},
                                }},
                           });
                sendEvent("transport.state", engineStateToJson());
                return;
            }

            if (method == "transport.loadFile") {
                const auto path = params.value("path", "");
                if (path.empty()) {
                    sendError(id, -32602, "transport.loadFile requires params.path");
                    return;
                }
                juce::File file(path);
                if (!file.existsAsFile()) {
                    sendError(id, -32602, "Input file does not exist");
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.inputFile = path;
                    state.playheadSec = 0.0;
                }
                sendResult(id, engineStateToJson());
                sendEvent("transport.state", engineStateToJson());
                return;
            }
            if (method == "transport.play") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.playing = true;
                }
                sendResult(id, engineStateToJson());
                sendEvent("transport.state", engineStateToJson());
                return;
            }
            if (method == "transport.pause") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.playing = false;
                }
                sendResult(id, engineStateToJson());
                sendEvent("transport.state", engineStateToJson());
                return;
            }
            if (method == "transport.seek") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.playheadSec = std::max(0.0, params.value("seconds", 0.0));
                }
                sendResult(id, engineStateToJson());
                sendEvent("transport.state", engineStateToJson());
                return;
            }
            if (method == "transport.setLoop") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.loopStartSec = std::max(0.0, params.value("startSec", 0.0));
                    state.loopEndSec = std::max(state.loopStartSec, params.value("endSec", 0.0));
                    state.loopEnabled = params.value("enabled", false);
                }
                sendResult(id, engineStateToJson());
                sendEvent("transport.state", engineStateToJson());
                return;
            }

            if (method == "engine.setSampleRate") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.sampleRate = std::max(8000.0, params.value("sampleRate", state.sampleRate));
                }
                sendResult(id, engineStateToJson());
                return;
            }
            if (method == "engine.setBlockSize") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.blockSize = std::max(16, params.value("blockSize", state.blockSize));
                }
                sendResult(id, engineStateToJson());
                return;
            }
            if (method == "engine.setChannels") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.channels = juce::jlimit(1, 8, params.value("channels", state.channels));
                }
                sendResult(id, engineStateToJson());
                return;
            }
            if (method == "engine.setParam") {
                const auto name = params.value("name", "");
                if (name.empty()) {
                    sendError(id, -32602, "engine.setParam requires params.name");
                    return;
                }
                const auto value = params.value("value", 0.0);
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.parameterSnapshot[name] = value;
                }
                sendResult(id, {{"ok", true}});
                return;
            }
            if (method == "engine.reset") {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.parameterSnapshot.clear();
                    state.playheadSec = 0.0;
                }
                sendResult(id, engineStateToJson());
                return;
            }

            if (method == "tests.runSuite") {
                runTestCommand(id, params, false);
                return;
            }
            if (method == "tests.runCase") {
                runTestCommand(id, params, true);
                return;
            }
            if (method == "tests.cancel") {
                cancelRequested = true;
                {
                    std::lock_guard<std::mutex> lock(testMutex);
                    if (activeTestProcess) {
                        activeTestProcess->kill();
                    }
                }
                sendResult(id, {{"ok", true}});
                sendEvent("tests.completed", {{"cancelled", true}});
                return;
            }

            sendError(id, -32601, "Unknown method: " + method);
        } catch (const std::exception& e) {
            sendError(id, -32000, e.what());
            sendEvent("engine.error", {{"message", e.what()}});
        }
    }

    void runTestCommand(const json& id, const json& params, bool singleCase) {
        const auto suite = params.value("suite", "");
        if (suite.empty()) {
            sendError(id, -32602, "tests.runSuite/tests.runCase require params.suite");
            return;
        }

        const auto outDir = params.value("outDir", ".vst-test/runs-ui");
        const int jobs = std::max(1, params.value("jobs", 1));
        const int maxSummaryCases = std::max(1, params.value("maxSummaryCases", 20));
        const auto caseId = params.value("caseId", "");
        const auto pluginPathOverride = params.value("pluginPath", "");

        if (singleCase && caseId.empty()) {
            sendError(id, -32602, "tests.runCase requires params.caseId");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(testMutex);
            if (activeTestProcess) {
                sendError(id, -32000, "A test run is already in progress");
                return;
            }
        }

        cancelRequested = false;

        const auto executable =
            juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();

        std::thread([this, id, suite, singleCase, caseId, outDir, jobs, maxSummaryCases,
                     pluginPathOverride, executable]() {
            sendEvent("tests.started",
                      {
                          {"suite", suite},
                          {"singleCase", singleCase},
                          {"caseId", caseId},
                      });

            auto process = std::make_unique<juce::ChildProcess>();
            {
                std::lock_guard<std::mutex> lock(testMutex);
                activeTestProcess = std::move(process);
            }

            auto suiteToRun = createSuiteWithPluginOverride(suite, pluginPathOverride);
            if (!suiteToRun) {
                sendEvent("tests.completed",
                          {{"ok", false},
                           {"error", "Failed to prepare suite file with plugin override"}});
                std::lock_guard<std::mutex> lock(testMutex);
                activeTestProcess.reset();
                return;
            }

            const bool tempSuite = (*suiteToRun != suite);

            juce::String command = "\"" + executable + "\" run --suite \"" + juce::String(*suiteToRun) +
                                   "\" --out-dir \"" + juce::String(outDir) + "\" --jobs " +
                                   juce::String(jobs) + " --json-summary --max-summary-cases " +
                                   juce::String(maxSummaryCases);

            if (singleCase) {
                command += " --select \"" + juce::String(caseId) + "\"";
            }

            bool started = false;
            {
                std::lock_guard<std::mutex> lock(testMutex);
                started = activeTestProcess && activeTestProcess->start(command);
            }

            if (!started) {
                sendEvent("tests.completed",
                          {{"ok", false}, {"error", "Failed to start test process"}});
                std::lock_guard<std::mutex> lock(testMutex);
                activeTestProcess.reset();
                return;
            }

            auto startedAt = std::chrono::steady_clock::now();
            bool finished = false;
            while (!finished && !cancelRequested) {
                {
                    std::lock_guard<std::mutex> lock(testMutex);
                    if (!activeTestProcess) {
                        break;
                    }
                    finished = activeTestProcess->waitForProcessToFinish(250);
                }

                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                           startedAt)
                        .count() /
                    1000.0;
                sendEvent("tests.progress", {{"elapsedSec", elapsed}});
            }

            std::string output;
            int exitCode = 0;

            {
                std::lock_guard<std::mutex> lock(testMutex);
                if (activeTestProcess) {
                    output = activeTestProcess->readAllProcessOutput().toStdString();
                    exitCode = static_cast<int>(activeTestProcess->getExitCode());
                    activeTestProcess.reset();
                }
            }

            const auto parsed = extractJsonObject(output);
            if (parsed) {
                sendEvent("tests.metric", {{"summary", parsed->value("summary", json::object())}});
                if (parsed->contains("nonPassing") && (*parsed)["nonPassing"].contains("cases")) {
                    for (const auto& caseJson : (*parsed)["nonPassing"]["cases"]) {
                        sendEvent("tests.visual",
                                  {
                                      {"testType", caseJson.value("testType", "unknown")},
                                      {"caseId", caseJson.value("id", "")},
                                      {"metrics", caseJson.value("metrics", json::object())},
                                  });
                    }
                }
                sendEvent("tests.completed", {{"ok", exitCode == 0}, {"exitCode", exitCode}, {"result", *parsed}});
            } else {
                sendEvent("tests.completed",
                          {
                              {"ok", exitCode == 0},
                              {"exitCode", exitCode},
                              {"rawOutput", output},
                          });
            }

            if (tempSuite) {
                juce::File(*suiteToRun).deleteFile();
            }
        }).detach();

        sendResult(id, {{"accepted", true}});
    }

    void meterLoop() {
        double phase = 0.0;
        int tick = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            phase += 0.15;
            ++tick;

            json stateJson;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (state.playing) {
                    state.playheadSec += 0.033;
                    if (state.loopEnabled && state.loopEndSec > state.loopStartSec &&
                        state.playheadSec >= state.loopEndSec) {
                        state.playheadSec = state.loopStartSec;
                    }
                }

                const double inPeak = -18.0 + 6.0 * std::sin(phase);
                const double outPeak = state.bypass ? inPeak : inPeak + 1.0 + 2.0 * std::sin(phase * 0.37);
                const double outRms = outPeak - 8.0;
                const double outLufs = outRms - 1.0;
                const double cpu = state.playing ? 4.0 + 2.0 * std::abs(std::sin(phase * 0.2)) : 1.0;

                sendEvent("transport.meters",
                          {
                              {"inputPeakDbfs", inPeak},
                              {"outputPeakDbfs", outPeak},
                              {"outputRmsDbfs", outRms},
                              {"outputLufs", outLufs},
                              {"cpuPercent", cpu},
                              {"xruns", 0},
                          });

                json bins = json::array();
                for (int i = 0; i < 48; ++i) {
                    const double value = -90.0 + 40.0 * std::abs(std::sin(phase * 0.3 + i * 0.17));
                    bins.push_back(value);
                }
                sendEvent("transport.spectrum", {{"binsDb", bins}});
            }

            if (tick % 8 == 0) {
                sendEvent("transport.state", engineStateToJson());
            }
        }
    }
};

} // namespace

std::shared_ptr<CLI::App> UiServerCommand::createApp() {
    auto app = std::make_shared<CLI::App>("Run native UI sidecar server for desktop app", "ui-server");
    app->add_option("--port", port, "Localhost port for JSON-RPC sidecar")->check(CLI::Range(1024, 65535));
    app->add_option("--session", sessionId, "Session identifier for UI/runtime state");
    app->add_option("--root", roots,
                    "Plugin scan root(s), can be repeated. Default: /Users/matt/dev/vst");
    return app;
}

void UiServerCommand::execute() {
    UiRpcServer server(port, sessionId, roots);
    server.run();
}
