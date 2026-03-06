#include "BaselineCommand.h"
#include "InspectCommand.h"
#include "ProcessCommand.h"
#include "ReportCommand.h"
#include "RunCommand.h"
#include "ScanCommand.h"
#include "UiServerCommand.h"
#include "Utils.h"
#include "ValidateCommand.h"
#include "WorkerCaseCommand.h"
#include <juce_events/juce_events.h>

namespace {

void registerSubcommand(CLI::App& app, CLICommand& command,
                        const std::vector<std::string>& aliases = {}, bool hidden = false) {
    auto sub = app.add_subcommand(command.createApp());
    for (const auto& alias : aliases) {
        sub->alias(alias);
    }

    if (hidden) {
        sub->group("");
    }

    sub->callback([&command]() { command.execute(); });
}

int runCommandLine(const std::string& commandLineParameters) {
    CLI::App app("vst-test - command-line audio plugin testing harness");

    ProcessCommand render;
    registerSubcommand(app, render, {"process"});

    InspectCommand inspect;
    registerSubcommand(app, inspect, {"listParameters"});

    ScanCommand scan;
    registerSubcommand(app, scan);

    ValidateCommand validate;
    registerSubcommand(app, validate);

    RunCommand run;
    registerSubcommand(app, run);

    BaselineCommand baseline;
    registerSubcommand(app, baseline);

    ReportCommand report;
    registerSubcommand(app, report);

    UiServerCommand uiServer;
    registerSubcommand(app, uiServer);

    WorkerCaseCommand worker;
    registerSubcommand(app, worker, {}, true);

    app.require_subcommand();

    try {
        app.parse(commandLineParameters, false);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    } catch (const CLIException& error) {
        std::cerr << error.what() << std::endl;
        return error.exitCode;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 2;
    }

    return 0;
}

class VstTestApplication : public juce::JUCEApplicationBase {
  public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    bool moreThanOneInstanceAllowed() override { return true; }

    void anotherInstanceStarted(const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void shutdown() override {}
    void unhandledException(const std::exception*, const juce::String&, int) override {}

    void systemRequestedQuit() override { quit(); }

    void initialise(const juce::String& commandLineParameters) override {
        const auto exitCode = runCommandLine(commandLineParameters.toStdString());
        setApplicationReturnValue(exitCode);
        quit();
    }
};

} // namespace

START_JUCE_APPLICATION(VstTestApplication)
