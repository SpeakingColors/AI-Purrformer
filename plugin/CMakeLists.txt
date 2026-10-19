cmake_minimum_required(VERSION 3.22)
project(AIPurrformer VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: pass -DJUCE_DIR=/path/to/JUCE, or it is fetched from GitHub (tag 8.0.16-security).
if(NOT JUCE_DIR)
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.16-security GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
else()
  add_subdirectory(${JUCE_DIR} juce)
endif()

# Studio 87 look and feel (wood cheeks, cat head, RenderGate). Headers and one source file are used directly.
include(FetchContent)
FetchContent_Declare(cat_ui GIT_REPOSITORY https://github.com/SpeakingColors/cat-ui.git GIT_TAG main GIT_SHALLOW ON)
FetchContent_GetProperties(cat_ui)
if(NOT cat_ui_POPULATED)
  FetchContent_Populate(cat_ui)
endif()

set(PURR_CORE src/PluginProcessor.cpp src/PluginEditor.cpp src/EngineClient.cpp)
set(PURR_SOURCES ${PURR_CORE})
set(PURR_FX_SOURCES ${PURR_CORE} src/FxProcessor.cpp src/FxEditor.cpp ${cat_ui_SOURCE_DIR}/src/CatLookAndFeel.cpp)
if(UNIX AND NOT APPLE)
  set(PURR_CURL JUCE_USE_CURL=1)   # Windows and macOS use their native HTTP stacks
else()
  set(PURR_CURL JUCE_USE_CURL=0)
endif()
set(PURR_DEFS JUCE_WEB_BROWSER=0 ${PURR_CURL} JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
set(PURR_LIBS
  PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_gui_basics juce::juce_events
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# 1) AI Purrformer: the FX plugin. Audio in, sidechain in, MIDI in; drag the result onto any track.
juce_add_plugin(AIPurrformer
  COMPANY_NAME "Studio 87"
  PRODUCT_NAME "AI Purrformer"
  PLUGIN_MANUFACTURER_CODE Spkc
  PLUGIN_CODE Purr
  FORMATS VST3
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Fx
  NEEDS_CURL TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(AIPurrformer PRIVATE ${PURR_FX_SOURCES})
target_include_directories(AIPurrformer PRIVATE ${cat_ui_SOURCE_DIR}/include)
target_compile_definitions(AIPurrformer PUBLIC ${PURR_DEFS} PURR_FX=1)
target_link_libraries(AIPurrformer ${PURR_LIBS})

# 2) AI Purrformer Sing: the small instrument version (MIDI + lyrics in, voice out). Works in every host.
juce_add_plugin(AIPurrformerSing
  COMPANY_NAME "Studio 87"
  PRODUCT_NAME "AI Purrformer Sing"
  PLUGIN_MANUFACTURER_CODE Spkc
  PLUGIN_CODE Prrs
  FORMATS VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Instrument Synth
  NEEDS_CURL TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(AIPurrformerSing PRIVATE ${PURR_SOURCES})
target_compile_definitions(AIPurrformerSing PUBLIC ${PURR_DEFS})
target_link_libraries(AIPurrformerSing ${PURR_LIBS})

# Headless self test: needs the engine running on 127.0.0.1:8765.
juce_add_console_app(purr_selftest PRODUCT_NAME "purr_selftest" NEEDS_CURL TRUE)
target_sources(purr_selftest PRIVATE tests/selftest.cpp ${PURR_FX_SOURCES})
target_include_directories(purr_selftest PRIVATE ${cat_ui_SOURCE_DIR}/include)
target_compile_definitions(purr_selftest PUBLIC PURR_FX=1 JUCE_WEB_BROWSER=0 ${PURR_CURL}
  JUCE_PLUGINHOST_VST3=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="AI Purrformer")
target_link_libraries(purr_selftest PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_gui_basics juce::juce_events
  PUBLIC juce::juce_recommended_config_flags)

# Pictures of the editor for the manual (not part of a release). cmake -DPURR_BUILD_SHOT=ON
option(PURR_BUILD_SHOT "Build purr_screenshot" OFF)
if(PURR_BUILD_SHOT)
  juce_add_console_app(purr_screenshot PRODUCT_NAME "purr_screenshot" NEEDS_CURL TRUE)
  target_sources(purr_screenshot PRIVATE tools/screenshot.cpp ${PURR_FX_SOURCES})
  target_include_directories(purr_screenshot PRIVATE ${cat_ui_SOURCE_DIR}/include)
  target_compile_definitions(purr_screenshot PUBLIC PURR_FX=1 JUCE_WEB_BROWSER=0 ${PURR_CURL}
    JUCE_PLUGINHOST_VST3=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="AI Purrformer")
  target_link_libraries(purr_screenshot PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_gui_basics juce::juce_events
    PUBLIC juce::juce_recommended_config_flags)
endif()
