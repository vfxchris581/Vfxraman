// VFXAamps — dual IR loader (A + B), zero-latency partitioned convolution.
#pragma once

#include "vfxa/Filters.h"
#include "vfxa/Smoothers.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <mutex>

namespace vfxa
{

struct IrParams
{
    bool on = true, phaseInvert = false;
    float levelDb = 0.0f, pan = 0.0f, delayMs = 0.0f, lowCutHz = 20.0f, highCutHz = 20000.0f;
};

/** One cab IR: convolution (wait-free IR swaps with JUCE's 50 ms crossfade) -> fractional delay
    -> polarity -> low/high cut -> level -> pan. Mono in, adds into a stereo out. */
class IrChannel
{
public:
    IrChannel();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Non-real-time (any thread): hand a decoded IR to the convolution engine. */
    void load (juce::AudioBuffer<float>&& ir, double irSampleRate, const juce::String& path);
    void clear();
    bool isLoaded() const noexcept { return loaded.load(); }
    /** Audio thread: loaded and the convolution has actually switched to *our* IR. JUCE builds IR engines in
        the background and until then runs its own placeholder (a 1-sample impulse at 44.1 kHz, resampled and
        scaled), so "size > 0" is not enough: we wait for the engine whose length matches the IR we gave it. */
    bool isReady() const noexcept
    {
        if (! loaded.load (std::memory_order_relaxed)) return false;
        const int want = expectedIrSize.load (std::memory_order_relaxed);
        return want > 0 && conv.getCurrentIRSize() == want;
    }
    juce::String getPath() const;

    /** Audio thread. weight = blend weight for this IR (0..1, smoothed by the caller per block). */
    void process (const float* in, float* outL, float* outR, int n, const IrParams& p, double weight) noexcept;

    /** Decodes an audio file into a buffer (non-real-time). Returns an error message or empty. */
    static juce::String readIrFile (const juce::File& f, juce::AudioBuffer<float>& out, double& sampleRate);

private:
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 256 } };
    juce::AudioBuffer<float> work;
    std::atomic<bool> loaded { false };
    std::atomic<int> irLength { 0 };          // samples, at irRate
    std::atomic<double> irRateHz { 0.0 };
    std::atomic<int> expectedIrSize { 0 };    // length JUCE's engine will report at the prepared rate
    static int resampledLength (int len, double from, double to) noexcept;
    mutable std::mutex loadLock;         // serialises loads from several non-real-time threads
    juce::String path;                   // guarded by loadLock

    static constexpr int kDelaySize = 1024; // > 2 ms at 384 kHz + interpolation taps
    float delayL[kDelaySize] {}, delayR[kDelaySize] {};
    int delayWrite = 0;
    CutFilters cutL, cutR;
    LinearSmoother gain, panPos, polarity, delayFade, lowCutLog, highCutLog, lowOn, highOn;
    double delayFrom = 0.0, delayTo = 0.0;
    double sampleRate = 48000.0;
    int idleSamples = 0, coefCountdown = 0;
    bool needsReset = true;

    double readDelay (const float* buf, double d) const noexcept;
};

/** IR A + IR B with an A/B blend and a global on/off (crossfaded against the dry amp signal). */
class DualIr
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    IrChannel& channel (int which) noexcept { return which == 0 ? a : b; }

    /** Audio thread: mono in -> stereo out. */
    bool isReady (int which) const noexcept { return which == 0 ? a.isReady() : b.isReady(); }

    void process (const float* in, float* outL, float* outR, int n,
                  bool globalOn, float blend, const IrParams& pa, const IrParams& pb) noexcept;

private:
    IrChannel a, b;
    LinearSmoother wetMix, weightA, weightB;
    bool first = true;
};

} // namespace vfxa
