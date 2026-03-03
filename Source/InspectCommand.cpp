#include "InspectCommand.h"

#include "Automation.h"
#include "Utils.h"
#include <nlohmann/json.hpp>

std::shared_ptr<CLI::App> InspectCommand::createApp() {
    auto app = std::make_shared<CLI::App>("Inspect plugin metadata and parameters", "inspect");

    app->add_option("-p,--plugin", pluginPath, "Plugin path")->required()->check(CLI::ExistingPath);
    app->add_option("-s,--sample-rate", sampleRate,
                    "Sample rate used to initialize plugin for parameter introspection");
    app->add_option("-b,--block-size", blockSize,
                    "Block size used to initialize plugin for parameter introspection");
    app->add_flag("--json", jsonOutput, "Print machine-readable JSON output");

    return app;
}

void InspectCommand::execute() {
    auto plugin = PluginUtils::createPluginInstance(pluginPath, sampleRate, static_cast<int>(blockSize));

    if (jsonOutput) {
        nlohmann::json out;
        out["name"] = plugin->getName().toStdString();
        out["pluginPath"] = pluginPath.toStdString();
        out["sampleRate"] = sampleRate;
        out["blockSize"] = blockSize;
        out["mainInputChannels"] = plugin->getMainBusNumInputChannels();
        out["mainOutputChannels"] = plugin->getMainBusNumOutputChannels();

        auto paramsJson = nlohmann::json::array();
        for (auto* param : plugin->getParameters()) {
            nlohmann::json item;
            item["index"] = param->getParameterIndex();
            item["name"] = param->getName(1024).toStdString();
            item["default"] = param->getText(param->getDefaultValue(), 1024).toStdString();
            item["supportsTextValues"] = Automation::parameterSupportsTextToValueConversion(param);

            auto values = param->getAllValueStrings();
            nlohmann::json allowedValues = nlohmann::json::array();
            for (const auto& value : values) {
                allowedValues.push_back(value.toStdString());
            }
            item["allowedValues"] = allowedValues;

            paramsJson.push_back(item);
        }

        out["parameters"] = paramsJson;
        std::cout << out.dump(2) << std::endl;
        return;
    }

    std::cout << "Plugin: " << plugin->getName() << std::endl;
    std::cout << "Main input channels: " << plugin->getMainBusNumInputChannels() << std::endl;
    std::cout << "Main output channels: " << plugin->getMainBusNumOutputChannels() << std::endl;
    std::cout << std::endl;
    std::cout << "Parameters:" << std::endl;

    for (auto* param : plugin->getParameters()) {
        std::cout << param->getParameterIndex() << ": " << param->getName(1024) << std::endl;
        std::cout << "  Default: " << param->getText(param->getDefaultValue(), 1024)
                  << param->getLabel() << std::endl;
        std::cout << "  Supports text values: " << std::boolalpha
                  << Automation::parameterSupportsTextToValueConversion(param) << std::endl;
    }
}
