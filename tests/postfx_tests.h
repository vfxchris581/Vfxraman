// VFXAamps — tests for the effects rack (graphic EQ, delay, reverb) and the tuner.
// Included by dsp_tests.cpp after its CHECK macro.
#pragma once

#include "vfxa/PostFx.h"
#include "vfxa/StudioDelay.h"
#include "vfxa/StudioReverb.h"
#include "vfxa/OptoLeveler.h"
#include "vfxa/GraphicEq31.h"
#include "vfxa/Tuner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace postfx_tests
{
constexpr double kPi = 3.14159265358979323846;

inline std::vector<float> tone (double fs, double seconds, double hz, double amp = 0.25)
{
    std::vector<float> v (static_cast<size_t> (fs * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float> (amp * std::sin (2.0 * kPi * hz * static_cast<double> (i) / fs));
    return v;
}

inline double rmsOf (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0.0;
    b = std::min (b, v.size());
    for (size_t i = a; i < b; ++i) s += static_cast<double> (v[i]) * v[i];
    return b > a ? std::sqrt (s / static_cast<double> (b - a)) : 0.0;
}

inline double maxStep (const std::vector<float>& v, size_t a = 1)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (1, a); i < v.size(); ++i) m = std::max (m, static_cast<double> (std::fabs (v[i] - v[i - 1])));
    return m;
}

/** Runs a stereo processor block-wise on a mono input copied to both channels. */
template <typename Fn>
void runStereo (const std::vector<float>& in, std::vector<float>& l, std::vector<float>& r, int block, Fn&& fn)
{
    l = in; r = in;
    for (size_t pos = 0, b = 0; pos < in.size(); pos += static_cast<size_t> (block), ++b)
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (block), in.size() - pos));
        fn (l.data() + pos, r.data() + pos, n, static_cast<int> (b));
    }
}
} // namespace postfx_tests

static void testGraphicEq()
{
    using namespace postfx_tests;
    std::printf ("Graphic EQ (10 bands)\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        vfxa::GraphicEq eq;
        eq.prepare (fs);
        std::array<double, vfxa::GraphicEq::kBands> g {};
        std::mt19937 rng (3);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        std::vector<float> noise (static_cast<size_t> (fs * 0.3));
        for (auto& v : noise) v = nd (rng);
        std::vector<float> l, r;
        runStereo (noise, l, r, 256, [&] (float* a, float* b, int n, int) { eq.setGains (g, true); eq.process (a, b, n); });
        CHECK (l == noise && r == noise, "flat EQ is bit-exact at %.0f Hz", fs);

        for (int band : { 0, 4, 5, 9 })
        {
            for (double db : { 12.0, -12.0 })
            {
                eq.prepare (fs);
                g.fill (0.0);
                g[static_cast<size_t> (band)] = db;
                const double f = vfxa::GraphicEq::kFreqs[static_cast<size_t> (band)];
                const auto x = tone (fs, 1.0, f);
                runStereo (x, l, r, 256, [&] (float* a, float* b, int n, int) { eq.setGains (g, true); eq.process (a, b, n); });
                const double meas = 20.0 * std::log10 (rmsOf (l, x.size() / 2, x.size()) / rmsOf (x, x.size() / 2, x.size()));
                CHECK (std::fabs (meas - db) < 0.35, "band %.0f Hz at %+.0f dB measures %+.2f dB (%.0f Hz)", f, db, meas, fs);
            }
        }
    }

    // automation: every band jumping randomly every block on a 1 kHz tone, plus switching on/off
    {
        const double fs = 48000.0;
        vfxa::GraphicEq eq;
        eq.prepare (fs);
        std::mt19937 rng (9);
        std::uniform_real_distribution<double> u (-12.0, 12.0);
        std::array<double, vfxa::GraphicEq::kBands> g {};
        const auto x = tone (fs, 3.0, 1000.0);
        std::vector<float> l, r;
        runStereo (x, l, r, 128, [&] (float* a, float* b, int n, int blk)
        {
            if (blk % 8 == 0) for (auto& v : g) v = u (rng);
            eq.setGains (g, (blk / 60) % 2 == 0);
            eq.process (a, b, n);
        });
        bool finite = true;
        for (float v : l) finite &= std::isfinite (v);
        const double step = maxStep (l), limit = 4.0 * 0.25 * 2.0 * kPi * 1000.0 / fs * 1.6;
        CHECK (finite && step < limit, "EQ automation + on/off is click-free (max step %.4f, limit %.4f)", step, limit);
    }
}

namespace postfx_tests
{
/** T20-based RT60 from the Schroeder energy decay of a stereo impulse response. */
inline double rt60 (const std::vector<float>& l, const std::vector<float>& r, double fs)
{
    std::vector<double> edc (l.size());
    double acc = 0.0;
    for (size_t i = l.size(); i-- > 0;) { acc += static_cast<double> (l[i]) * l[i] + static_cast<double> (r[i]) * r[i]; edc[i] = acc; }
    size_t i5 = 0, i25 = 0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / edc[0] + 1e-30);
        if (! i5 && db < -5.0) i5 = i;
        if (! i25 && db < -25.0) { i25 = i; break; }
    }
    return i25 > i5 ? 3.0 * static_cast<double> (i25 - i5) / fs : 0.0;
}

inline std::vector<float> bandFilter (const std::vector<float>& x, double fs, vfxa::Svf::Type type, double hz)
{
    vfxa::Svf a, b;
    for (auto* f : { &a, &b }) { f->setSampleRate (fs); f->set (type, hz, 0.7071); }
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float> (b.process (a.process (x[i])));
    return y;
}

inline double peakAbs (const std::vector<float>& v, size_t a = 0, size_t b = SIZE_MAX)
{
    double p = 0.0;
    for (size_t i = a; i < std::min (b, v.size()); ++i) p = std::max (p, static_cast<double> (std::fabs (v[i])));
    return p;
}
} // namespace postfx_tests

static void testDelay()
{
    using namespace postfx_tests;
    using D = vfxa::StudioDelay;
    std::printf ("Studio delay (VX-D8)\n");
    const double fs = 48000.0;
    D::Params p;
    p.on = true; p.mode = D::Digital; p.timeMs = 250.0; p.feedback = 0.5; p.mix = 1.0;
    p.lowCutHz = 20.0; p.highCutHz = 20000.0; p.modDepth = 0.0;
    std::vector<float> imp (static_cast<size_t> (fs * 1.0), 0.0f);
    imp[1000] = 1.0f;
    std::vector<float> l, r;

    {
        D d; d.prepare (fs);
        runStereo (imp, l, r, 512, [&] (float* a, float* b, int n, int) { d.process (a, b, n, p); });
        auto peakNear = [&] (size_t at) { size_t best = at; for (size_t i = at - 20; i < at + 20; ++i) if (std::fabs (l[i]) > std::fabs (l[best])) best = i; return best; };
        const size_t e1 = peakNear (13000), e2 = peakNear (25000);
        const double ratio = std::fabs (l[e2]) / std::fabs (l[e1]);
        CHECK (l[1000] == 1.0f && e1 == 13000 && e2 == 25000, "digital: dry untouched, repeats at 250 / 500 ms (%zu, %zu)", e1, e2);
        CHECK (std::fabs (l[e1]) > 0.75 && ratio > 0.38 && ratio < 0.56, "digital: first repeat %.3f, feedback ratio %.3f (set 0.5)", std::fabs (l[e1]), ratio);
    }
    {
        D d; d.prepare (fs);
        auto q = p; q.pingPong = true;
        runStereo (imp, l, r, 512, [&] (float* a, float* b, int n, int) { d.process (a, b, n, q); });
        CHECK (peakAbs (l, 12900, 13100) > 0.3 && peakAbs (r, 12900, 13100) < 1e-6 && peakAbs (r, 24900, 25100) > 0.15 && peakAbs (l, 24900, 25100) < 1e-6,
               "ping-pong alternates left / right");
    }
    {
        D d; d.prepare (fs);
        auto q = p; q.mode = D::Dual; q.feedback = 0.0;
        runStereo (imp, l, r, 512, [&] (float* a, float* b, int n, int) { d.process (a, b, n, q); });
        CHECK (peakAbs (l, 12900, 13100) > 0.5 && peakAbs (r, 12900, 13100) < 1e-3 && peakAbs (r, 18900, 19100) > 0.5,
               "dual: left at 250 ms, right at 375 ms");
    }
    // reverse: a swell (quiet -> loud) comes back loud -> quiet
    {
        D d; d.prepare (fs);
        auto q = p; q.mode = D::Reverse; q.timeMs = 300.0; q.feedback = 0.0;
        std::vector<float> swell (static_cast<size_t> (fs * 1.2), 0.0f);
        for (size_t i = 0; i < static_cast<size_t> (0.3 * fs); ++i)
            swell[i] = static_cast<float> (0.5 * (static_cast<double> (i) / (0.3 * fs)) * std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / fs));
        runStereo (swell, l, r, 256, [&] (float* a, float* b, int n, int) { d.process (a, b, n, q); });
        const double early = rmsOf (l, static_cast<size_t> (0.33 * fs), static_cast<size_t> (0.42 * fs));
        const double late = rmsOf (l, static_cast<size_t> (0.48 * fs), static_cast<size_t> (0.57 * fs));
        CHECK (early > 1.5 * late && early > 0.05, "reverse: the swell plays backwards (%.3f then %.3f)", early, late);
    }
    // ducking: repeats dip while playing, come back once the guitar stops
    {
        auto wetWhile = [&] (double duck, double& after)
        {
            D d; d.prepare (fs);
            auto q = p; q.duck = duck; q.feedback = 0.6; q.timeMs = 180.0;
            std::vector<float> x (static_cast<size_t> (fs * 2.5), 0.0f);
            for (size_t i = 0; i < static_cast<size_t> (1.5 * fs); ++i) x[i] = static_cast<float> (0.25 * std::sin (2.0 * kPi * 330.0 * static_cast<double> (i) / fs));
            runStereo (x, l, r, 256, [&] (float* a, float* b, int n, int) { d.process (a, b, n, q); });
            std::vector<float> wet (l.size());
            for (size_t i = 0; i < l.size(); ++i) wet[i] = l[i] - x[i];
            after = rmsOf (wet, static_cast<size_t> (2.0 * fs), static_cast<size_t> (2.4 * fs));
            return rmsOf (wet, static_cast<size_t> (0.8 * fs), static_cast<size_t> (1.4 * fs));
        };
        double a0 = 0, a1 = 0;
        const double w0 = wetWhile (0.0, a0), w1 = wetWhile (1.0, a1);
        const double duckDb = 20.0 * std::log10 (w1 / w0), backDb = 20.0 * std::log10 (a1 / a0);
        CHECK (duckDb < -12.0 && backDb > -6.0, "ducking: repeats %.1f dB while playing, %.1f dB after", duckDb, backDb);
    }
    // diffusion smears each repeat (lower crest factor)
    {
        auto crest = [&] (double diffusion)
        {
            D d; d.prepare (fs);
            auto q = p; q.diffusion = diffusion; q.feedback = 0.0;
            runStereo (imp, l, r, 512, [&] (float* a, float* b, int n, int) { d.process (a, b, n, q); });
            return peakAbs (l, 12000, 16000) / (rmsOf (l, 12000, 16000) + 1e-12);
        };
        const double c0 = crest (0.0), c1 = crest (1.0);
        CHECK (c1 < c0 * 0.4, "diffusion smears the repeats (crest %.1f -> %.1f)", c0, c1);
    }
    // every mode: max feedback on loud noise stays bounded; mode switching + knob moves on a tone are click-free
    {
        std::mt19937 rng (5); std::normal_distribution<float> nd (0.0f, 0.5f);
        std::vector<float> loud (static_cast<size_t> (fs * 4.0)); for (auto& v : loud) v = nd (rng);
        double worst = 0.0;
        for (int mode = 0; mode < D::kNumModes; ++mode)
        {
            D d; d.prepare (fs);
            auto q = p; q.mode = mode; q.feedback = 0.98; q.timeMs = 40.0; q.diffusion = 0.7; q.modDepth = 1.0;
            runStereo (loud, l, r, 512, [&] (float* a, float* b, int n, int) { d.process (a, b, n, q); });
            worst = std::max (worst, std::max (peakAbs (l), peakAbs (r)));
        }
        CHECK (std::isfinite (worst) && worst < 12.0, "all modes at 98%% feedback on loud noise stay bounded (peak %.2f)", worst);

        D d; d.prepare (fs);
        const auto t = tone (fs, 6.0, 220.0, 0.2);
        auto q = p; q.feedback = 0.6; q.mix = 0.5; q.modDepth = 0.4;
        runStereo (t, l, r, 256, [&] (float* a, float* b, int n, int blk)
        {
            q.mode = (blk / 70) % D::kNumModes;
            q.timeMs = blk % 40 < 20 ? 140.0 : 420.0;
            q.duck = (blk / 33) % 2 ? 0.8 : 0.0;
            q.diffusion = (blk / 27) % 2 ? 0.9 : 0.0;
            q.on = (blk / 200) % 2 == 0;
            d.process (a, b, n, q);
        });
        bool finite = true; for (float v : l) finite &= std::isfinite (v);
        CHECK (finite && maxStep (l) < 0.2, "mode switches, time / duck / diffusion moves and on/off are click-free (max step %.4f)", maxStep (l));
    }
}

static void testReverb()
{
    using namespace postfx_tests;
    using R = vfxa::StudioReverb;
    std::printf ("Studio reverb (VX-R16)\n");
    auto impulseResponse = [] (double fs, const R::Params& p, double seconds, std::vector<float>& l, std::vector<float>& r)
    {
        R rv; rv.prepare (fs);
        std::vector<float> x (static_cast<size_t> (fs * seconds), 0.0f);
        x[100] = 1.0f;
        runStereo (x, l, r, 512, [&] (float* a, float* b, int n, int) { rv.process (a, b, n, p); });
        l[100] -= 1.0f; r[100] -= 1.0f; // wet only
    };
    std::vector<float> l, r;
    for (double fs : { 44100.0, 96000.0 })
        for (int prog : { (int) R::Room, (int) R::Hall, (int) R::Plate, (int) R::Cathedral })
            for (double rt : { 1.5, 5.0 })
            {
                R::Params p;
                p.on = true; p.program = prog; p.decayS = rt; p.mix = 1.0; p.preDelayMs = 0.0; p.lowRT = 1.0; p.highRT = 1.0;
                p.highCutHz = 20000.0; p.lowCutHz = 20.0; p.early = 0.0;
                impulseResponse (fs, p, rt * 1.5 + 0.5, l, r);
                const double t = rt60 (l, r, fs);
                double c = 0, el = 0, er = 0;
                for (size_t i = 0; i < l.size(); ++i) { c += (double) l[i] * r[i]; el += (double) l[i] * l[i]; er += (double) r[i] * r[i]; }
                const double corr = c / std::sqrt (el * er + 1e-30);
                CHECK (t > rt * 0.75 && t < rt * 1.3 && std::fabs (corr) < 0.3, "%s %.1f s @ %.0f Hz: RT60 %.2f s, L/R correlation %.2f",
                       R::programName (prog), rt, fs, t, corr);
            }

    // separate low / high decay
    {
        const double fs = 48000.0;
        R::Params p;
        p.on = true; p.program = R::Hall; p.decayS = 3.0; p.mix = 1.0; p.preDelayMs = 0.0; p.early = 0.0; p.highCutHz = 20000.0; p.lowCutHz = 20.0;
        p.lowRT = 1.8; p.highRT = 0.3;
        impulseResponse (fs, p, 6.5, l, r);
        const double tLow = rt60 (bandFilter (l, fs, vfxa::Svf::Type::LowPass, 120.0), bandFilter (r, fs, vfxa::Svf::Type::LowPass, 120.0), fs);
        const double tMid = rt60 (bandFilter (bandFilter (l, fs, vfxa::Svf::Type::HighPass, 700.0), fs, vfxa::Svf::Type::LowPass, 1400.0),
                                  bandFilter (bandFilter (r, fs, vfxa::Svf::Type::HighPass, 700.0), fs, vfxa::Svf::Type::LowPass, 1400.0), fs);
        const double tHigh = rt60 (bandFilter (l, fs, vfxa::Svf::Type::HighPass, 8000.0), bandFilter (r, fs, vfxa::Svf::Type::HighPass, 8000.0), fs);
        CHECK (tLow > tMid * 1.3 && tHigh < tMid * 0.6, "LOW RT x1.8 / HIGH RT x0.3: lows %.2f s, mids %.2f s, highs %.2f s", tLow, tMid, tHigh);
    }
    // early reflections: hall has them, plate doesn't
    {
        const double fs = 48000.0;
        auto earlyEnergy = [&] (int prog, double early)
        {
            R::Params p;
            p.on = true; p.program = prog; p.decayS = 2.0; p.mix = 1.0; p.preDelayMs = 0.0; p.early = early;
            impulseResponse (fs, p, 0.3, l, r);
            return rmsOf (l, 0, static_cast<size_t> (0.04 * fs));
        };
        const double hallOn = earlyEnergy (R::Hall, 1.0), hallOff = earlyEnergy (R::Hall, 0.0);
        const double plateOn = earlyEnergy (R::Plate, 1.0), plateOff = earlyEnergy (R::Plate, 0.0);
        CHECK (hallOn > hallOff * 1.5 && std::fabs (plateOn - plateOff) < plateOff * 0.05 + 1e-9,
               "EARLY: hall %.4f -> %.4f, plate unchanged (%.4f / %.4f)", hallOff, hallOn, plateOff, plateOn);
    }
    // extremes: 20 s, size 1, low RT x2, full diffusion + modulation on loud noise -> bounded and decaying
    {
        const double fs = 48000.0;
        R rv; rv.prepare (fs);
        R::Params p;
        p.on = true; p.program = R::Cathedral; p.decayS = 20.0; p.size = 1.0; p.lowRT = 2.0; p.highRT = 1.0; p.diffusion = 1.0; p.mod = 1.0; p.mix = 1.0;
        std::mt19937 rng (2); std::normal_distribution<float> nd (0.0f, 0.5f);
        std::vector<float> x (static_cast<size_t> (fs * 8.0), 0.0f);
        for (size_t i = 0; i < static_cast<size_t> (fs); ++i) x[i] = nd (rng);
        runStereo (x, l, r, 512, [&] (float* a, float* b, int n, int) { rv.process (a, b, n, p); });
        const double peak = std::max (peakAbs (l), peakAbs (r));
        CHECK (std::isfinite (peak) && peak < 8.0 && rmsOf (l, x.size() - 48000, x.size()) < rmsOf (l, 48000, 96000),
               "most extreme settings stay bounded (peak %.2f) and decay", peak);
    }
    // programs switched and every knob moved while a tone plays: click-free
    {
        const double fs = 48000.0;
        R rv; rv.prepare (fs);
        const auto t = tone (fs, 6.0, 330.0, 0.2);
        R::Params q;
        q.mix = 0.6;
        runStereo (t, l, r, 256, [&] (float* a, float* b, int n, int blk)
        {
            q.on = (blk / 150) % 2 == 0;
            q.program = (blk / 55) % R::kNumPrograms;
            q.size = (blk / 30) % 2 ? 0.1 : 0.95;
            q.decayS = (blk / 20) % 2 ? 0.6 : 6.0;
            q.preDelayMs = (blk / 25) % 2 ? 0.0 : 150.0;
            q.lowRT = (blk / 17) % 2 ? 0.5 : 2.0;
            q.highRT = (blk / 19) % 2 ? 0.1 : 1.0;
            q.width = (blk / 23) % 2 ? 0.0 : 1.5;
            q.mod = (blk / 21) % 2 ? 0.0 : 1.0;
            rv.process (a, b, n, q);
        });
        bool finite = true; for (float v : l) finite &= std::isfinite (v);
        CHECK (finite && maxStep (l) < 0.25, "program switches, knob moves and on/off are click-free (max step %.4f)", maxStep (l));
    }
}

static void testTuner()
{
    using namespace postfx_tests;
    std::printf ("Tuner\n");
    struct Note { double hz; int midi; const char* name; };
    const Note notes[] = { { 46.25, 30, "F#1 (8-string)" }, { 61.74, 35, "B1 (7-string)" }, { 82.41, 40, "E2" }, { 110.0, 45, "A2" },
                           { 146.83, 50, "D3" }, { 196.0, 55, "G3" }, { 246.94, 59, "B3" }, { 329.63, 64, "E4" }, { 659.26, 76, "E5" },
                           { 1046.5, 84, "C6" } };
    int bad = 0;
    double worst = 0.0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto& note : notes)
        {
            for (double detune : { 0.0, 13.0, -27.0 })
            {
                for (int kind = 0; kind < 2; ++kind)
                {
                    vfxa::Tuner t;
                    t.prepare (fs);
                    const double f = note.hz * std::pow (2.0, detune / 1200.0);
                    std::vector<float> x (static_cast<size_t> (fs * 0.5));
                    for (size_t i = 0; i < x.size(); ++i)
                    {
                        const double ph = 2.0 * kPi * f * static_cast<double> (i) / fs;
                        double v = 0.0;
                        if (kind == 0) v = 0.3 * std::sin (ph);
                        else for (int h = 1; h <= 12; ++h) if (f * h < fs * 0.45) v += 0.3 / h * std::sin (h * ph + 0.7 * h) * std::exp (-0.3 * h * i / fs);
                        x[i] = static_cast<float> (v);
                    }
                    for (size_t pos = 0; pos < x.size(); pos += 256) t.push (x.data() + pos, static_cast<int> (std::min<size_t> (256, x.size() - pos)));
                    const auto rd = t.analyse();
                    // the expected nearest note (a -27 cent detune can't round to another note)
                    const double err = rd.valid ? std::fabs (rd.cents - detune) : 99.0;
                    worst = std::max (worst, err);
                    if (! rd.valid || rd.midiNote != note.midi || err > 1.5)
                    {
                        ++bad;
                        std::printf ("  tuner miss: %s %+.0f ct (%s, %.0f Hz): got %s%d %+.2f ct valid %d\n", note.name, detune,
                                     kind ? "harmonics" : "sine", fs, vfxa::TunerReading::noteName (rd.midiNote), rd.octave(), rd.cents, (int) rd.valid);
                    }
                }
            }
        }
    }
    CHECK (bad == 0, "tuner: every note, detune and rate within 1.5 cents (worst %.2f ct, %d misses)", worst, bad);

    vfxa::Tuner t;
    t.prepare (48000.0);
    std::vector<float> quiet (24000, 0.0f);
    t.push (quiet.data(), static_cast<int> (quiet.size()));
    CHECK (! t.analyse().valid, "no reading on silence");
}


static void testOptoLeveler()
{
    using namespace postfx_tests;
    using C = vfxa::OptoLeveler;
    std::printf ("Opto leveling amp (VX-O2)\n");
    const double fs = 48000.0;
    auto runMono = [&] (C& c, std::vector<float>& y, const C::Params& p, std::vector<double>* gr = nullptr)
    {
        for (size_t i = 0; i < y.size(); i += 48)
        {
            c.process (y.data() + i, nullptr, static_cast<int> (std::min<size_t> (48, y.size() - i)), p);
            if (gr) gr->push_back (c.getGainReductionDb());
        }
    };
    auto dbRms = [] (const std::vector<float>& v, size_t a, size_t b) { return 20.0 * std::log10 (rmsOf (v, a, b) + 1e-30); };

    // off: bit-exact
    {
        C c; c.prepare (fs);
        auto x = tone (fs, 0.5, 440.0); auto y = x;
        C::Params p; p.active = false;
        runMono (c, y, p);
        CHECK (y == x, "off: bit-exact passthrough");
    }
    // static curves: compress ~3:1 soft knee, limit much firmer
    auto slopeAt = [&] (bool limit, double hiDb)
    {
        double out[2];
        for (int k = 0; k < 2; ++k)
        {
            C c; c.prepare (fs);
            C::Params p; p.active = true; p.peakReduction = 0.6; p.limit = limit;
            auto y = tone (fs, 4.0, 220.0, std::pow (10.0, (hiDb - 5.0 * (1 - k)) / 20.0));
            runMono (c, y, p);
            out[k] = dbRms (y, y.size() - 48000, y.size());
        }
        return 5.0 / (out[1] - out[0]);
    };
    const double cLow = slopeAt (false, -28.0), cHigh = slopeAt (false, -3.0), lHigh = slopeAt (true, -3.0);
    CHECK (cLow < 1.25 && cHigh > 2.4 && cHigh < 3.4, "COMPRESS: soft knee (%.2f:1 low) rising to %.2f:1", cLow, cHigh);
    CHECK (lHigh > 7.0, "LIMIT: %.1f:1 at high level", lHigh);

    // attack ~10 ms, two-stage program-dependent release
    auto burst = [&] (double seconds, double& attackMs, double& rel50Ms, double& rel90s)
    {
        C c; c.prepare (fs);
        C::Params p; p.active = true; p.peakReduction = 0.6;
        const size_t n = static_cast<size_t> (fs * (2.0 + seconds + 8.0));
        std::vector<float> y (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / fs;
            const double db = (t >= 2.0 && t < 2.0 + seconds) ? -10.0 : -30.0;
            y[i] = static_cast<float> (std::pow (10.0, db / 20.0) * std::sin (2.0 * kPi * 440.0 * t));
        }
        std::vector<double> gr;
        runMono (c, y, p, &gr);
        auto at = [&] (double t) { return gr[std::min (gr.size() - 1, static_cast<size_t> (t * 1000.0))]; };
        const double base = at (1.99), peak = at (2.0 + seconds - 0.001), shortPeak = at (2.08);
        attackMs = rel50Ms = rel90s = 0.0;
        for (double t = 2.0; t < 2.08; t += 0.001) if (at (t) - base >= 0.63 * (shortPeak - base)) { attackMs = (t - 2.0) * 1000.0; break; }
        for (double t = 2.0 + seconds; t < 2.0 + seconds + 8.0; t += 0.001)
        {
            const double rel = (peak - at (t)) / (peak - base);
            if (rel50Ms == 0.0 && rel >= 0.5) rel50Ms = (t - 2.0 - seconds) * 1000.0;
            if (rel >= 0.9) { rel90s = t - 2.0 - seconds; break; }
        }
    };
    double a1, r1, f1, a2, r2, f2;
    burst (0.1, a1, r1, f1);
    burst (3.0, a2, r2, f2);
    CHECK (a1 > 4.0 && a1 < 20.0, "attack %.1f ms (opto: ~10 ms)", a1);
    CHECK (r1 > 25.0 && r1 < 120.0 && r2 > 25.0 && r2 < 120.0, "release to 50%%: %.0f / %.0f ms (opto: ~60 ms)", r1, r2);
    CHECK (f1 < 0.5 && f2 > 1.0 && f2 < 15.0, "full release is program-dependent: %.2f s after a short peak, %.2f s after a long passage", f1, f2);

    // tube stage: low distortion at nominal level, mostly 2nd harmonic; flat response
    {
        auto thd = [&] (double db, double& h2share)
        {
            C c; c.prepare (fs);
            C::Params p; p.active = true; p.peakReduction = 0.0;
            auto y = tone (fs, 1.0, 1000.0, std::pow (10.0, db / 20.0));
            runMono (c, y, p);
            auto amp = [&] (double hz)
            {
                double sn = 0, cs = 0;
                for (size_t i = 24000; i < 48000; ++i) { sn += y[i] * std::sin (2.0 * kPi * hz * i / fs); cs += y[i] * std::cos (2.0 * kPi * hz * i / fs); }
                return 2.0 * std::sqrt (sn * sn + cs * cs) / 24000.0;
            };
            const double f = amp (1000.0), h2 = amp (2000.0), h3 = amp (3000.0);
            h2share = h2 / std::sqrt (h2 * h2 + h3 * h3);
            return 100.0 * std::sqrt (h2 * h2 + h3 * h3) / f;
        };
        double s18, s12;
        const double t18 = thd (-18.0, s18), t12 = thd (-12.0, s12);
        CHECK (t18 < 0.2 && t12 < 0.35 && s18 > 0.9, "tube stage THD %.3f%% (-18 dBFS), %.3f%% (-12 dBFS), mostly 2nd harmonic", t18, t12);
        double worst = 0.0;
        for (double hz : { 20.0, 100.0, 1000.0, 10000.0, 20000.0 })
        {
            C c; c.prepare (fs);
            C::Params p; p.active = true; p.peakReduction = 0.0;
            const auto x = tone (fs, 1.0, hz, 0.06);
            auto y = x;
            runMono (c, y, p);
            worst = std::max (worst, std::fabs (dbRms (y, 24000, 48000) - dbRms (x, 24000, 48000)));
        }
        CHECK (worst < 0.05, "flat 20 Hz - 20 kHz (worst %.3f dB)", worst);
    }
    // gain knob, mix, stereo link, on/off and knob moves click-free, stable
    {
        C c; c.prepare (fs);
        C::Params p; p.active = true; p.peakReduction = 0.0; p.gainDb = 6.0;
        auto y = tone (fs, 1.0, 1000.0, 0.05);
        const auto x = y;
        runMono (c, y, p);
        const double g = dbRms (y, 24000, 48000) - dbRms (x, 24000, 48000);
        CHECK (std::fabs (g - 6.0) < 0.1, "GAIN +6 dB measures %+.2f dB", g);

        C s; s.prepare (fs);
        std::vector<float> l = tone (fs, 2.0, 220.0, 0.5), r (l.size(), 0.0f);
        C::Params q; q.active = true; q.peakReduction = 0.7;
        for (size_t i = 0; i < l.size(); i += 256) s.process (l.data() + i, r.data() + i, static_cast<int> (std::min<size_t> (256, l.size() - i)), q);
        CHECK (rmsOf (r, 48000, 96000) < 1e-3, "stereo-linked: a loud left side does not leak into a silent right (%.2g)", rmsOf (r, 48000, 96000));

        C m; m.prepare (fs);
        auto t = tone (fs, 6.0, 196.0, 0.3);
        std::vector<float> l2, r2;
        runStereo (t, l2, r2, 256, [&] (float* a, float* b, int n, int blk)
        {
            C::Params z;
            z.active = (blk / 150) % 2 == 0;
            z.peakReduction = (blk / 40) % 2 ? 0.2 : 0.9;
            z.limit = (blk / 70) % 2 == 1;
            z.gainDb = (blk / 33) % 2 ? 0.0 : 9.0;
            z.mix = (blk / 45) % 2 ? 1.0 : 0.4;
            z.emphasis = (blk / 27) % 2 ? 0.0 : 1.0;
            m.process (a, b, n, z);
        });
        bool finite = true; for (float v : l2) finite &= std::isfinite (v);
        CHECK (finite && maxStep (l2) < 0.12, "on/off, mode and knob moves are click-free (max step %.4f)", maxStep (l2));
    }
}


static void testGraphicEq31()
{
    using namespace postfx_tests;
    using E = vfxa::GraphicEq31;
    std::printf ("VX-31 dual 31-band graphic EQ / limiter\n");
    auto runEq = [] (E& e, std::vector<float>& l, std::vector<float>& r, const E::Params& p, int block = 256)
    {
        for (size_t i = 0; i < l.size(); i += static_cast<size_t> (block))
        {
            const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), l.size() - i));
            e.process (l.data() + i, r.data() + i, n, p);
        }
    };
    auto gainAt = [&] (double fs, const E::Params& p, double hz)
    {
        E e; e.prepare (fs);
        const double secs = hz < 60.0 ? 2.5 : 0.6; // narrow low bands settle slowly
        const auto x = tone (fs, secs, hz, 0.05);
        auto l = x, r = x;
        runEq (e, l, r, p);
        const size_t a = static_cast<size_t> (0.5 * secs * fs), b = x.size();
        return 20.0 * std::log10 (rmsOf (l, a, b) / rmsOf (x, a, b));
    };

    // neutral (on, flat) and off: bit-exact
    {
        E e; e.prepare (48000.0);
        E::Params p; p.on = true;
        std::mt19937 rng (4); std::normal_distribution<float> nd (0.0f, 0.2f);
        std::vector<float> x (24000); for (auto& v : x) v = nd (rng);
        auto l = x, r = x;
        runEq (e, l, r, p);
        CHECK (l == x && r == x, "on with every fader at 0 and nothing engaged: bit-exact");
    }
    // every ISO band hits its fader gain at its centre, at 44.1 / 48 / 96 kHz
    {
        double worst = 0.0;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int b = 0; b < E::kBands; b += 3)
            {
                E::Params p; p.on = true;
                p.ch[0].gainDb[static_cast<size_t> (b)] = 12.0;
                worst = std::max (worst, std::fabs (gainAt (fs, p, E::kFreqs[static_cast<size_t> (b)]) - 12.0));
            }
        CHECK (worst < 0.2, "each band reaches its fader gain at its ISO centre, 44.1 / 48 / 96 kHz (worst error %.2f dB)", worst);
    }
    // constant Q: the same 1/3-octave width (between the half-gain points) at every fader setting, boost or cut
    {
        const double fs = 48000.0, fc = 1000.0;
        std::vector<double> widths;
        for (double g : { 3.0, 6.0, 12.0, 15.0, -6.0, -15.0 })
        {
            E::Params p; p.on = true; p.ch[0].gainDb[17] = g;
            double lo = 0, hi = 0;
            for (double f = fc / 1.6; f < fc; f *= 1.004) if (std::fabs (gainAt (fs, p, f)) >= std::fabs (g) / 2.0) { lo = f; break; }
            for (double f = fc * 1.6; f > fc; f /= 1.004) if (std::fabs (gainAt (fs, p, f)) >= std::fabs (g) / 2.0) { hi = f; break; }
            widths.push_back (std::log2 (hi / lo));
        }
        double mn = 9, mx = 0; for (double w : widths) { mn = std::min (mn, w); mx = std::max (mx, w); }
        CHECK (mn > 0.30 && mx < 0.37, "constant Q: 1/3-octave width at every setting (%.3f .. %.3f octaves)", mn, mx);
    }
    // low cut: 40 Hz, 18 dB/octave, Bessel
    {
        E::Params p; p.on = true; p.ch[0].lowCut = true;
        const double fs = 48000.0;
        const double g40 = gainAt (fs, p, 40.0), g20 = gainAt (fs, p, 20.0), g10 = gainAt (fs, p, 10.0), g5 = gainAt (fs, p, 5.0), g200 = gainAt (fs, p, 200.0);
        CHECK (std::fabs (g40 + 3.0) < 0.3, "low cut: -3 dB at 40 Hz (%.2f)", g40);
        CHECK (g5 - g10 < -15.5 && g5 - g10 > -18.5 && g200 > -0.2, "low cut: 18 dB/octave slope (%.1f dB from 10 to 5 Hz), flat above (%.2f dB at 200 Hz); 20 Hz %.1f dB",
               g5 - g10, g200, g20);
    }
    // range +/-6 (handled by the plug-in mapping) is just the gain; link / unlink; input gain
    {
        const double fs = 48000.0;
        E::Params p; p.on = true; p.link = false; p.ch[0].gainDb[17] = 9.0; p.ch[1].inputDb = -6.0;
        E e; e.prepare (fs);
        const auto x = tone (fs, 0.6, 1000.0, 0.05);
        auto l = x, r = x;
        runEq (e, l, r, p);
        const double gl = 20.0 * std::log10 (rmsOf (l, 14400, 28800) / rmsOf (x, 14400, 28800));
        const double gr = 20.0 * std::log10 (rmsOf (r, 14400, 28800) / rmsOf (x, 14400, 28800));
        CHECK (std::fabs (gl - 9.0) < 0.2 && std::fabs (gr + 6.0) < 0.05, "unlinked: A +9 dB at 1 kHz (%.2f), B input -6 dB (%.2f)", gl, gr);
        p.link = true;
        E e2; e2.prepare (fs);
        l = x; r = x;
        runEq (e2, l, r, p);
        CHECK (l == r, "LINK: channel B follows channel A exactly");
    }
    // limiter: holds the threshold (ceiling +1 dB), meters, transparent when not reached
    {
        const double fs = 48000.0;
        E::Params p; p.on = true; p.ch[0].limit = true; p.ch[0].thresholdDb = -12.0;
        E e; e.prepare (fs);
        const auto x = tone (fs, 1.0, 220.0, 0.5); // -6 dBFS peak
        auto l = x, r = x;
        runEq (e, l, r, p);
        const double pk = 20.0 * std::log10 (peakAbs (l, 2400, l.size()));
        CHECK (pk < -11.0 && pk > -13.5 && e.getGainReductionDb (0) > 4.0, "limiter at -12 dBFS: peaks %.2f dBFS, GR %.1f dB", pk, e.getGainReductionDb (0));
        E e2; e2.prepare (fs);
        const auto q = tone (fs, 0.5, 220.0, 0.1); // -20 dBFS: under the threshold
        l = q; r = q;
        runEq (e2, l, r, p);
        CHECK (l == q, "limiter below threshold: bit-exact");
    }
    // noise reduction: quiet hiss is reduced up to 10 dB, playing level untouched
    {
        const double fs = 48000.0;
        E::Params p; p.on = true; p.ch[0].nr = true;
        std::mt19937 rng (6); std::normal_distribution<float> nd (0.0f, 0.0001f); // ~ -80 dBFS hiss, well under the knee
        std::vector<float> hiss (static_cast<size_t> (fs)); for (auto& v : hiss) v = nd (rng);
        E e; e.prepare (fs);
        auto l = hiss, r = hiss;
        runEq (e, l, r, p);
        const double red = 20.0 * std::log10 (rmsOf (l, 24000, 48000) / rmsOf (hiss, 24000, 48000));
        const double play = gainAt (fs, p, 330.0);
        CHECK (red < -8.0 && red > -10.5 && std::fabs (play) < 0.05, "noise reduction: hiss %.1f dB (max 10), playing level %.2f dB", red, play);
    }
    // automation of every control and on/off: click-free; extremes bounded
    {
        const double fs = 48000.0;
        E e; e.prepare (fs);
        std::mt19937 rng (8); std::uniform_real_distribution<double> u (-15.0, 15.0);
        const auto x = tone (fs, 4.0, 700.0, 0.2);
        auto l = x, r = x;
        E::Params p;
        for (size_t i = 0, blk = 0; i < l.size(); i += 128, ++blk)
        {
            if (blk % 8 == 0) for (auto& g : p.ch[0].gainDb) g = u (rng);
            p.on = (blk / 200) % 2 == 0;
            p.ch[0].lowCut = (blk / 70) % 2 == 1;
            p.ch[0].limit = (blk / 90) % 2 == 1;
            p.ch[0].nr = (blk / 110) % 2 == 1;
            p.ch[0].inputDb = (blk / 50) % 2 ? 6.0 : -6.0;
            const int n = static_cast<int> (std::min<size_t> (128, l.size() - i));
            e.process (l.data() + i, r.data() + i, n, p);
        }
        bool finite = true; for (float v : l) finite &= std::isfinite (v);
        const double limit = 6.0 * 0.2 * 2.0 * kPi * 700.0 / fs * 2.0;
        CHECK (finite && maxStep (l) < limit, "faders, switches and on/off while playing are click-free (max step %.4f, limit %.4f)", maxStep (l), limit);
    }
}

static void testPostFx()
{
    testGraphicEq();
    testDelay();
    testReverb();
    testOptoLeveler();
    testGraphicEq31();
    testTuner();
}
