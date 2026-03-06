cmake_minimum_required(VERSION 3.15)
project(VST_TEST VERSION 1.0.0)

# embrace the future, baby!
set(CMAKE_CXX_STANDARD 20)

if(APPLE)
    # JUCE submodule version in this repo uses APIs obsoleted at macOS 15 SDK defaults.
    # Pin deployment target to 14 for broad host compatibility and successful toolchain builds.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment target" FORCE)
    add_compile_definitions(MAC_OS_X_VERSION_MAX_ALLOWED=140000)
endif()

# Allow older subproject CMake minimum requirements under newer CMake policy behavior.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# include JUCE from submodule
add_subdirectory(JUCE)
juce_add_console_app(Plugalyzer
    COMPANY_NAME CrushedPixel
    )
set_target_properties(Plugalyzer PROPERTIES OUTPUT_NAME "vst-test")

# include CLI11 from submodule
add_subdirectory(CLI11)
target_include_directories(Plugalyzer PRIVATE CLI11/include)

#include nlohmann/json from submodule
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(json)
target_include_directories(Plugalyzer PRIVATE json/include)

target_sources(Plugalyzer
    PRIVATE
    Source/main.cpp
    Source/Utils.cpp
    Source/PresetLoadingExtensionsVisitor.cpp
    Source/ProcessCommand.cpp
    Source/Automation.cpp
    Source/ScanCommand.cpp
    Source/InspectCommand.cpp
    Source/ValidateCommand.cpp
    Source/UiServerCommand.cpp
    Source/RunCommand.cpp
    Source/BaselineCommand.cpp
    Source/ReportCommand.cpp
    Source/WorkerCaseCommand.cpp
    Source/harness/Results.cpp
    Source/harness/Config.cpp
    Source/harness/SignalGenerator.cpp
    Source/harness/RenderEngine.cpp
    Source/harness/Analyzers.cpp
    Source/harness/Baseline.cpp
    Source/harness/Reporting.cpp
    )

target_compile_definitions(Plugalyzer
    PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Plugalyzer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Plugalyzer,JUCE_VERSION>"

    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=1
    JUCE_PLUGINHOST_LV2=1
    )

target_link_libraries(Plugalyzer
    PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_dsp

    CLI11::CLI11
    nlohmann_json::nlohmann_json

    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_custom_command(
    TARGET Plugalyzer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Plugalyzer>" "$<TARGET_FILE_DIR:Plugalyzer>/plugalyzer"
    COMMENT "Creating legacy plugalyzer binary alias"
)
