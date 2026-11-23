cmake_minimum_required(VERSION 3.22)

project(OneShotSampler VERSION 0.1.0)

add_subdirectory(JUCE JUCE)

juce_add_plugin(OneShotSampler
    COMPANY_NAME "YourName"
    PLUGIN_MANUFACTURER_CODE YNAM
    PLUGIN_CODE OSSM
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "Modular Beta 2"
    ICON_BIG Source/Assets/modular-blue-icon.png
    ICON_SMALL Source/Assets/modular-blue-icon.png
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
        Source/Assets/modular-wordmark.png
)

target_compile_definitions(OneShotSampler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# Local-build workaround for an older JUCE vs newer macOS SDK. The GitHub build
# uses a current JUCE and passes -DMODULAR_SKIP_DRIFT_FIX=ON, because with a
# current JUCE this define makes the SDK symbol get defined twice.
if(NOT MODULAR_SKIP_DRIFT_FIX)
    target_compile_definitions(OneShotSampler
        PUBLIC
            kAudioAggregateDriftCompensationHighQuality=kAudioSubDeviceDriftCompensationHighQuality
    )
endif()

target_link_libraries(OneShotSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors
        OneShotSamplerAssets
)

option(MODULAR_BUILD_TESTS "Build the Modular audio regression executable" OFF)
if(MODULAR_BUILD_TESTS)
    enable_testing()
    add_executable(ModularAudioRegression "../../tests/audio-regression.cpp")
    target_compile_features(ModularAudioRegression PRIVATE cxx_std_17)
    target_include_directories(ModularAudioRegression PRIVATE
        Source
        $<TARGET_PROPERTY:OneShotSampler,INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(ModularAudioRegression PRIVATE
        $<TARGET_PROPERTY:OneShotSampler,COMPILE_DEFINITIONS>
    )
    target_link_libraries(ModularAudioRegression PRIVATE OneShotSampler)
    add_test(NAME ModularAudioRegression COMMAND ModularAudioRegression)
endif()
