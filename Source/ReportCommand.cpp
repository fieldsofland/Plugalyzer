#include "ReportCommand.h"

#include "Utils.h"
#include "harness/Reporting.h"
#include "harness/Results.h"
#include <nlohmann/json.hpp>

std::shared_ptr<CLI::App> ReportCommand::createApp() {
    auto app =
        std::make_shared<CLI::App>("Regenerate report artifacts from results.json", "report");

    app->add_option("--results", resultsFile, "Path to results.json")
        ->required()
        ->check(CLI::ExistingFile);
    app->add_option("--formats", formats,
                    "Comma-separated output formats to generate (json,junit,html)");

    return app;
}

void ReportCommand::execute() {
    const auto parsed = nlohmann::json::parse(resultsFile.loadFileAsString().toStdString());
    const auto runResult = vstest::runResultFromJson(parsed);

    auto outputDir = resultsFile.getParentDirectory();
    auto selected = juce::StringArray::fromTokens(formats, ",", "");

    for (auto token : selected) {
        token = token.trim().toLowerCase();
        if (token == "json") {
            vstest::Reporter::writeJson(runResult, outputDir.getChildFile("results.json"));
        } else if (token == "junit") {
            vstest::Reporter::writeJunit(runResult, outputDir.getChildFile("junit.xml"));
        } else if (token == "html") {
            vstest::Reporter::writeHtml(runResult, outputDir.getChildFile("report.html"));
        } else if (!token.isEmpty()) {
            throw CLIException("Unknown report format: " + token);
        }
    }

    std::cout << "Regenerated reports in " << outputDir.getFullPathName() << std::endl;
}
