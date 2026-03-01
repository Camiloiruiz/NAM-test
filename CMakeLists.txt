cmake_minimum_required(VERSION 3.22)
project(GuitarAmpPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Submodules ──────────────────────────────────────────────────────────────
# Clone submodules before configuring:
#   git submodule add https://github.com/juce-framework/JUCE Modules/JUCE
#   git submodule add https://github.com/sdatkinson/NeuralAmpModelerCore Modules/NeuralAmpModelerCore
#   git submodule update --init --recursive

add_subdirectory(Modules/JUCE)
# NeuralAmpModelerCore has no usable CMake library target; sources are added manually below.

# ─── JUCE plugin target ───────────────────────────────────────────────────────
juce_add_plugin(GuitarAmpPlugin
    COMPANY_NAME         "YourCompany"
    PLUGIN_MANUFACTURER_CODE "YrCo"
    PLUGIN_CODE          "GtAp"
    FORMATS              VST3 AU Standalone
    PRODUCT_NAME         "Guitar Amp Plugin"
    IS_SYNTH             FALSE
    NEEDS_MIDI_INPUT     FALSE
    NEEDS_MIDI_OUTPUT    FALSE
    IS_MIDI_EFFECT       FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

# Generate JuceHeader.h (required by all Source files via #include <JuceHeader.h>)
juce_generate_juce_header(GuitarAmpPlugin)

# ─── NAM Core source files ────────────────────────────────────────────────────
set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Modules/NeuralAmpModelerCore")

set(NAM_SOURCES
    "${NAM_CORE_DIR}/NAM/dsp.cpp"
    "${NAM_CORE_DIR}/NAM/get_dsp.cpp"
    "${NAM_CORE_DIR}/NAM/lstm.cpp"
    "${NAM_CORE_DIR}/NAM/wavenet.cpp"
    "${NAM_CORE_DIR}/NAM/convnet.cpp"
    "${NAM_CORE_DIR}/NAM/activations.cpp"
)

# ─── Plugin sources ───────────────────────────────────────────────────────────
target_sources(GuitarAmpPlugin
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/NamEngine.cpp
        Source/NamEngine.h
        Source/NoiseGate.cpp
        Source/NoiseGate.h
        ${NAM_SOURCES}
)

# ─── Compile definitions ──────────────────────────────────────────────────────
target_compile_definitions(GuitarAmpPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Eigen compatibility – remove later for performance
        EIGEN_MAX_ALIGN_BYTES=0
        EIGEN_DONT_VECTORIZE=1
        # Expose plugin formats to code
        JucePlugin_Build_VST3=1
        JucePlugin_Build_AU=1
        JucePlugin_Build_Standalone=1
)

# ─── Include directories ──────────────────────────────────────────────────────
target_include_directories(GuitarAmpPlugin
    PRIVATE
        Source
        # JuceHeader.h is generated here; juce_generate_juce_header adds it via
        # INTERFACE which doesn't cover the target's own sources, so add it explicitly.
        "${CMAKE_CURRENT_BINARY_DIR}/GuitarAmpPlugin_artefacts/JuceLibraryCode"
        "${NAM_CORE_DIR}"
        "${NAM_CORE_DIR}/NAM"
        "${NAM_CORE_DIR}/Dependencies/eigen"    # Eigen bundled with NAMCore
        "${NAM_CORE_DIR}/Dependencies/nlohmann" # nlohmann/json.hpp bundled with NAMCore
)

# ─── JUCE module links ────────────────────────────────────────────────────────
target_link_libraries(GuitarAmpPlugin
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ─── Platform-specific ───────────────────────────────────────────────────────
if(APPLE)
    set_target_properties(GuitarAmpPlugin PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
endif()

if(MSVC)
    # NAMCore uses GCC's __restrict__ keyword; map it to MSVC's __restrict.
    target_compile_definitions(GuitarAmpPlugin PRIVATE __restrict__=__restrict)
    # Ensure source files are compiled as UTF-8 so Unicode string literals
    # (e.g. \u2713 checkmark in PluginEditor.cpp) are handled correctly.
    target_compile_options(GuitarAmpPlugin PRIVATE /utf-8)
endif()
