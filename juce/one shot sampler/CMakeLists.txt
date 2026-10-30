cmake_minimum_required(VERSION 3.22)

project(OneShotSampler VERSION 0.1.0)

add_subdirectory(JUCE JUCE)

juce_add_plugin(OneShotSampler
    COMPANY_NAME "YourName"
    PLUGIN_MANUFACTURER_CODE YNAM
    PLUGIN_CODE OSSM
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "Modular Beta 2"
    ICON_BIG Source/Assets/modular-beta1-icon.png
    ICON_SMALL Source/Assets/modular-beta1-icon.png
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

target_sources(OneShotSampler
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

juce_generate_juce_header(OneShotSampler)

juce_add_binary_data(OneShotSamplerAssets
    SOURCES
        Source/Assets/modular-top.png
        Source/Assets/modular-wordmark.png
        Source/Assets/fmw-no-bg-dark-gray.png
        Source/Assets/frameworks-halftone-3000.png
)

target_compile_definitions(OneShotSampler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        kAudioAggregateDriftCompensationHighQuality=kAudioSubDeviceDriftCompensationHighQuality
)

target_link_libraries(OneShotSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors
        OneShotSamplerAssets
)
