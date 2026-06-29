cmake_minimum_required(VERSION 3.22)
project(VocalCleaner VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("C:/Users/nvhstudio/JUCE" JUCE)

# ---- RNNoise (Xiph) compiled as a static lib, scalar path (no x86 RTCD) ----
set(RN "C:/Users/nvhstudio/rnnoise")
add_library(rnnoise STATIC
    ${RN}/src/denoise.c
    ${RN}/src/rnn.c
    ${RN}/src/pitch.c
    ${RN}/src/kiss_fft.c
    ${RN}/src/celt_lpc.c
    ${RN}/src/nnet.c
    ${RN}/src/nnet_default.c
    ${RN}/src/parse_lpcnet_weights.c
    ${RN}/src/rnnoise_data.c
    ${RN}/src/rnnoise_tables.c)
target_include_directories(rnnoise PUBLIC ${RN}/include PRIVATE ${RN}/src)
target_compile_definitions(rnnoise PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
if (MSVC)
    target_compile_options(rnnoise PRIVATE /wd4244 /wd4267 /wd4305 /wd4101 /wd4018 /wd4146 /wd4334)
endif()

juce_add_plugin(VocalCleaner
    COMPANY_NAME            "nvhstudio"
    PRODUCT_NAME            "nvh the noise"
    PLUGIN_MANUFACTURER_CODE Nvhs
    PLUGIN_CODE             Vcl1
    FORMATS                 VST3
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES         "Fx" "Restoration")

target_sources(VocalCleaner PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(VocalCleaner PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VocalCleaner
    PRIVATE
        juce::juce_audio_utils
        rnnoise
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
