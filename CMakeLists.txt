cmake_minimum_required(VERSION 3.22)
project(Splint VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Splint
  COMPANY_NAME            "Splint"
  PLUGIN_MANUFACTURER_CODE Splt
  PLUGIN_CODE             Spl1
  FORMATS                 VST3
  PRODUCT_NAME            "Splint"
  IS_SYNTH                TRUE
  NEEDS_MIDI_INPUT        TRUE
  NEEDS_MIDI_OUTPUT       FALSE
  IS_MIDI_EFFECT          FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES         "Instrument" "Sampler"
)

target_sources(Splint PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
)

target_compile_definitions(Splint PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(Splint PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_audio_formats
  juce::juce_dsp
  juce::juce_gui_basics
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
)

option(SPLINT_TEST "Собрать консольный тест" OFF)
if(SPLINT_TEST)
  juce_add_console_app(SplintTest PRODUCT_NAME "SplintTest")
  target_sources(SplintTest PRIVATE tests/Test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(SplintTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Splint" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
  target_link_libraries(SplintTest PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics PUBLIC juce::juce_recommended_config_flags)
endif()
