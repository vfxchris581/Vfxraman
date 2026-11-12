// VFXAamps — parameter IDs and layout.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vfxa::params
{

inline constexpr int kVersion = 1;

inline const juce::String inTrim     = "inTrim";
inline const juce::String gateOn     = "gateOn";
inline const juce::String gateThresh = "gateThresh";
inline const juce::String gateRel    = "gateRelease";
inline const juce::String morph      = "morph";
inline const juce::String normalize  = "normalize";
inline const juce::String bass       = "bass";
inline const juce::String mid        = "mid";
inline const juce::String treble     = "treble";
inline const juce::String presence   = "presence";
inline const juce::String irOn       = "irOn";
inline const juce::String irBlend    = "irBlend";
inline const juce::String outLevel   = "outLevel";
inline const juce::String limiter    = "limiter";
inline const juce::String oversample = "oversampling";

inline juce::String slotTrim (int slot) { return "slotTrim" + juce::String (slot + 1); }
/** Per-slot cab switch (parameter version 3): use the dual IR while this slot is playing. */
inline juce::String slotIr (int slot) { return "slotIr" + juce::String (slot + 1); }

// Effects rack (added in parameter version 2)
inline const juce::String eqOn        = "eqOn";
inline juce::String eqBand (int b) { return "eqBand" + juce::String (b + 1); }
inline const juce::String dlyOn       = "delayOn";
inline const juce::String dlyTime     = "delayTime";
inline const juce::String dlySync     = "delaySync";
inline const juce::String dlyDiv      = "delayDivision";
inline const juce::String dlyFeedback = "delayFeedback";
inline const juce::String dlyMix      = "delayMix";
inline const juce::String dlyTone     = "delayTone";
inline const juce::String dlyPing     = "delayPingPong";
inline const juce::String rvbOn       = "reverbOn";
inline const juce::String rvbSize     = "reverbSize";
inline const juce::String rvbDecay    = "reverbDecay";
inline const juce::String rvbPre      = "reverbPreDelay";
inline const juce::String rvbTone     = "reverbTone";
inline const juce::String rvbMix      = "reverbMix";

/** Delay note divisions (for tempo sync) and their length in beats. */
inline const juce::StringArray& delayDivisionNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16" };
    return names;
}
inline double delayDivisionBeats (int index)
{
    static const double beats[] { 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25 };
    return beats[juce::jlimit (0, 8, index)];
}
inline const char* eqBandLabel (int b)
{
    static const char* labels[] { "31", "62", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
    return labels[juce::jlimit (0, 9, b)];
}

/** Per-IR parameter: ir (0 = A, 1 = B) + name, e.g. "irA_level". */
inline juce::String ir (int which, const char* name) { return juce::String ("ir") + (which == 0 ? "A_" : "B_") + name; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pid = [] (const String& id) { return ParameterID { id, kVersion }; };
    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    auto knob = [] (float v, int) { return String (v, 1); };
    auto ms = [] (float v, int) { return String (v, v < 10.0f ? 2 : 0) + " ms"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };

    auto dbAttr = AudioParameterFloatAttributes().withStringFromValueFunction (db).withLabel ("dB");
    auto knobAttr = AudioParameterFloatAttributes().withStringFromValueFunction (knob);

    layout.add (std::make_unique<AudioParameterFloat> (pid (inTrim), "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr));
    layout.add (std::make_unique<AudioParameterBool> (pid (gateOn), "Gate", false));
    layout.add (std::make_unique<AudioParameterFloat> (pid (gateThresh), "Gate Threshold", NormalisableRange<float> (-90.0f, -20.0f, 0.1f), -65.0f, dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (pid (gateRel), "Gate Release",
        NormalisableRange<float> (5.0f, 500.0f, 0.1f, 0.4f), 80.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));

    layout.add (std::make_unique<AudioParameterFloat> (pid (morph), "Gain",
        NormalisableRange<float> (1.0f, 10.0f, 0.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
    layout.add (std::make_unique<AudioParameterBool> (pid (normalize), "Normalize Models", true));
    for (int k = 0; k < 10; ++k)
        layout.add (std::make_unique<AudioParameterFloat> (pid (slotTrim (k)), "Slot " + String (k + 1) + " Level",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr));

    for (auto* id : { &bass, &mid, &treble, &presence })
        layout.add (std::make_unique<AudioParameterFloat> (pid (*id), id->substring (0, 1).toUpperCase() + id->substring (1),
                                                           NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f, knobAttr));

    layout.add (std::make_unique<AudioParameterBool> (pid (irOn), "IR", true));
    layout.add (std::make_unique<AudioParameterFloat> (pid (irBlend), "IR Blend", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return "A " + String (roundToInt ((1.0f - v) * 100.0f)) + " / B " + String (roundToInt (v * 100.0f)); })));
    for (int w = 0; w < 2; ++w)
    {
        const String n = w == 0 ? "IR A " : "IR B ";
        layout.add (std::make_unique<AudioParameterBool> (pid (ir (w, "on")), n + "On", true));
        layout.add (std::make_unique<AudioParameterFloat> (pid (ir (w, "level")), n + "Level", NormalisableRange<float> (-36.0f, 12.0f, 0.01f), 0.0f, dbAttr));
        layout.add (std::make_unique<AudioParameterFloat> (pid (ir (w, "pan")), n + "Pan", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                return std::abs (v) < 0.005f ? String ("C") : (v < 0 ? "L" : "R") + String (roundToInt (std::abs (v) * 100.0f)); })));
        layout.add (std::make_unique<AudioParameterBool> (pid (ir (w, "phase")), n + "Phase Invert", false));
        layout.add (std::make_unique<AudioParameterFloat> (pid (ir (w, "delay")), n + "Delay",
            NormalisableRange<float> (0.0f, 2.0f, 0.001f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
        layout.add (std::make_unique<AudioParameterFloat> (pid (ir (w, "lowcut")), n + "Low Cut",
            NormalisableRange<float> (20.0f, 500.0f, 0.1f, 0.4f), 20.0f, AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
        layout.add (std::make_unique<AudioParameterFloat> (pid (ir (w, "highcut")), n + "High Cut",
            NormalisableRange<float> (2000.0f, 20000.0f, 1.0f, 0.4f), 20000.0f, AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    }

    layout.add (std::make_unique<AudioParameterFloat> (pid (outLevel), "Output", NormalisableRange<float> (-36.0f, 12.0f, 0.01f), 0.0f, dbAttr));
    layout.add (std::make_unique<AudioParameterBool> (pid (limiter), "Limiter", true));
    layout.add (std::make_unique<AudioParameterChoice> (pid (oversample), "Oversampling", StringArray { "Off", "2x", "4x" }, 0));

    // --- effects rack ----------------------------------------------------------------------
    auto pid2 = [] (const String& id) { return ParameterID { id, 2 }; };
    auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction (pct);
    auto msAttr = AudioParameterFloatAttributes().withStringFromValueFunction (ms);
    auto hzAttr = AudioParameterFloatAttributes().withStringFromValueFunction (hz);

    layout.add (std::make_unique<AudioParameterBool> (pid2 (eqOn), "EQ", false));
    for (int b = 0; b < 10; ++b)
        layout.add (std::make_unique<AudioParameterFloat> (pid2 (eqBand (b)), String ("EQ ") + eqBandLabel (b) + (b < 5 ? " Hz" : "Hz"),
                                                           NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterBool> (pid2 (dlyOn), "Delay", false));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (dlyTime), "Delay Time", NormalisableRange<float> (1.0f, 2500.0f, 0.1f, 0.45f), 400.0f, msAttr));
    layout.add (std::make_unique<AudioParameterBool> (pid2 (dlySync), "Delay Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (pid2 (dlyDiv), "Delay Division", delayDivisionNames(), 3));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (dlyFeedback), "Delay Feedback", NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.35f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (dlyMix), "Delay Mix", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (dlyTone), "Delay Tone", NormalisableRange<float> (800.0f, 20000.0f, 1.0f, 0.35f), 6000.0f, hzAttr));
    layout.add (std::make_unique<AudioParameterBool> (pid2 (dlyPing), "Delay Ping-Pong", false));

    layout.add (std::make_unique<AudioParameterBool> (pid2 (rvbOn), "Reverb", false));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (rvbSize), "Reverb Size", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (rvbDecay), "Reverb Decay", NormalisableRange<float> (0.2f, 20.0f, 0.01f, 0.4f), 2.2f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " s"; })));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (rvbPre), "Reverb Pre-Delay", NormalisableRange<float> (0.0f, 250.0f, 0.1f, 0.5f), 20.0f, msAttr));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (rvbTone), "Reverb Tone", NormalisableRange<float> (1000.0f, 18000.0f, 1.0f, 0.4f), 7000.0f, hzAttr));
    layout.add (std::make_unique<AudioParameterFloat> (pid2 (rvbMix), "Reverb Mix", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f, pctAttr));

    for (int k = 0; k < 10; ++k)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { slotIr (k), 3 }, "Slot " + String (k + 1) + " IR", true));
    return layout;
}

} // namespace vfxa::params
