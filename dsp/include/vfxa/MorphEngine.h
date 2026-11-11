// VFXAamps — 10-slot NAM morph engine.
//
// Runs Neural Amp Modeler captures at their trained rate (the engine works at 48 kHz, the NAM
// standard; captures trained at another rate get their own resampling adaptor with latency
// compensation so every slot stays sample-aligned). The 1..10 knob blends the two loaded slots
// around its position with an equal-power crossfade, per sample.
//
// Real-time design
//   * Models are loaded and prepared on a background thread (loadSlotModel) and handed over with
//     setSlot(); the audio thread picks them up with an atomic exchange.
//   * Only the 2-4 models that matter run: the two being blended plus one loaded neighbour on each
//     side ("warm", output discarded) so their internal state is settled before they fade in.
//   * A model that becomes warm is first caught up on the last ~85 ms of input (kept in a history
//     ring), so even a slot that was idle starts with exactly the right state - no clicks.
//   * Loading/clearing a slot and large knob jumps crossfade between the old and new configuration
//     (40 ms, equal power) instead of sweeping through every slot in between.
//   * The audio thread never allocates, locks or frees; retired models are freed by collectGarbage().
//
// This header is JUCE- and NAM-free; the implementation is compiled as C++20 with NAM core.
#pragma once

#include "vfxa/MorphMap.h"
#include "vfxa/SincResampler.h"
#include "vfxa/Smoothers.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vfxa
{

struct ModelInfo
{
    std::string path, name, modeledBy, gearMake, gearModel, gearType, toneType, architecture, version;
    double sampleRate = -1.0;   // training rate (-1 = not stated, treated as 48 kHz)
    double loudnessDb = 0.0;
    bool hasLoudness = false;
    int receptiveField = -1;    // samples of history the network depends on (-1 = unbounded/unknown, e.g. LSTM)

    std::string gearName() const { return gearMake.empty() ? gearModel : (gearModel.empty() ? gearMake : gearMake + " " + gearModel); }
    std::string displayName() const { return name; }
};

/** Metadata only (fast). Returns false and sets error on failure. Non-real-time. */
bool readModelInfo (const std::string& path, ModelInfo& info, std::string& error);

class SlotModel; // opaque: NAM network + rate adaptor + buffers
void destroySlotModel (SlotModel*) noexcept;
using SlotModelPtr = std::unique_ptr<SlotModel, void (*) (SlotModel*)>;

struct ModelLoad
{
    SlotModelPtr model { nullptr, destroySlotModel };
    ModelInfo info;
    std::string error; // empty on success
};

class MorphEngine
{
public:
    static constexpr double kEngineRate = 48000.0;
    static constexpr int kChunk = 512;          // NAM block size inside the engine
    static constexpr int kMaxCatchUp = 16384;   // longest history a model is caught up on before it is heard
    static constexpr int kDefaultCatchUp = 8192; // for recurrent (LSTM) models: ~170 ms settles their state
    static constexpr int kRing = 32768;         // per-model output / input history ring (power of two)

    struct BlockParams
    {
        double morph = 1.0;                       // 1..10
        std::array<float, kNumSlots> slotTrimDb {};
        bool normalize = true;                    // loudness-normalise captures to -18 dBFS
    };

    MorphEngine();
    ~MorphEngine();

    /** Non-real-time (audio stopped). */
    void prepare (double hostSampleRate, int maxHostBlock);
    void reset();

    /** Non-real-time: load and prepare a model for this engine (any thread except audio). */
    static ModelLoad loadModel (const std::string& path);

    /** Non-real-time: hand a loaded model to slot (0-based). A load with a null model clears the slot. */
    void setSlot (int slot, ModelLoad&& load);
    void clearSlot (int slot);
    /** Non-real-time: frees models the audio thread has finished with (call from a timer). */
    void collectGarbage();

    /** Audio thread: mono in -> mono out (in may equal out). */
    void process (const float* in, float* out, int numSamples, const BlockParams& params) noexcept;

    // ---- information for the UI / host (any thread) ---------------------------------
    int latencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }
    bool slotHasModel (int slot) const;
    ModelInfo slotInfo (int slot) const;
    std::string slotPath (int slot) const;
    /** Audible mix gain of each slot (0..1) at the end of the last block - for the glow indicator. */
    float slotLevel (int slot) const noexcept { return levels[static_cast<size_t> (slot)].load (std::memory_order_relaxed); }
    /** Bitmask of slots whose model ran in the last block, and how many model instances ran. */
    SlotMask runningMask() const noexcept { return running.load (std::memory_order_relaxed); }
    int runningModels() const noexcept { return runningCount.load (std::memory_order_relaxed); }
    int catchUpsDone() const noexcept { return catchUps.load (std::memory_order_relaxed); }

    /** Test hook: when set, records per engine-rate sample the gain applied to every source
        (one entry per model instance, so a crossfade from one configuration to another is traced per model). */
    struct TraceEntry { const void* model = nullptr; int slot = -1; float gain = 0.0f; float weight = 0.0f; }; // model == nullptr: dry input
    struct GainTrace { std::vector<std::array<TraceEntry, 2 * kNumSlots + 1>> gains; };
    void setGainTrace (GainTrace* t) noexcept { trace = t; }

private:
    struct Slot
    {
        SlotModel* cur = nullptr;   // model of the current configuration (audio thread)
        SlotModel* old = nullptr;   // model of the previous configuration while fading
        bool changed = false;       // slot differs between old and new configuration
        std::atomic<SlotModel*> pending { nullptr };
        LinearSmoother trim;
    };

    void domainProcess (const float* in, float* out, int m, const BlockParams& params) noexcept;
    void pickUpChanges (const BlockParams& params) noexcept;
    void finishFade() noexcept;
    void updateWarm (double posA, double posB) noexcept;
    double mixSample (SlotMask mask, int compLatency, bool useOld, double pos, int i, const double* trims, double weight) const noexcept;
    bool retire (SlotModel* m) noexcept;
    SlotMask maskOf (bool old) const noexcept;
    int compensationFor (SlotMask mask, bool old) const noexcept;

    std::array<Slot, kNumSlots> slots;
    ResamplingPair hostPair;
    void runRange (SlotModel& model, std::int64_t start, int count) noexcept;
    SlotModel* modelFor (int slot, bool old) const noexcept;

    std::vector<float> inputRing;             // domain-rate input history, indexed by clock
    std::vector<float> scratchIn, scratchOut;
    std::int64_t clock = 0;                   // domain sample index at the start of the block
    LinearSmoother morph, normAmount;
    bool firstBlock = true;

    // configuration crossfade
    bool fading = false;
    double fade = 0.0, fadeStep = 0.0, oldPos = 1.0, newPos = 1.0;
    SlotMask oldMask = 0, newMask = 0;
    int oldComp = 0, newComp = 0;

    static constexpr int kRetired = 64;
    std::array<std::atomic<SlotModel*>, kRetired> retired {};

    // non-real-time bookkeeping (never touched by the audio thread)
    mutable std::mutex infoLock;
    std::array<ModelInfo, kNumSlots> shownInfo;
    std::array<bool, kNumSlots> shownLoaded {};

    std::atomic<int> latency { 0 }, runningCount { 0 }, catchUps { 0 };
    std::atomic<SlotMask> running { 0 };
    std::array<std::atomic<float>, kNumSlots> levels {};
    double hostRate = 48000.0;
    GainTrace* trace = nullptr;
    std::array<TraceEntry, 2 * kNumSlots + 1>* traceRow = nullptr;
    void traceAdd (const void* model, int slot, double gain, double weight) const noexcept;
};

} // namespace vfxa
