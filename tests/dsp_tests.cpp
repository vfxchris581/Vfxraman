// VFXAamps — DSP tests (no JUCE). Stages 1 and 2: resampling, tone/dynamics, single NAM slot,
// and the 10-slot morph checked sample-by-sample against an always-running reference.

#include "vfxa/Dynamics.h"
#include "vfxa/Filters.h"
#include "vfxa/MorphEngine.h"
#include "vfxa/MorphMap.h"
#include "vfxa/SincResampler.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#ifndef VFXA_EXAMPLE_MODELS
 #define VFXA_EXAMPLE_MODELS "."
#endif

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static const double pi = 3.14159265358979323846;

#include "postfx_tests.h"
static std::string model (const char* name) { return std::string (VFXA_EXAMPLE_MODELS) + "/" + name; }

/** Guitar-like test signal: plucked notes (decaying harmonics), mostly in the guitar range. */
static std::vector<float> guitarSignal (double fs, double seconds, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    const int n = static_cast<int> (fs * seconds);
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    const double notes[] = { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 220.0, 164.8 };
    int noteLen = static_cast<int> (0.25 * fs);
    for (int start = 0, k = 0; start < n; start += noteLen, ++k)
    {
        const double f = notes[k % 8] * (uni (rng) < 0.3 ? 2.0 : 1.0);
        const double amp = 0.15 + 0.25 * uni (rng);
        for (int i = 0; i < noteLen * 3 && start + i < n; ++i)
        {
            const double t = i / fs;
            double v = 0.0;
            for (int h = 1; h <= 6; ++h) v += std::sin (2 * pi * f * h * t) / (h * h);
            x[static_cast<size_t> (start + i)] += static_cast<float> (amp * v * std::exp (-t * 3.0) * std::min (1.0, i / 48.0));
        }
    }
    return x;
}

/** Steady chord (constant level): any discontinuity in a model's output stands out against it. */
static std::vector<float> steadySignal (double fs, double seconds)
{
    const int n = static_cast<int> (fs * seconds);
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        x[static_cast<size_t> (i)] = static_cast<float> (0.12 * std::sin (2 * pi * 110.0 * t) + 0.08 * std::sin (2 * pi * 164.8 * t)
                                                       + 0.05 * std::sin (2 * pi * 220.0 * t) + 0.03 * std::sin (2 * pi * 329.6 * t));
    }
    return x;
}

/** Largest jump in the second difference relative to the surrounding 10 ms of signal.
    A smooth signal gives values around 1-3; a click or gap gives 10+. */
static double clickScore (const std::vector<float>& y, double fs, int skip, int* where = nullptr)
{
    const int n = static_cast<int> (y.size());
    std::vector<double> d (static_cast<size_t> (n), 0.0);
    for (int i = 2; i < n; ++i) d[static_cast<size_t> (i)] = std::fabs (y[static_cast<size_t> (i)] - 2.0 * y[static_cast<size_t> (i - 1)] + y[static_cast<size_t> (i - 2)]);
    const int w = static_cast<int> (0.010 * fs);
    double worst = 0.0;
    std::vector<double> prefix (static_cast<size_t> (n + 1), 0.0);
    for (int i = 0; i < n; ++i) prefix[static_cast<size_t> (i + 1)] = prefix[static_cast<size_t> (i)] + d[static_cast<size_t> (i)] * d[static_cast<size_t> (i)];
    for (int i = skip; i < n - w; ++i)
    {
        const int a = std::max (0, i - w), b = std::min (n, i + w);
        const double rms = std::sqrt ((prefix[static_cast<size_t> (b)] - prefix[static_cast<size_t> (a)]) / (b - a));
        const double score = d[static_cast<size_t> (i)] / (rms + 1e-4);
        if (score > worst) { worst = score; if (where) *where = i; }
    }
    return worst;
}

static double rmsOf (const std::vector<float>& y, int from = 0)
{
    double s = 0; int n = 0;
    for (size_t i = static_cast<size_t> (from); i < y.size(); ++i) { s += y[i] * y[i]; ++n; }
    return std::sqrt (s / std::max (1, n));
}

//==============================================================================
static void testResampler()
{
    std::printf ("Resampler\n");
    for (double host : { 44100.0, 88200.0, 96000.0, 32000.0 })
    {
        vfxa::ResamplingPair pair;
        pair.prepare (host, 48000.0, 512);
        const int lat = pair.latencySamples();
        const int n = static_cast<int> (host);
        std::vector<float> x (static_cast<size_t> (n), 0.0f), y (x.size(), 0.0f);
        const int impulseAt = 10000;
        x[impulseAt] = 1.0f;
        std::mt19937 rng (3);
        bool ok = true;
        for (int pos = 0; pos < n;)
        {
            const int k = std::min (n - pos, 1 + static_cast<int> (rng() % 512));
            ok = pair.process (x.data() + pos, y.data() + pos, k, [] (const float* a, float* b, int m) { std::copy (a, a + m, b); }) && ok;
            pos += k;
        }
        int peak = 0;
        for (int i = 0; i < n; ++i) if (std::fabs (y[static_cast<size_t> (i)]) > std::fabs (y[static_cast<size_t> (peak)])) peak = i;
        CHECK (ok, "underrun at %.0f Hz", host);
        CHECK (peak == impulseAt + lat, "host %.0f: impulse peak at %d, expected %d", host, peak, impulseAt + lat);
        double dc = 0;
        for (float v : y) dc += v;
        CHECK (std::fabs (dc - 1.0) < 0.002, "host %.0f: impulse DC gain %.4f", host, dc);

        // 1 kHz and 10 kHz sines come through at unity gain with no distortion
        for (double f : { 1000.0, 10000.0 })
        {
            pair.reset();
            std::vector<float> s (static_cast<size_t> (n)), o (s.size());
            for (int i = 0; i < n; ++i) s[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (2 * pi * f * i / host));
            for (int pos = 0; pos < n; pos += 300) pair.process (s.data() + pos, o.data() + pos, std::min (300, n - pos), [] (const float* a, float* b, int m) { std::copy (a, a + m, b); });
            double err = 0;
            for (int i = 4000; i < n - 100; ++i) err = std::max (err, (double) std::fabs (o[static_cast<size_t> (i)] - s[static_cast<size_t> (i - lat)]));
            CHECK (err < 0.005, "host %.0f, %.0f Hz: max error %.5f (latency %d)", host, f, err, lat);
        }
        std::printf ("  host %6.0f Hz -> 48 kHz -> host: latency %d samples (%.2f ms)\n", host, lat, 1000.0 * lat / host);
    }
}

static void testMorphMap()
{
    std::printf ("Morph map\n");
    using namespace vfxa;
    const SlotMask all = (1u << kNumSlots) - 1;
    auto p = morphAt (1.5, all);
    CHECK (p.lo == 0 && p.hi == 1 && std::fabs (p.gainLo - std::sqrt (0.5)) < 1e-9 && std::fabs (p.gainHi - std::sqrt (0.5)) < 1e-9, "1.5 equal power");
    p = morphAt (3.0, all);
    CHECK (p.lo == 2 && p.hi == 2 && p.gainLo == 1.0, "integer position plays one slot");
    p = morphAt (10.0, all);
    CHECK (p.lo == 9 && p.gainLo == 1.0, "10 plays slot 10");

    const SlotMask some = (1u << 1) | (1u << 4); // slots 2 and 5
    p = morphAt (1.0, some);  CHECK (p.lo == 1 && p.hi == 1, "below first loaded plays it");
    p = morphAt (3.5, some);  CHECK (p.lo == 1 && p.hi == 4 && std::fabs (p.t - 0.5) < 1e-9, "skips empty slots (t=%.3f)", p.t);
    p = morphAt (9.0, some);  CHECK (p.lo == 4 && p.hi == 4, "above last loaded plays it");

    // power continuity across the whole knob for random masks
    std::mt19937 rng (7);
    for (int trial = 0; trial < 200; ++trial)
    {
        const SlotMask m = static_cast<SlotMask> (rng() & all);
        if (m == 0) continue;
        auto gainsAt = [m] (double k, double* g)
        {
            for (int s = 0; s < kNumSlots; ++s) g[s] = 0.0;
            const auto q = morphAt (k, m);
            g[q.lo] += q.gainLo;
            if (q.hi != q.lo) g[q.hi] += q.gainHi;
            return q;
        };
        double last[kNumSlots], cur[kNumSlots];
        gainsAt (1.0, last);
        bool continuous = true, power = true;
        for (double k = 1.001; k <= 10.0; k += 0.001)
        {
            const auto q = gainsAt (k, cur);
            power &= std::fabs (q.gainLo * q.gainLo + q.gainHi * q.gainHi - 1.0) < 1e-9;
            for (int s = 0; s < kNumSlots; ++s) continuous &= std::fabs (cur[s] - last[s]) < 0.01;
            std::copy (cur, cur + kNumSlots, last);
        }
        CHECK (power && continuous, "mask %x: equal power %d, continuous %d", m, power, continuous);
        const SlotMask w = warmSetFor (5.5, 5.5, m);
        CHECK (countSlots (w) <= 4, "warm set at most 4 (got %d)", countSlots (w));
    }
}

static void testToneAndDynamics()
{
    std::printf ("Tone stack / gate / limiter\n");
    const double fs = 48000;
    vfxa::ToneStack t;
    t.prepare (fs);
    t.update (5, 5, 5, 5);
    double maxDev = 0;
    for (double f : { 50.0, 200.0, 1000.0, 4000.0, 12000.0 })
    {
        t.reset();
        double peak = 0;
        for (int i = 0; i < 48000; ++i) { const double y = t.process (std::sin (2 * pi * f * i / fs)); if (i > 24000) peak = std::max (peak, std::fabs (y)); }
        maxDev = std::max (maxDev, std::fabs (20 * std::log10 (peak)));
    }
    CHECK (maxDev < 0.05, "flat at noon: %.3f dB", maxDev);

    // gate closes on silence-level noise and opens on a note, without clicks
    vfxa::NoiseGate g;
    g.prepare (fs);
    g.setThresholdDb (-60);
    double gainQuiet = 0, gainLoud = 0;
    for (int i = 0; i < 24000; ++i) gainQuiet = g.process (0.0001 * std::sin (i * 0.1));
    for (int i = 0; i < 4800; ++i) gainLoud = g.process (0.3 * std::sin (i * 0.1));
    CHECK (gainQuiet < 1e-3 && gainLoud > 0.999, "gate %.4f / %.4f", gainQuiet, gainLoud);

    vfxa::Limiter lim;
    lim.prepare (fs);
    lim.setCeilingDb (-0.3);
    double worst = 0;
    for (int i = 0; i < 48000; ++i)
    {
        double l = 3.0 * std::sin (2 * pi * 100 * i / fs), r = -l;
        lim.process (l, r);
        worst = std::max (worst, std::fabs (l));
    }
    CHECK (worst <= std::pow (10.0, -0.3 / 20) + 1e-9, "limiter ceiling %.4f", worst);
}

//==============================================================================
struct Run
{
    std::vector<float> out;
    double seconds = 0;
};

/** Runs an engine over x with a per-block callback that sets params (and may load/clear slots). */
template <typename BlockFn>
static Run runEngine (vfxa::MorphEngine& e, const std::vector<float>& x, double fs, int block, BlockFn&& perBlock, bool randomBlocks = false)
{
    Run r;
    r.out.assign (x.size(), 0.0f);
    vfxa::MorphEngine::BlockParams p;
    std::mt19937 rng (11);
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0, b = 0; pos < static_cast<int> (x.size()); ++b)
    {
        const int n = std::min (randomBlocks ? 1 + static_cast<int> (rng() % block) : block, static_cast<int> (x.size()) - pos);
        perBlock (b, pos, p);
        e.process (x.data() + pos, r.out.data() + pos, n, p);
        e.collectGarbage();
        pos += n;
    }
    r.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return r;
}

static void testSingleSlot()
{
    std::printf ("Stage 1: single NAM slot\n");
    for (const char* name : { "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam", "A2.nam" })
    {
        auto load = vfxa::MorphEngine::loadModel (model (name));
        CHECK (load.model != nullptr, "%s failed to load: %s", name, load.error.c_str());
        if (load.model == nullptr) continue;

        for (double fs : { 48000.0, 44100.0, 96000.0 })
        {
            vfxa::MorphEngine e;
            e.prepare (fs, 512);
            e.setSlot (0, vfxa::MorphEngine::loadModel (model (name)));
            const auto x = steadySignal (fs, 3.0);
            const auto r = runEngine (e, x, fs, 512, [] (int, int, vfxa::MorphEngine::BlockParams& p) { p.morph = 1.0; });
            bool finite = true;
            for (float v : r.out) finite &= std::isfinite (v);
            const double rms = rmsOf (r.out, static_cast<int> (0.5 * fs));
            int at = 0;
            const double click = clickScore (r.out, fs, static_cast<int> (0.5 * fs), &at);
            CHECK (finite && rms > 1e-3, "%s @%.0f: rms %.5f", name, fs, rms);
            std::printf ("  %-26s @%6.0f Hz  rms %.4f  edge %.1f  latency %d  CPU %.1f%% of real time\n", name, fs, rms, click,
                         e.latencySamples(), 100.0 * r.seconds / 3.0);
        }
    }
}

/** Largest per-sample change of any source gain in a trace (engine rate). */
static double maxGainSlope (const vfxa::MorphEngine::GainTrace& tr, int skip, int* where = nullptr)
{
    auto gainOf = [] (const auto& row, const void* m, int slot)
    {
        for (const auto& e : row) if (e.slot != -2 && e.model == m && e.slot == slot) return static_cast<double> (e.weight);
        return 0.0;
    };
    double worst = 0;
    for (size_t i = static_cast<size_t> (std::max (1, skip)); i < tr.gains.size(); ++i)
    {
        auto check = [&] (const auto& row, const auto& other)
        {
            for (const auto& e : row)
            {
                if (e.slot == -2) break;
                const double d = std::fabs (gainOf (row, e.model, e.slot) - gainOf (other, e.model, e.slot));
                if (d > worst) { worst = d; if (where) *where = static_cast<int> (i); }
            }
        };
        check (tr.gains[i], tr.gains[i - 1]);
        check (tr.gains[i - 1], tr.gains[i]);
    }
    return worst;
}

static double totalGain (const std::array<vfxa::MorphEngine::TraceEntry, 2 * vfxa::kNumSlots + 1>& row)
{
    double s = 0;
    for (const auto& e : row) if (e.slot != -2) s += e.weight;
    return s;
}

/** The morph output must equal, sample for sample, the gains the engine applied times the output of
    each slot model running continuously from the start. That proves warm neighbours and catch-up leave
    no trace in the audio; the gain trace itself is then checked for smoothness (no steps = no clicks). */
static void testMorphAgainstReference()
{
    std::printf ("Stage 2: 10-slot morph vs always-running reference (48 kHz)\n");
    const double fs = 48000;
    const char* names[vfxa::kNumSlots] = { "wavenet.nam", "wavenet_a1_standard.nam", "lstm.nam", nullptr, "A2.nam",
                                           "my_model.nam", nullptr, "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam" };
    const double seconds = 9.0;
    const auto x = guitarSignal (fs, seconds, 5);

    std::vector<std::vector<float>> ref (vfxa::kNumSlots);
    for (int k = 0; k < vfxa::kNumSlots; ++k)
    {
        if (names[k] == nullptr) continue;
        vfxa::MorphEngine e;
        e.prepare (fs, 256);
        e.setSlot (0, vfxa::MorphEngine::loadModel (model (names[k])));
        // raw model output: the trace's gains already include the loudness normalisation
        ref[static_cast<size_t> (k)] = runEngine (e, x, fs, 256, [] (int, int, vfxa::MorphEngine::BlockParams& p) { p.morph = 1.0; p.normalize = false; }).out;
    }

    // automation: slow sweep 1 -> 10 -> 1 (0-6 s), then big jumps every 0.25 s (6-9 s)
    auto knobAt = [&] (int pos)
    {
        const double t = pos / fs;
        if (t < 3.0) return 1.0 + 3.0 * t;
        if (t < 6.0) return 10.0 - 3.0 * (t - 3.0);
        static const double jumps[] = { 9.0, 2.0, 6.5, 1.0, 10.0, 3.5, 8.2, 1.7, 5.0, 9.5, 2.5, 7.0 };
        return jumps[static_cast<int> ((t - 6.0) * 4) % 12];
    };
    vfxa::MorphEngine e;
    e.prepare (fs, 256);
    for (int k = 0; k < vfxa::kNumSlots; ++k) if (names[k]) e.setSlot (k, vfxa::MorphEngine::loadModel (model (names[k])));
    vfxa::MorphEngine::GainTrace trace;
    trace.gains.reserve (x.size());
    e.setGainTrace (&trace);
    int maxRunning = 0;
    const auto r = runEngine (e, x, fs, 256, [&] (int, int pos, vfxa::MorphEngine::BlockParams& p)
    {
        p.morph = std::clamp (knobAt (pos), 1.0, 10.0);
        maxRunning = std::max (maxRunning, e.runningModels());
    });

    double maxErr = 0;
    int worstAt = 0;
    const double outRms = rmsOf (r.out, 0);
    for (size_t i = 0; i < x.size(); ++i)
    {
        double y = 0;
        for (const auto& en : trace.gains[i])
        {
            if (en.slot == -2) break;
            y += en.gain * (en.slot < 0 ? x[i] : ref[static_cast<size_t> (en.slot)][i]);
        }
        // before the references finished their own first fade-in from dry, they are not pure model output
        if (i < static_cast<size_t> (0.05 * fs)) continue;
        const double err = std::fabs (y - r.out[i]);
        if (err > maxErr) { maxErr = err; worstAt = static_cast<int> (i); }
    }
    const double errDb = 20 * std::log10 (maxErr / (outRms + 1e-12) + 1e-12);
    if (errDb > -60.0)
    {
        std::printf ("  debug @%d out %.5f:", worstAt, r.out[static_cast<size_t> (worstAt)]);
        for (const auto& en : trace.gains[static_cast<size_t> (worstAt)])
            if (en.slot != -2) std::printf (" [slot %d g %.4f w %.4f ref %.5f]", en.slot, en.gain, en.weight, en.slot < 0 ? x[static_cast<size_t> (worstAt)] : ref[static_cast<size_t> (en.slot)][static_cast<size_t> (worstAt)]);
        std::printf ("\n");
    }
    int slopeAt = 0;
    const double slope = maxGainSlope (trace, 1, &slopeAt);
    CHECK (errDb < -60.0, "max deviation from reference %.1f dB re RMS at %.3f s", errDb, worstAt / fs);
    CHECK (maxRunning <= 4, "ran %d models at once", maxRunning);
    CHECK (slope < 3e-3, "gain step %.5f at %.3f s", slope, slopeAt / fs);
    // equal-power check at a parked half-way point is covered by the map test; check no dropout here
    double minSum = 1e9;
    for (size_t i = static_cast<size_t> (0.1 * fs); i < trace.gains.size(); ++i)
    {
        minSum = std::min (minSum, totalGain (trace.gains[i]));
    }
    CHECK (minSum > 0.4, "gain dropout: sum of gains fell to %.3f", minSum);
    std::printf ("  sweep + jumps: deviation %.1f dB re RMS, max gain step %.5f/sample, <= %d models, %d catch-ups, CPU %.1f%%\n",
                 errDb, slope, maxRunning, e.catchUpsDone(), 100.0 * r.seconds / seconds);
}

static void testMorphStress()
{
    std::printf ("Stage 2: automation stress with loads/clears while playing (all host rates, random block sizes)\n");
    const char* names[vfxa::kNumSlots] = { "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam", "A2.nam", nullptr,
                                           "my_model.nam", "wavenet.nam", nullptr, "lstm.nam", "wavenet_a1_standard.nam" };

    // a 44.1 kHz-trained capture (same network, relabelled) exercises the per-slot rate adaptor
    const auto dir = std::filesystem::temp_directory_path() / "vfxa_tests";
    std::filesystem::create_directories (dir);
    const auto m441 = (dir / "wavenet_44k.nam").string();
    {
        std::ifstream in (model ("wavenet.nam"));
        std::string s ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto p = s.find ("\"sample_rate\": 48000");
        if (p != std::string::npos) s.replace (p, 20, "\"sample_rate\": 44100");
        std::ofstream (m441) << s;
    }

    for (double fs : { 48000.0, 44100.0, 96000.0 })
    {
        vfxa::MorphEngine e;
        e.prepare (fs, 1024);
        for (int k = 0; k < vfxa::kNumSlots; ++k) if (names[k]) e.setSlot (k, vfxa::MorphEngine::loadModel (model (names[k])));
        vfxa::MorphEngine::GainTrace trace;
        e.setGainTrace (&trace);
        const auto x = guitarSignal (fs, 8.0, 9);
        std::mt19937 rng (21);
        double knob = 1.0;
        int maxRunning = 0, lastEvent = -1;
        const auto r = runEngine (e, x, fs, 1024, [&] (int b, int pos, vfxa::MorphEngine::BlockParams& p)
        {
            const double t = pos / fs;
            if (t < 2.0) knob = 1.0 + 4.5 * t;                                                   // fast sweep up
            else if (t < 3.0) { if (b % 20 == 0) knob = 1.0 + 9.0 * (rng() % 1000) / 999.0; }  // big random jumps
            else if (t < 5.0) knob = 5.5 + 4.5 * std::sin (2 * pi * 2.0 * t);                   // 2 Hz wobble across slots
            else knob = 7.4;
            p.morph = knob;
            for (int k = 0; k < vfxa::kNumSlots; ++k) p.slotTrimDb[static_cast<size_t> (k)] = t > 6.5 ? -6.0f * static_cast<float> (k % 2) : 0.0f;
            p.normalize = t < 7.2;
            const int ev = static_cast<int> (t * 4);
            if (ev != lastEvent)
            {
                lastEvent = ev;
                if (ev == 21) e.setSlot (7, vfxa::MorphEngine::loadModel (m441));                 // fill the empty slot under the knob
                if (ev == 23) e.clearSlot (6);                                                    // remove the audible neighbour
                if (ev == 25) e.setSlot (6, vfxa::MorphEngine::loadModel (model ("lstm.nam")));  // put a different capture back
                if (ev == 26) e.setSlot (7, vfxa::MorphEngine::loadModel (model ("A2.nam")));    // replace the audible slot in place
            }
            maxRunning = std::max (maxRunning, e.runningModels());
        }, true);
        bool finite = true;
        for (float v : r.out) finite &= std::isfinite (v);
        int at = 0;
        const double slope = maxGainSlope (trace, 1, &at);
        double minSum = 1e9;
        for (size_t i = static_cast<size_t> (0.1 * 48000); i < trace.gains.size(); ++i)
        {
            minSum = std::min (minSum, totalGain (trace.gains[i]));
        }
        CHECK (finite, "non-finite output @%.0f", fs);
        CHECK (slope < 3e-3, "@%.0f gain step %.5f at %.3f s", fs, slope, at / 48000.0);
        CHECK (minSum > 0.4, "@%.0f gain dropout %.3f", fs, minSum);
        CHECK (maxRunning <= 6, "@%.0f %d models running", fs, maxRunning);
        std::printf ("  @%6.0f Hz: max gain step %.5f/sample, min total gain %.2f, max %d models (incl. crossfades), latency %d, CPU %.1f%%\n",
                     fs, slope, minSum, maxRunning, e.latencySamples(), 100.0 * r.seconds / 8.0);
    }
}

/** Stage 1 exactness: one slot at 48 kHz must be the raw NAM output times the loudness gain. */
static void testSingleSlotExact()
{
    std::printf ("Stage 1: single slot output == NAM output x normalisation\n");
    const double fs = 48000;
    const auto x = guitarSignal (fs, 2.0, 3);
    for (const char* name : { "wavenet.nam", "lstm.nam", "A2.nam" })
    {
        vfxa::MorphEngine a, b;
        a.prepare (fs, 300); b.prepare (fs, 300);
        auto la = vfxa::MorphEngine::loadModel (model (name));
        const double norm = la.info.hasLoudness ? std::pow (10.0, (-18.0 - la.info.loudnessDb) / 20.0) : 1.0;
        a.setSlot (0, std::move (la));
        b.setSlot (0, vfxa::MorphEngine::loadModel (model (name)));
        const auto ra = runEngine (a, x, fs, 300, [] (int, int, vfxa::MorphEngine::BlockParams& p) { p.morph = 1; p.normalize = true; });
        const auto rb = runEngine (b, x, fs, 300, [] (int, int, vfxa::MorphEngine::BlockParams& p) { p.morph = 1; p.normalize = false; });
        double err = 0;
        for (size_t i = 4800; i < x.size(); ++i) err = std::max (err, std::fabs (ra.out[i] - norm * rb.out[i]));
        CHECK (err < 1e-5, "%s normalisation mismatch %.2e", name, err);
    }
}

int main()
{
    testResampler();
    testMorphMap();
    testToneAndDynamics();
    testSingleSlot();
    testSingleSlotExact();
    testMorphAgainstReference();
    testMorphStress();
    testPostFx();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
