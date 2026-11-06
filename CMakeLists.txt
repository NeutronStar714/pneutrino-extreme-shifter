cmake_minimum_required(VERSION 3.22)

project(PolyShifter VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PNEUTRINO_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE"
    CACHE PATH "Path to the pinned JUCE source checkout")
if(NOT EXISTS "${PNEUTRINO_JUCE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE is missing. Run git submodule update --init --recursive, "
        "or set PNEUTRINO_JUCE_SOURCE_DIR to an existing matching checkout.")
endif()
add_subdirectory("${PNEUTRINO_JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

# 2. Define the plugin target
juce_add_plugin(PolyShifter
    COMPANY_NAME "NeutronStar714"
    PLUGIN_MANUFACTURER_CODE NtrS
    PLUGIN_CODE Shft
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Extreme Shifter"
    BUNDLE_ID "com.yourcompany.PolyShifter"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANT_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

# 3. Add your source files
target_sources(PolyShifter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

# 4. Compile definitions (same set as Spacer - we never ship a VST2, so
#    the VST3 replace-VST2 machinery and its warnings are disabled)
target_compile_definitions(PolyShifter
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# 5. Link JUCE modules
target_link_libraries(PolyShifter PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)

# 5. Include the path to Signalsmith Stretch headers (signalsmith-stretch.h
#    includes "signalsmith-linear/stft.h", which lives in libs/signalsmith-linear/)
target_include_directories(PolyShifter PRIVATE
    libs/
)

# 6. Apply mandatory compile properties
juce_generate_juce_header(PolyShifter)
