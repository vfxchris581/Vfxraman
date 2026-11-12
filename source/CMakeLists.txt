# VFXAamps plugin (VST3 / AU / Standalone) and the headless plugin tests.

set(VFXA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VFXA_FORMATS AU)
endif()

juce_add_plugin(VFXAamps
    COMPANY_NAME                "Viridian Audio Works"
    COMPANY_WEBSITE             "https://github.com/vfxchris581/Vfxraman"
    BUNDLE_ID                   "com.viridianaudioworks.vfxaamps.head"
    PRODUCT_NAME                "VFXAamps"
    VERSION                     ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE    Vfxc
    PLUGIN_CODE                 VFXh
    FORMATS                     ${VFXA_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "VFXAamps uses your audio interface input for your guitar.")

juce_add_binary_data(VFXAampsAssets
    HEADER_NAME VFXAampsAssets.h
    NAMESPACE   VFXAampsAssets
    SOURCES
        ../resources/logo-tree.png
        ../resources/logo-wordmark.png)

set(VFXA_PLUGIN_SOURCES
    PluginProcessor.cpp
    PluginEditor.cpp
    DualIr.cpp
    ui/AmpLookAndFeel.cpp
    ui/AmpComponents.cpp
    ui/Rack.cpp)

target_sources(VFXAamps PRIVATE ${VFXA_PLUGIN_SOURCES})
target_include_directories(VFXAamps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(VFXA_JUCE_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_compile_definitions(VFXAamps PUBLIC ${VFXA_JUCE_DEFS})
target_link_libraries(VFXAamps
    PRIVATE
        vfxa_dsp
        VFXAampsAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless tests: the processor + editor compiled into a console app (no plugin wrapper).
if(VFXA_BUILD_TESTS)
    juce_add_console_app(VFXAampsPluginTests PRODUCT_NAME "VFXAampsPluginTests")
    target_sources(VFXAampsPluginTests PRIVATE ${VFXA_PLUGIN_SOURCES} ../tests/plugin_tests.cpp)
    target_include_directories(VFXAampsPluginTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(VFXAampsPluginTests PRIVATE ${VFXA_JUCE_DEFS} ${VFXA_EXTRA_TEST_DEFS}
        VFXA_EXAMPLE_MODELS="${VFXA_NAM_DIR}/example_models"
        JucePlugin_Name="VFXAamps")
    target_link_libraries(VFXAampsPluginTests PRIVATE
        vfxa_dsp VFXAampsAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME plugin_tests COMMAND VFXAampsPluginTests)
endif()
