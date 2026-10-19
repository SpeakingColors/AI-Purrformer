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

set(PURR_SOURCES src/PluginProcessor.cpp src/PluginEditor.cpp src/EngineClient.cpp)

juce_add_plugin(AIPurrformer
  COMPANY_NAME "Studio 87"
  PRODUCT_NAME "AI Purrformer"
  PLUGIN_MANUFACTURER_CODE Spkc
  PLUGIN_CODE Purr
  FORMATS VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Instrument Synth
  NEEDS_CURL TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(AIPurrformer PRIVATE ${PURR_SOURCES})
if(UNIX AND NOT APPLE)
  set(PURR_CURL JUCE_USE_CURL=1)   # Windows and macOS use their native HTTP stacks
else()
  set(PURR_CURL JUCE_USE_CURL=0)
endif()

target_compile_definitions(AIPurrformer PUBLIC
  JUCE_WEB_BROWSER=0 ${PURR_CURL} JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(AIPurrformer
  PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_gui_basics juce::juce_events
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Headless self test: needs the engine running on 127.0.0.1:8765.
juce_add_console_app(purr_selftest PRODUCT_NAME "purr_selftest" NEEDS_CURL TRUE)
target_sources(purr_selftest PRIVATE tests/selftest.cpp ${PURR_SOURCES})
target_compile_definitions(purr_selftest PUBLIC JUCE_WEB_BROWSER=0 ${PURR_CURL}
  JUCE_PLUGINHOST_VST3=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="AI Purrformer")
target_link_libraries(purr_selftest PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_gui_basics juce::juce_events
  PUBLIC juce::juce_recommended_config_flags)
