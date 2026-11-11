// VFXAamps — 10-slot NAM morph engine (compiled as C++20 together with NeuralAmpModelerCore).

#include "vfxa/MorphEngine.h"

#include "NAM/container.h"
#include "NAM/convnet.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/linear.h"
#include "NAM/lstm.h"
#include "NAM/sequential.h"
#include "NAM/wavenet/model.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace vfxa
{

//==============================================================================
// NAM registers each architecture from a static object in that architecture's source
// file; inside a static library the linker would drop those files. Referencing one
// function from each keeps WaveNet, LSTM, ConvNet, Linear, Sequential and Container linked.
using ConfigFactory = std::unique_ptr<::nam::ModelConfig> (*) (const nlohmann::json&, double);
static ConfigFactory const volatile keepArchitecturesLinked[] = {
    &::nam::wavenet::create_config, &::nam::lstm::create_config,       &::nam::convnet::create_config,
    &::nam::linear::create_config,  &::nam::sequential::create_config, &::nam::container::create_config,
};

static int touchArchitectures() noexcept
{
    int n = 0;
    for (auto f : keepArchitecturesLinked) n += f != nullptr ? 1 : 0;
    return n;
}

//==============================================================================
/** Samples of input history a network's output depends on; -1 when unbounded (recurrent) or unknown. */
static long receptiveFieldOf (const nlohmann::json& root, int depth = 0)
{
    if (depth > 8 || ! root.is_object()) return -1;
    const auto arch = root.value ("architecture", std::string());
    const auto& c = root.contains ("config") ? root["config"] : nlohmann::json::object();
    auto kernelAt = [] (const nlohmann::json& layer, size_t i) -> long
    {
        if (layer.contains ("kernel_sizes") && layer["kernel_sizes"].is_array() && i < layer["kernel_sizes"].size())
            return layer["kernel_sizes"][i].get<long>();
        if (layer.contains ("kernel_size") && layer["kernel_size"].is_number()) return layer["kernel_size"].get<long>();
        return 3;
    };
    if (arch == "WaveNet")
    {
        long rf = 1;
        if (! c.contains ("layers") || ! c["layers"].is_array()) return -1;
        for (const auto& layer : c["layers"])
        {
            if (! layer.contains ("dilations") || ! layer["dilations"].is_array()) return -1;
            for (size_t i = 0; i < layer["dilations"].size(); ++i) rf += layer["dilations"][i].get<long>() * (kernelAt (layer, i) - 1);
            if (layer.contains ("head") && layer["head"].is_object() && layer["head"].contains ("kernel_size"))
                rf += layer["head"]["kernel_size"].get<long>() - 1;
            if (layer.contains ("condition_dsp")) return -1;
        }
        if (c.contains ("condition_dsp")) return -1;
        return rf;
    }
    if (arch == "ConvNet")
    {
        long rf = 1;
        if (! c.contains ("dilations") || ! c["dilations"].is_array()) return -1;
        for (const auto& d : c["dilations"]) rf += d.get<long>();
        return rf;
    }
    if (arch == "Linear") return c.value ("receptive_field", -1L);
    if (arch == "SlimmableContainer" || arch == "Container")
    {
        long rf = 1;
        if (! c.contains ("submodels") || ! c["submodels"].is_array()) return -1;
        for (const auto& sub : c["submodels"])
        {
            const long r = receptiveFieldOf (sub.contains ("model") ? sub["model"] : sub, depth + 1);
            if (r < 0) return -1;
            rf = std::max (rf, r);
        }
        return rf;
    }
    return -1; // LSTM and anything new: catch up on a generous fixed history
}

bool readModelInfo (const std::string& path, ModelInfo& info, std::string& error)
{
    try
    {
        std::ifstream f { std::filesystem::path (path) };
        if (! f) { error = "cannot open file"; return false; }
        const auto root = nlohmann::json::parse (f);
        if (! root.is_object() || ! root.contains ("architecture")) { error = "not a NAM model"; return false; }

        info = ModelInfo();
        info.path = path;
        info.architecture = root.value ("architecture", std::string());
        if (root.contains ("version") && root["version"].is_string()) info.version = root["version"].get<std::string>();
        if (root.contains ("sample_rate") && root["sample_rate"].is_number()) info.sampleRate = root["sample_rate"].get<double>();
        const auto meta = root.contains ("metadata") && root["metadata"].is_object() ? root["metadata"] : nlohmann::json::object();
        auto str = [&meta] (const char* k) { return meta.contains (k) && meta[k].is_string() ? meta[k].get<std::string>() : std::string(); };
        info.name = str ("name");
        info.modeledBy = str ("modeled_by");
        info.gearMake = str ("gear_make");
        info.gearModel = str ("gear_model");
        info.gearType = str ("gear_type");
        info.toneType = str ("tone_type");
        if (meta.contains ("loudness") && meta["loudness"].is_number())
        {
            info.loudnessDb = meta["loudness"].get<double>();
            info.hasLoudness = std::isfinite (info.loudnessDb);
        }
        const long rf = receptiveFieldOf (root);
        info.receptiveField = rf > 0 && rf < 1000000 ? static_cast<int> (rf) : -1;
        // The file name is what the player recognises; the capture's own name is often generic.
        const auto stem = std::filesystem::path (path).stem().string();
        if (info.name.empty() || info.name == "Test Model" || info.name == "default") info.name = stem;
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}

//==============================================================================
class SlotModel
{
public:
    std::unique_ptr<::nam::DSP> dsp;
    ModelInfo info;
    ResamplingPair adaptor;       // engine rate <-> trained rate (inactive for 48 kHz captures)
    int latency = 0;              // engine-rate samples added by the adaptor
    double normGain = 1.0;        // loudness normalisation to -18 dBFS
    bool warm = false;            // audio thread: state is current
    int catchUp = MorphEngine::kDefaultCatchUp; // engine-rate samples of history needed to settle the state
    std::vector<float> ring;      // engine-rate output history, indexed by the engine clock

    void prepare()
    {
        const double trained = dsp->GetExpectedSampleRate();
        const bool needsAdaptor = trained > 0.0 && std::fabs (trained - MorphEngine::kEngineRate) > 0.5
                                  && ! dsp->SupportsArbitrarySampleRate();
        const double netRate = needsAdaptor ? trained : MorphEngine::kEngineRate;
        adaptor.prepare (MorphEngine::kEngineRate, netRate, MorphEngine::kChunk);
        latency = adaptor.latencySamples();
        netBlock = adaptor.maxInnerBlock();
        dsp->Reset (netRate, netBlock); // allocates and prewarms - background thread only

        nIn = std::max (1, dsp->NumInputChannels());
        nOut = std::max (1, dsp->NumOutputChannels());
        inBufs.assign (static_cast<size_t> (nIn), std::vector<float> (static_cast<size_t> (netBlock), 0.0f));
        outBufs.assign (static_cast<size_t> (nOut), std::vector<float> (static_cast<size_t> (netBlock), 0.0f));
        inPtrs.clear(); outPtrs.clear();
        for (auto& b : inBufs) inPtrs.push_back (b.data());
        for (auto& b : outBufs) outPtrs.push_back (b.data());
        ring.assign (static_cast<size_t> (MorphEngine::kRing), 0.0f);
        normGain = info.hasLoudness ? std::pow (10.0, (-18.0 - info.loudnessDb) / 20.0) : 1.0;
        if (info.receptiveField > 0)
        {
            const double rfEngine = info.receptiveField * MorphEngine::kEngineRate / netRate;
            catchUp = std::clamp (static_cast<int> (std::ceil (rfEngine)) + 2 * latency + 256, 1024, MorphEngine::kMaxCatchUp);
        }
    }

    /** n <= kChunk engine-rate samples. */
    void process (const float* in, float* out, int n) noexcept
    {
        adaptor.process (in, out, n, [this] (const float* x, float* y, int k) { runNet (x, y, k); });
    }

private:
    void runNet (const float* x, float* y, int k) noexcept
    {
        for (int start = 0; start < k; start += netBlock)
        {
            const int c = std::min (netBlock, k - start);
            for (auto& b : inBufs) std::copy_n (x + start, c, b.data());
            dsp->process (inPtrs.data(), outPtrs.data(), c);
            if (nOut == 1) { std::copy_n (outBufs[0].data(), c, y + start); continue; }
            const float scale = 1.0f / static_cast<float> (nOut);
            for (int i = 0; i < c; ++i)
            {
                float s = 0.0f;
                for (auto& b : outBufs) s += b[static_cast<size_t> (i)];
                y[start + i] = s * scale;
            }
        }
    }

    int netBlock = MorphEngine::kChunk, nIn = 1, nOut = 1;
    std::vector<std::vector<float>> inBufs, outBufs;
    std::vector<float*> inPtrs, outPtrs;
};

void destroySlotModel (SlotModel* m) noexcept { delete m; }

/** Marks "clear this slot" in a slot's pending pointer (never dereferenced). */
static SlotModel* clearMarker() noexcept
{
    static SlotModel* const marker = reinterpret_cast<SlotModel*> (static_cast<std::uintptr_t> (0x1));
    return marker;
}

ModelLoad MorphEngine::loadModel (const std::string& path)
{
    (void) touchArchitectures();
    ModelLoad r;
    if (! readModelInfo (path, r.info, r.error)) return r;
    try
    {
        auto dsp = ::nam::get_dsp (std::filesystem::path (path));
        if (dsp == nullptr) { r.error = "unsupported or invalid model"; return r; }
        if (dsp->NumInputChannels() < 1 || dsp->NumOutputChannels() < 1) { r.error = "model has no audio channels"; return r; }
        auto* m = new SlotModel();
        r.model.reset (m);
        m->dsp = std::move (dsp);
        m->info = r.info;
        m->prepare();
    }
    catch (const std::exception& e)
    {
        r.model.reset();
        r.error = e.what();
    }
    return r;
}

//==============================================================================
MorphEngine::MorphEngine()
{
    for (auto& l : levels) l.store (0.0f);
    prepare (48000.0, 512);
}

MorphEngine::~MorphEngine()
{
    for (auto& s : slots)
    {
        if (auto* p = s.pending.exchange (nullptr); p != nullptr && p != clearMarker()) delete p;
        if (s.old != nullptr && s.old != s.cur) delete s.old;
        delete s.cur;
    }
    collectGarbage();
}

void MorphEngine::prepare (double hostSampleRate, int maxHostBlock)
{
    hostRate = hostSampleRate;
    hostPair.prepare (hostSampleRate, kEngineRate, std::max (1, maxHostBlock));
    inputRing.assign (static_cast<size_t> (kRing), 0.0f);
    scratchIn.assign (static_cast<size_t> (kChunk), 0.0f);
    scratchOut.assign (static_cast<size_t> (kChunk), 0.0f);
    morph.prepare (kEngineRate, 0.020);
    normAmount.prepare (kEngineRate, 0.030);
    for (auto& s : slots) s.trim.prepare (kEngineRate, 0.030);
    fadeStep = 1.0 / (0.040 * kEngineRate);
    reset();
}

void MorphEngine::reset()
{
    hostPair.reset();
    std::fill (inputRing.begin(), inputRing.end(), 0.0f);
    clock = 0;
    firstBlock = true;
    if (fading) finishFade();
    for (auto& s : slots) if (s.cur != nullptr) { s.cur->warm = false; std::fill (s.cur->ring.begin(), s.cur->ring.end(), 0.0f); }
    newMask = oldMask = maskOf (false);
    newComp = oldComp = compensationFor (newMask, false);
    latency.store (hostPair.latencySamples() + static_cast<int> (std::lround (newComp * hostRate / kEngineRate)));
}

//==============================================================================
void MorphEngine::setSlot (int slot, ModelLoad&& load)
{
    if (slot < 0 || slot >= kNumSlots) return;
    if (load.model == nullptr) { clearSlot (slot); return; }
    {
        std::lock_guard<std::mutex> lock (infoLock);
        shownInfo[static_cast<size_t> (slot)] = load.info;
        shownLoaded[static_cast<size_t> (slot)] = true;
    }
    auto* m = load.model.release();
    if (auto* prev = slots[static_cast<size_t> (slot)].pending.exchange (m); prev != nullptr && prev != clearMarker())
        delete prev; // superseded before the audio thread picked it up
}

void MorphEngine::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumSlots) return;
    {
        std::lock_guard<std::mutex> lock (infoLock);
        shownInfo[static_cast<size_t> (slot)] = ModelInfo();
        shownLoaded[static_cast<size_t> (slot)] = false;
    }
    if (auto* prev = slots[static_cast<size_t> (slot)].pending.exchange (clearMarker()); prev != nullptr && prev != clearMarker())
        delete prev;
}

void MorphEngine::collectGarbage()
{
    for (auto& r : retired) delete r.exchange (nullptr, std::memory_order_acq_rel);
}

bool MorphEngine::slotHasModel (int slot) const
{
    std::lock_guard<std::mutex> lock (infoLock);
    return slot >= 0 && slot < kNumSlots && shownLoaded[static_cast<size_t> (slot)];
}

ModelInfo MorphEngine::slotInfo (int slot) const
{
    std::lock_guard<std::mutex> lock (infoLock);
    return slot >= 0 && slot < kNumSlots ? shownInfo[static_cast<size_t> (slot)] : ModelInfo();
}

std::string MorphEngine::slotPath (int slot) const { return slotInfo (slot).path; }

bool MorphEngine::retire (SlotModel* m) noexcept
{
    if (m == nullptr) return true;
    for (auto& r : retired)
    {
        SlotModel* expected = nullptr;
        if (r.compare_exchange_strong (expected, m, std::memory_order_acq_rel)) return true;
    }
    return false;
}

//==============================================================================
SlotModel* MorphEngine::modelFor (int k, bool old) const noexcept
{
    const auto& s = slots[static_cast<size_t> (k)];
    return old && s.changed ? s.old : s.cur;
}

SlotMask MorphEngine::maskOf (bool old) const noexcept
{
    SlotMask m = 0;
    for (int k = 0; k < kNumSlots; ++k) if (modelFor (k, old) != nullptr) m |= (1u << k);
    return m;
}

int MorphEngine::compensationFor (SlotMask mask, bool old) const noexcept
{
    int c = 0;
    for (int k = 0; k < kNumSlots; ++k)
        if (slotLoaded (mask, k)) c = std::max (c, modelFor (k, old)->latency);
    return c;
}

void MorphEngine::pickUpChanges (const BlockParams& params) noexcept
{
    // Only take changes we can retire afterwards (the retire list is drained by collectGarbage()).
    int freeRetire = 0;
    for (auto& r : retired) if (r.load (std::memory_order_acquire) == nullptr) ++freeRetire;

    bool changed = false;
    const SlotMask maskBefore = maskOf (false);
    const int compBefore = compensationFor (maskBefore, false);
    for (int k = 0; k < kNumSlots; ++k)
    {
        auto& s = slots[static_cast<size_t> (k)];
        if (s.pending.load (std::memory_order_acquire) == nullptr) continue;
        if (s.cur != nullptr && freeRetire <= 0) continue; // try again next block
        auto* p = s.pending.exchange (nullptr, std::memory_order_acq_rel);
        if (p == nullptr) continue;
        if (s.cur != nullptr) --freeRetire;
        s.old = s.cur;
        s.changed = true;
        s.cur = p == clearMarker() ? nullptr : p;
        if (s.cur != nullptr) s.cur->warm = false;
        changed = true;
    }

    const double target = std::clamp (params.morph, 1.0, 10.0);
    const bool jump = std::fabs (target - morph.getCurrent()) > 1.0;
    if (! changed && ! jump) return;

    oldMask = maskBefore;
    oldComp = compBefore;
    oldPos = morph.getCurrent();
    if (jump) morph.snap (target);
    newPos = morph.getCurrent();
    newMask = maskOf (false);
    newComp = compensationFor (newMask, false);

    // Nothing audible changes (e.g. a model replaced in a silent slot): switch without a fade.
    const auto a = morphAt (oldPos, oldMask), b = morphAt (newPos, newMask);
    bool audibleChange = oldMask != newMask || oldComp != newComp || a.lo != b.lo || a.hi != b.hi || std::fabs (oldPos - newPos) > 1e-9;
    for (int k : { a.lo, a.hi })
        if (k >= 0 && slots[static_cast<size_t> (k)].changed) audibleChange = true;

    fading = true;
    fade = 0.0;
    if (! audibleChange) finishFade();
    latency.store (hostPair.latencySamples() + static_cast<int> (std::lround (newComp * hostRate / kEngineRate)), std::memory_order_relaxed);
}

void MorphEngine::finishFade() noexcept
{
    for (auto& s : slots)
    {
        if (! s.changed) continue;
        retire (s.old); // space was reserved when the change was picked up
        s.old = nullptr;
        s.changed = false;
    }
    fading = false;
    oldMask = newMask;
    oldComp = newComp;
}

void MorphEngine::runRange (SlotModel& model, std::int64_t start, int count) noexcept
{
    constexpr std::int64_t mask = kRing - 1;
    for (int done = 0; done < count;)
    {
        const int n = std::min (kChunk, count - done);
        const std::int64_t t0 = start + done;
        for (int i = 0; i < n; ++i) scratchIn[static_cast<size_t> (i)] = inputRing[static_cast<size_t> ((t0 + i) & mask)];
        model.process (scratchIn.data(), scratchOut.data(), n);
        for (int i = 0; i < n; ++i) model.ring[static_cast<size_t> ((t0 + i) & mask)] = scratchOut[static_cast<size_t> (i)];
        done += n;
    }
}

void MorphEngine::updateWarm (double posA, double posB) noexcept
{
    std::array<SlotModel*, 2 * kNumSlots> needed {};
    int numNeeded = 0;
    auto need = [&] (SlotModel* m)
    {
        if (m == nullptr) return;
        for (int i = 0; i < numNeeded; ++i) if (needed[static_cast<size_t> (i)] == m) return;
        needed[static_cast<size_t> (numNeeded++)] = m;
    };

    SlotMask runMask = 0;
    if (fading)
    {
        const SlotMask o = warmSetFor (oldPos, oldPos, oldMask, false);
        for (int k = 0; k < kNumSlots; ++k) if (slotLoaded (o, k)) { need (modelFor (k, true)); runMask |= 1u << k; }
        const SlotMask n = warmSetFor (newPos, newPos, newMask, false); // neighbours warm up after the fade
        for (int k = 0; k < kNumSlots; ++k) if (slotLoaded (n, k)) { need (modelFor (k, false)); runMask |= 1u << k; }
    }
    else
    {
        const SlotMask n = warmSetFor (posA, posB, newMask, true);
        for (int k = 0; k < kNumSlots; ++k) if (slotLoaded (n, k)) { need (modelFor (k, false)); runMask |= 1u << k; }
    }

    // Everything not needed goes cold; needed models that were cold are caught up first.
    for (auto& s : slots)
        for (auto* m : { s.cur, s.old })
            if (m != nullptr)
            {
                bool isNeeded = false;
                for (int i = 0; i < numNeeded; ++i) isNeeded |= needed[static_cast<size_t> (i)] == m;
                if (! isNeeded) m->warm = false;
            }

    for (int i = 0; i < numNeeded; ++i)
    {
        auto& m = *needed[static_cast<size_t> (i)];
        if (! m.warm)
        {
            runRange (m, clock - m.catchUp, m.catchUp);
            m.warm = true;
            catchUps.fetch_add (1, std::memory_order_relaxed);
        }
    }

    running.store (runMask, std::memory_order_relaxed);
    runningCount.store (numNeeded, std::memory_order_relaxed);
}

void MorphEngine::traceAdd (const void* model, int slot, double gain, double weight) const noexcept
{
    if (traceRow == nullptr) return;
    for (auto& e : *traceRow)
    {
        if (e.slot == -2) { e.model = model; e.slot = slot; e.gain = static_cast<float> (gain); e.weight = static_cast<float> (weight); return; } // first unused entry
        if (e.model == model && e.slot == slot) { e.gain += static_cast<float> (gain); e.weight += static_cast<float> (weight); return; }
    }
}

double MorphEngine::mixSample (SlotMask mask, int comp, bool useOld, double pos, int i, const double* trims, double weight) const noexcept
{
    constexpr std::int64_t ringMask = kRing - 1;
    if (mask == 0)
    {
        traceAdd (nullptr, -1, weight, weight);
        return inputRing[static_cast<size_t> ((clock + i - comp) & ringMask)];
    }
    const auto p = morphAt (pos, mask);
    const double norm = normAmount.getCurrent();
    double y = 0.0;
    auto add = [&] (int k, double g)
    {
        if (k < 0 || g <= 0.0) return;
        const auto* m = modelFor (k, useOld);
        const double level = trims[k] * (1.0 + norm * (m->normGain - 1.0));
        traceAdd (m, k, weight * g * level, weight * g);
        y += g * level * m->ring[static_cast<size_t> ((clock + i - (comp - m->latency)) & ringMask)];
    };
    add (p.lo, p.gainLo);
    if (p.hi != p.lo) add (p.hi, p.gainHi);
    return y;
}

void MorphEngine::domainProcess (const float* in, float* out, int m, const BlockParams& params) noexcept
{
    constexpr std::int64_t ringMask = kRing - 1;
    for (int i = 0; i < m; ++i) inputRing[static_cast<size_t> ((clock + i) & ringMask)] = in[i];

    const double target = std::clamp (params.morph, 1.0, 10.0);
    if (firstBlock)
    {
        morph.snap (target);
        normAmount.snap (params.normalize ? 1.0 : 0.0);
        for (int k = 0; k < kNumSlots; ++k) slots[static_cast<size_t> (k)].trim.snap (GainSmoother::dbToGain (params.slotTrimDb[static_cast<size_t> (k)]));
        oldPos = newPos = target;
        firstBlock = false;
    }

    if (! fading) pickUpChanges (params);
    if (! fading) morph.setTarget (target);
    normAmount.setTarget (params.normalize ? 1.0 : 0.0);
    for (int k = 0; k < kNumSlots; ++k)
        slots[static_cast<size_t> (k)].trim.setTarget (GainSmoother::dbToGain (params.slotTrimDb[static_cast<size_t> (k)]));

    const double posA = morph.getCurrent(), posB = morph.peek (m);
    updateWarm (posA, posB);

    // Run every needed model on this block.
    for (auto& s : slots)
    {
        if (s.cur != nullptr && s.cur->warm) runRange (*s.cur, clock, m);
        if (s.old != nullptr && s.old != s.cur && s.old->warm) runRange (*s.old, clock, m);
    }

    std::array<double, kNumSlots> trims {};
    constexpr double halfPi = 1.57079632679489661923;
    for (int i = 0; i < m; ++i)
    {
        for (int k = 0; k < kNumSlots; ++k) trims[static_cast<size_t> (k)] = slots[static_cast<size_t> (k)].trim.next();
        normAmount.next();
        if (trace != nullptr) { trace->gains.emplace_back(); trace->gains.back().fill ({ nullptr, -2, 0.0f, 0.0f }); traceRow = &trace->gains.back(); }
        double y;
        if (fading)
        {
            fade = std::min (1.0, fade + fadeStep);
            const double gNew = 0.5 - 0.5 * std::cos (fade * 2.0 * halfPi); // raised cosine: sums to 1
            const double yOld = mixSample (oldMask, oldComp, true, oldPos, i, trims.data(), 1.0 - gNew);
            const double yNew = mixSample (newMask, newComp, false, newPos, i, trims.data(), gNew);
            y = yOld + gNew * (yNew - yOld);
        }
        else
        {
            y = mixSample (newMask, newComp, false, morph.next(), i, trims.data(), 1.0);
        }
        out[i] = static_cast<float> (y);
        traceRow = nullptr;
    }

    // UI telemetry: what each slot contributes at the end of the block.
    const auto p = morphAt (fading ? newPos : morph.getCurrent(), newMask);
    for (int k = 0; k < kNumSlots; ++k)
    {
        const float g = k == p.lo ? static_cast<float> (p.gainLo) : (k == p.hi ? static_cast<float> (p.gainHi) : 0.0f);
        levels[static_cast<size_t> (k)].store (g, std::memory_order_relaxed);
    }

    clock += m;
    if (fading && fade >= 1.0) finishFade();
}

void MorphEngine::process (const float* in, float* out, int numSamples, const BlockParams& params) noexcept
{
    hostPair.process (in, out, numSamples, [this, &params] (const float* x, float* y, int m)
    {
        for (int start = 0; start < m; start += kRing / 4)
        {
            const int n = std::min (kRing / 4, m - start);
            domainProcess (x + start, y + start, n, params);
        }
    });
}

} // namespace vfxa
