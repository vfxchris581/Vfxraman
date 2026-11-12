// VFXAamps — tests for the effects rack (graphic EQ, delay, reverb) and the tuner.
// Included by dsp_tests.cpp after its CHECK macro.
#pragma once

#include "vfxa/PostFx.h"
#include "vfxa/StudioDelay.h"
#include "vfxa/StudioReverb.h"
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

static void testPostFx()
{
    testGraphicEq();
    testDelay();
    testReverb();
    testTuner();
}
