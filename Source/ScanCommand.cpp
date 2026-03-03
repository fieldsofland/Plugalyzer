#include "ScanCommand.h"

#include "Utils.h"
#include <nlohmann/json.hpp>
#include <set>

namespace {

bool candidateMatchesFormat(const juce::File& file, const std::optional<std::string>& formatFilter) {
    const auto extension = file.getFileExtension().toLowerCase();

    if (!formatFilter) {
        return extension == ".vst3" || extension == ".component" || extension == ".lv2" ||
               extension == ".dll" || extension == ".so";
    }

    const auto filter = juce::String(*formatFilter).toLowerCase();

    if (filter == "vst3") {
        return extension == ".vst3";
    }

    if (filter == "au") {
        return extension == ".component";
    }

    if (filter == "lv2") {
        return extension == ".lv2";
    }

    if (filter == "ladspa") {
        return extension == ".so";
    }

    return false;
}

std::vector<juce::File> defaultSearchPaths() {
    std::vector<juce::File> paths;

#if JUCE_MAC
    paths.emplace_back("/Library/Audio/Plug-Ins");
    paths.emplace_back(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                           .getChildFile("Library/Audio/Plug-Ins"));
#elif JUCE_WINDOWS
    paths.emplace_back("C:/Program Files/Common Files/VST3");
    paths.emplace_back("C:/Program Files/VSTPlugins");
#elif JUCE_LINUX
    paths.emplace_back("/usr/lib/vst3");
    paths.emplace_back("/usr/local/lib/vst3");
    paths.emplace_back("/usr/lib/lv2");
    paths.emplace_back("/usr/local/lib/lv2");
#endif

    return paths;
}

} // namespace

std::shared_ptr<CLI::App> ScanCommand::createApp() {
    auto app = std::make_shared<CLI::App>("Scan plugin directories and print inventory", "scan");

    app->add_option("--search-path", searchPaths,
                    "Directory to recursively scan for plugins (can be repeated)");
    app->add_option("--format", formatFilter, "Optional format filter: vst3, au, lv2, ladspa");
    app->add_flag("--json", jsonOutput, "Print machine-readable JSON output");

    return app;
}

void ScanCommand::execute() {
    if (searchPaths.empty()) {
        searchPaths = defaultSearchPaths();
    }

    juce::AudioPluginFormatManager formatManager;
    addEnabledPluginFormats(formatManager);

    juce::KnownPluginList knownPlugins;
    juce::OwnedArray<juce::PluginDescription> tempDescriptions;

    std::set<std::string> visitedPaths;

    for (const auto& root : searchPaths) {
        if (!root.exists()) {
            continue;
        }

        juce::RangedDirectoryIterator iterator(root, true, "*",
                                               juce::File::findFilesAndDirectories);

        for (const auto& entry : iterator) {
            auto candidate = entry.getFile();
            if (!candidate.exists()) {
                continue;
            }

            if (!candidateMatchesFormat(candidate, formatFilter)) {
                continue;
            }

            const auto key = candidate.getFullPathName().toStdString();
            if (visitedPaths.contains(key)) {
                continue;
            }

            visitedPaths.insert(key);

            juce::OwnedArray<juce::PluginDescription> found;
            knownPlugins.scanAndAddDragAndDroppedFiles(formatManager,
                                                       juce::StringArray(candidate.getFullPathName()),
                                                       found);

            for (auto* description : found) {
                tempDescriptions.add(new juce::PluginDescription(*description));
            }
        }
    }

    if (jsonOutput) {
        nlohmann::json j = nlohmann::json::array();
        for (auto* desc : tempDescriptions) {
            nlohmann::json item;
            item["name"] = desc->name.toStdString();
            item["format"] = desc->pluginFormatName.toStdString();
            item["identifier"] = desc->fileOrIdentifier.toStdString();
            item["manufacturer"] = desc->manufacturerName.toStdString();
            item["version"] = desc->version.toStdString();
            j.push_back(item);
        }

        std::cout << j.dump(2) << std::endl;
        return;
    }

    if (tempDescriptions.isEmpty()) {
        std::cout << "No plugins found." << std::endl;
        return;
    }

    std::cout << "Discovered plugins:" << std::endl;
    for (auto* desc : tempDescriptions) {
        std::cout << "- [" << desc->pluginFormatName << "] " << desc->name << " -> "
                  << desc->fileOrIdentifier << std::endl;
    }
}
