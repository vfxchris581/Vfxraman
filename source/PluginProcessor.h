// VFXAamps — NAM amp head with a 10-slot morphing gain knob and a dual IR loader.
#pragma once

#include "DualIr.h"
#include "Parameters.h"
#include "vfxa/Dynamics.h"
#include "vfxa/Filters.h"
#include "vfxa/MorphEngine.h"
#include "vfxa/PostFx.h"
#include "vfxa/StudioDelay.h"
#include "vfxa/StudioReverb.h"
#include "vfxa/Tuner.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

class VFXAampsProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    static constexpr int kNumIrs = 2;
    static constexpr int kNumPedals = 2;
    // asset index: 0..9 NAM slots, 10 = IR A, 11 = IR B, 12 / 13 = NAM pedals 1 / 2
    static constexpr int kNumAssets = vfxa::kNumSlots + kNumIrs + kNumPedals;
    static int irAsset (int which) noexcept { return vfxa::kNumSlots + which; }
    static int pedalAsset (int which) noexcept { return vfxa::kNumSlots + kNumIrs + which; }
    static bool isPedalAsset (int asset) noexcept { return asset >= vfxa::kNumSlots + kNumIrs && asset < kNumAssets; }
    /** 0..1: how much of the pedal is in the signal right now (on/off fade) - for its LED. */
    float getPedalLevel (int which) const noexcept { return pedalMeter[static_cast<size_t> (which)].load (std::memory_order_relaxed); }

    VFXAampsProcessor();
    ~VFXAampsProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VFXAamps"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; } // longest typical cab IR

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Assets (message thread). Loads run on a background thread and are swapped in lock-free.
    struct AssetStatus
    {
        juce::String path;      // file in use or wanted (also kept when missing, for relinking)
        juce::String name;      // display name
        juce::String error;     // last load error
        juce::String detail;    // e.g. "WaveNet  48 kHz  Marshall JCM800"
        bool loading = false, missing = false, loaded = false;
    };
    /** fromUser: chosen in the UI (not a session/preset restore). For a model it then sets the slot's IR switch
        from the capture's gear type: off when the capture already includes a cab, on for an amp-only capture. */
    void loadAsset (int asset, const juce::File& file, bool fromUser = false);
    void clearAsset (int asset);
    AssetStatus getAssetStatus (int asset) const;
    bool isAnyAssetLoading() const;
    /** Blocks until all queued loads finish (tests / offline render). */
    bool waitForLoads (int timeoutMs);

    /** Files referenced by the session/preset that could not be found. */
    juce::Array<int> getMissingAssets() const;
    /** Looks for every missing file (by name) under folder; returns how many were found and loaded. */
    int relinkFromFolder (const juce::File& folder);

    //==============================================================================
    // Presets (.vfxapreset files: parameters + file paths, relative to the preset as a fallback).
    static juce::File getPresetFolder();
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    juce::String getCurrentPresetName() const { return presetName; }

    juce::PropertiesFile& getSettings() { return *settings.getUserSettings(); }

    //==============================================================================
    // Telemetry for the UI.
    float getCpuLoad() const noexcept { return static_cast<float> (loadMeasurer.getLoadAsProportion()); }
    int getRunningModels() const noexcept { return engine.runningModels(); }
    float getSlotLevel (int slot) const noexcept { return engine.slotLevel (slot); }
    float getOutputPeak() const noexcept { return outPeak.load (std::memory_order_relaxed); }
    /** True once IR `which` is loaded and active in the convolution (tests / offline use). */
    bool isIrReady (int which) const noexcept { return irs.isReady (which); }

    /** Tuner (message thread): pitch of the guitar input right now. */
    vfxa::TunerReading analyseTuner() { return tuner.analyse(); }
    /** Mute the output while tuning (not saved with the session). */
    void setTunerMute (bool m) noexcept { tunerMute.store (m); }
    bool isTunerMuted() const noexcept { return tunerMute.load(); }
    /** Delay time actually in use, in ms (follows the host tempo when synced). */
    float getDelayTimeMs() const noexcept { return delayTimeMs.load (std::memory_order_relaxed); }
    /** Message thread (normally from the timer): applies slot IR switches set by capture metadata after a load. */
    void applyPendingSlotIrSwitches();
    /** Latency the processor wants to report right now (the host is updated from the timer). */
    int getWantedLatency() const noexcept { return latencyWanted.load (std::memory_order_relaxed); }
    float getInputPeak() const noexcept { return inPeak.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;
    vfxa::MorphEngine& getEngine() noexcept { return engine; }

private:
    template <typename T> void process (juce::AudioBuffer<T>& buffer);
    void processChunk (int n) noexcept;
    void timerCallback() override;
    int computeLatency() const noexcept;
    void applyOversamplingChoice (int choice) noexcept;

    juce::ValueTree saveAssetPaths (const juce::File* relativeTo) const;
    void restoreAssetPaths (const juce::ValueTree& assets, const juce::File* relativeTo);
    juce::File resolve (const juce::String& absPath, const juce::String& relHome, const juce::String& relBase,
                        const juce::File* base, bool isIr) const;
    juce::Array<juce::File> searchFolders() const;
    void setStatus (int asset, const std::function<void (AssetStatus&)>& change);

    vfxa::MorphEngine engine;
    vfxa::DualIr irs;
    vfxa::GraphicEq graphicEq;
    vfxa::StudioDelay delay;
    vfxa::StudioReverb reverb;
    vfxa::Tuner tuner;
    struct Pedal
    {
        vfxa::MorphEngine engine;                // one slot used; handles the capture's sample rate
        std::atomic<bool> hasModel { false };
        vfxa::LinearSmoother mix;                // 0 = bypassed (dry), 1 = on
        vfxa::GainSmoother level;
        vfxa::fxdetail::DelayLine dry;           // dry path delayed to match the engine's latency
        std::vector<float> wet;
        bool running = false;                    // engine runs only with a capture loaded (no added latency otherwise)
        vfxa::LinearSmoother gate;               // brief fade when running starts / stops and the latency changes
        int hold = 0;                            // samples to stay silent after starting (engine history settles)
    };
    std::array<Pedal, kNumPedals> pedals;
    vfxa::MorphEngine::BlockParams pedalParams;  // morph 1 = slot 0 only
    std::array<std::atomic<float>, kNumPedals> pedalMeter {};
    std::atomic<bool> tunerMute { false };
    vfxa::LinearSmoother tunerGate;
    double hostBpm = 120.0;
    std::atomic<float> delayTimeMs { 400.0f };
    vfxa::NoiseGate gate;
    vfxa::Limiter limiterL;
    std::array<vfxa::ToneStack, 3> tone;   // at 1x, 2x, 4x
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> osTone, osLim;

    vfxa::GainSmoother inTrim, outGain;
    vfxa::LinearSmoother gateEnable, limitEnable, switchFade;
    std::array<vfxa::LinearSmoother, 4> toneKnobs;
    vfxa::MorphEngine::BlockParams engineParams;

    juce::AudioBuffer<float> mono, left, right;
    double sampleRateHz = 48000.0;
    int maxBlock = 512;
    int activeOs = 0, wantedOs = 0, toneCountdown = 0;

    // parameter pointers (read on the audio thread)
    std::atomic<float>* pInTrim = nullptr, * pGateOn = nullptr, * pGateThresh = nullptr, * pGateRel = nullptr, * pMorph = nullptr,
                      * pNormalize = nullptr, * pBass = nullptr, * pMid = nullptr, * pTreble = nullptr, * pPresence = nullptr,
                      * pIrOn = nullptr, * pIrBlend = nullptr, * pOut = nullptr, * pLimiter = nullptr, * pOs = nullptr;
    std::array<std::atomic<float>*, vfxa::kNumSlots> pSlotTrim {};
    struct IrParamPtrs { std::atomic<float>* on, * level, * pan, * phase, * delay, * lowcut, * highcut; };
    std::array<IrParamPtrs, kNumIrs> pIr {};
    std::atomic<float>* pEqOn = nullptr, * pDlyOn = nullptr, * pDlyTime = nullptr, * pDlySync = nullptr, * pDlyDiv = nullptr,
                      * pDlyFb = nullptr, * pDlyMix = nullptr, * pDlyTone = nullptr, * pDlyPing = nullptr,
                      * pRvbOn = nullptr, * pRvbSize = nullptr, * pRvbDecay = nullptr, * pRvbPre = nullptr, * pRvbTone = nullptr, * pRvbMix = nullptr,
                      * pDlyMode = nullptr, * pDlyLowCut = nullptr, * pDlyModDepth = nullptr, * pDlyModRate = nullptr, * pDlyDuck = nullptr, * pDlyDiff = nullptr,
                      * pRvbProgram = nullptr, * pRvbDiff = nullptr, * pRvbMod = nullptr, * pRvbLowRT = nullptr, * pRvbHighRT = nullptr,
                      * pRvbEarly = nullptr, * pRvbLowCut = nullptr, * pRvbWidth = nullptr;
    std::array<std::atomic<float>*, vfxa::GraphicEq::kBands> pEqBand {};
    std::array<std::atomic<float>*, vfxa::kNumSlots> pSlotIr {};
    std::array<std::atomic<float>*, kNumPedals> pPedalOn {}, pPedalLevel {};
    std::array<std::atomic<int>, vfxa::kNumSlots> pendingSlotIr {}; // 0 none, 1 set on, 2 set off (message thread applies)

    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<float> outPeak { 0.0f }, inPeak { 0.0f };
    std::atomic<int> latencyWanted { 0 };

    juce::ThreadPool loaderPool { 2 };
    mutable juce::CriticalSection statusLock;
    std::array<AssetStatus, kNumAssets> status;
    std::array<std::atomic<int>, kNumAssets> requestId {};
    juce::String presetName;
    juce::ApplicationProperties settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VFXAampsProcessor)
};
