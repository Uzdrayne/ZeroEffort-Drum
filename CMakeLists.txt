cmake_minimum_required(VERSION 3.22)
project(ZeroEffortDrum VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(ara_sdk
    GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
    GIT_TAG releases/2.2.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ara_sdk)
juce_set_ara_sdk_path("${ara_sdk_SOURCE_DIR}")
include(cmake/PatchJuceARABuses.cmake)

juce_add_plugin(ZeroEffortDrum
    COMPANY_NAME "Robin"
    COMPANY_WEBSITE "https://example.invalid/zeroeffort-drum"
    VERSION 0.10.0
    BUNDLE_ID "com.robin.zeroeffortdrum"
    PLUGIN_MANUFACTURER_CODE Rbin
    PLUGIN_CODE Zedr
    PRODUCT_NAME "ZeroEffort Drum"
    DESCRIPTION "Drum channel strip with instrument Auto presets, kit phase alignment, bus Glue and ARA playback"
    FORMATS VST3
    IS_ARA_EFFECT TRUE
    ARA_FACTORY_ID "\"com.robin.zeroeffortdrum.factory.1\""
    ARA_DOCUMENT_ARCHIVE_ID "\"com.robin.zeroeffortdrum.archive.1\""
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools)
target_sources(ZeroEffortDrum PRIVATE
    Source/ARA/ZeroEffortDrumProcessor.cpp
    Source/ARA/ZeroEffortDrumProcessor.h
    Source/ARA/RegionCache.h
    Source/Kit/KitTypes.h
    Source/Kit/KitAnalysis.h
    Source/Kit/KitAligner.h
    Source/Kit/KitRegistry.h
    Source/Kit/KitRegistry.cpp
    Source/UI/DrumLookAndFeel.h
    Source/UI/DrumControlNames.h
    Source/UI/ZeroEffortDrumEditor.h
    Source/UI/ZeroEffortDrumEditor.cpp)
target_include_directories(ZeroEffortDrum PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/ARA")
target_compile_definitions(ZeroEffortDrum PUBLIC
    DRUM_ARA_CUBASE_BUS_WORKAROUND=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ZeroEffortDrum PUBLIC juce::juce_dsp PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(RegionCacheTests PRODUCT_NAME "Region Cache Tests")
    target_sources(RegionCacheTests PRIVATE tests/RegionCacheTests.cpp)
    target_include_directories(RegionCacheTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_compile_definitions(RegionCacheTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(RegionCacheTests PRIVATE juce::juce_audio_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME RegionCacheTests COMMAND RegionCacheTests)

    juce_add_console_app(ZeroEffortDrumTests PRODUCT_NAME "ZeroEffort Drum Tests")
    target_sources(ZeroEffortDrumTests PRIVATE tests/ZeroEffortDrumProcessorTests.cpp tests/KitTests.cpp tests/EditorTests.cpp tests/Screenshots.cpp
        "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp")
    target_include_directories(ZeroEffortDrumTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_compile_definitions(ZeroEffortDrumTests PRIVATE JucePlugin_Build_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1
        ZED_WAV_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/wav_test_file")
    target_link_libraries(ZeroEffortDrumTests PRIVATE ZeroEffortDrum juce::juce_audio_utils)
    add_test(NAME ZeroEffortDrumTests COMMAND ZeroEffortDrumTests)

    juce_add_console_app(ARADiscoveryTests PRODUCT_NAME "ARA Discovery Tests")
    target_sources(ARADiscoveryTests PRIVATE tests/ARADiscoveryTests.cpp)
    target_include_directories(ARADiscoveryTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/ARA")
    target_compile_definitions(ARADiscoveryTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_ARA=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ARADiscoveryTests PRIVATE juce::juce_audio_utils juce_ara_sdk
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(ARADiscoveryTests ZeroEffortDrum_VST3)
    add_test(NAME ARADiscoveryTests COMMAND ARADiscoveryTests
        "${CMAKE_CURRENT_BINARY_DIR}/ZeroEffortDrum_artefacts/$<CONFIG>/VST3/ZeroEffort Drum.vst3")
    set_tests_properties(ARADiscoveryTests PROPERTIES TIMEOUT 30)
endif()