// VFXAamps — NAM amp head with a 10-slot morphing gain knob and a dual IR loader.
#pragma once

#include "DualIr.h"
#include "License.h"
#include "Parameters.h"
#include "vfxa/Dynamics.h"
#include "vfxa/Filters.h"
#include "vfxa/MorphEngine.h"

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
    static constexpr int kNumAssets = vfxa::kNumSlots + kNumIrs; // asset index: 0..9 NAM slots, 10 = IR A, 11 = IR B
    static int irAsset (int which) noexcept { return vfxa::kNumSlots + which; }

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
    void loadAsset (int asset, const juce::File& file);
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

    /** License / trial state, shared by all instances (message thread). */
    vfxa::LicenseManager& getLicense() noexcept { return *license; }
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
    vfxa::NoiseGate gate;
    vfxa::Limiter limiterL;
    std::array<vfxa::ToneStack, 3> tone;   // at 1x, 2x, 4x
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> osTone, osLim;

    vfxa::GainSmoother inTrim, outGain;
    vfxa::LinearSmoother gateEnable, limitEnable, switchFade, licenseGate;
    juce::SharedResourcePointer<vfxa::LicenseManager> license;
    int licenseRefreshTicks = 0;
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
