// VFXAamps — tests for the effects rack (graphic EQ, delay, reverb) and the tuner.
// Included by dsp_tests.cpp after its CHECK macro.
#pragma once

#include "vfxa/PostFx.h"
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

static void testDelay()
{
    using namespace postfx_tests;
    std::printf ("Delay\n");
    const double fs = 48000.0;
    vfxa::StereoDelay::Params p;
    p.on = true; p.timeMs = 250.0; p.feedback = 0.5; p.mix = 1.0; p.toneHz = 20000.0;

    {
        vfxa::StereoDelay d;
        d.prepare (fs);
        std::vector<float> x (static_cast<size_t> (fs * 1.0), 0.0f);
        x[1000] = 1.0f;
        std::vector<float> l, r;
        runStereo (x, l, r, 512, [&] (float* a, float* b, int n, int) { d.process (a, b, n, p); });
        auto peakNear = [&] (size_t at) { size_t best = at; for (size_t i = at - 20; i < at + 20; ++i) if (std::fabs (l[i]) > std::fabs (l[best])) best = i; return best; };
        const size_t e1 = peakNear (1000 + 12000), e2 = peakNear (1000 + 24000);
        CHECK (l[1000] == 1.0f, "dry signal passes unchanged (%.3f)", l[1000]);
        CHECK (e1 == 13000 && e2 == 25000, "repeats at 250 / 500 ms (samples %zu, %zu)", e1, e2);
        const double ratio = std::fabs (l[e2]) / std::fabs (l[e1]);
        CHECK (std::fabs (l[e1]) > 0.8 && ratio > 0.4 && ratio < 0.55, "first repeat %.3f, feedback ratio %.3f (set 0.5)", std::fabs (l[e1]), ratio);
    }
    // ping-pong: first repeat on the left, second on the right
    {
        vfxa::StereoDelay d;
        d.prepare (fs);
        auto q = p; q.pingPong = true;
        std::vector<float> x (static_cast<size_t> (fs * 1.0), 0.0f);
        x[1000] = 1.0f;
        std::vector<float> l, r;
        runStereo (x, l, r, 512, [&] (float* a, float* b, int n, int) { d.process (a, b, n, q); });
        CHECK (std::fabs (l[13000]) > 0.4 && std::fabs (r[13000]) < 1e-6 && std::fabs (r[25000]) > 0.2 && std::fabs (l[25000]) < 1e-6,
               "ping-pong alternates L %.3f R %.3f / L %.3f R %.3f", l[13000], r[13000], l[25000], r[25000]);
    }
    // trails: switched off after the first impulse, so a second impulse gets no echo but the first keeps repeating
    {
        vfxa::StereoDelay d;
        d.prepare (fs);
        std::vector<float> x (static_cast<size_t> (fs * 1.5), 0.0f);
        x[1000] = 1.0f; x[30000] = 1.0f;
        std::vector<float> l, r;
        runStereo (x, l, r, 512, [&] (float* a, float* b, int n, int blk) { auto q = p; q.on = blk * 512 < 20000; d.process (a, b, n, q); });
        CHECK (std::fabs (l[37000]) > 0.05 && std::fabs (l[42000 - 1]) < 0.02 && std::fabs (l[42000]) < 0.02,
               "trails ring out after switching off (%.3f), no new echoes (%.4f)", l[37000], l[42000]);
    }
    // turning the time knob while a tone plays glides instead of clicking; heavy feedback stays bounded
    {
        vfxa::StereoDelay d;
        d.prepare (fs);
        const auto x = tone (fs, 4.0, 220.0);
        std::vector<float> l, r;
        auto q = p; q.feedback = 0.6; q.mix = 0.5;
        runStereo (x, l, r, 256, [&] (float* a, float* b, int n, int blk) { q.timeMs = blk % 40 < 20 ? 120.0 : 480.0; d.process (a, b, n, q); });
        bool finite = true; for (float v : l) finite &= std::isfinite (v);
        CHECK (finite && maxStep (l) < 0.2, "time changes while playing are click-free (max step %.4f)", maxStep (l));

        vfxa::StereoDelay h;
        h.prepare (fs);
        std::mt19937 rng (1); std::normal_distribution<float> nd (0.0f, 0.5f);
        std::vector<float> loud (static_cast<size_t> (fs * 8.0)); for (auto& v : loud) v = nd (rng);
        auto z = p; z.feedback = 0.95; z.timeMs = 30.0;
        runStereo (loud, l, r, 512, [&] (float* a, float* b, int n, int) { h.process (a, b, n, z); });
        double peak = 0; for (float v : l) peak = std::max (peak, (double) std::fabs (v));
        CHECK (std::isfinite (peak) && peak < 12.0, "max feedback on loud noise stays bounded (peak %.2f)", peak);
    }
}

static void testReverb()
{
    using namespace postfx_tests;
    std::printf ("Reverb\n");
    for (double fs : { 44100.0, 96000.0 })
    {
        for (double rt : { 1.5, 5.0 })
        {
            vfxa::FdnReverb rv;
            rv.prepare (fs);
            vfxa::FdnReverb::Params p;
            p.on = true; p.decayS = rt; p.mix = 1.0; p.preDelayMs = 0.0; p.toneHz = 18000.0; p.size = 0.8;
            std::vector<float> x (static_cast<size_t> (fs * (rt * 1.6 + 0.5)), 0.0f);
            x[100] = 1.0f;
            std::vector<float> l, r;
            runStereo (x, l, r, 512, [&] (float* a, float* b, int n, int) { rv.process (a, b, n, p); });
            l[100] -= 1.0f; r[100] -= 1.0f; // wet only
            // Schroeder backward integration -> fit the -5 .. -25 dB part (T20 x 3)
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
            const double t60 = i25 > i5 ? 3.0 * static_cast<double> (i25 - i5) / fs : 0.0;
            CHECK (t60 > rt * 0.65 && t60 < rt * 1.35, "decay %.1f s measures RT60 %.2f s (%.0f Hz)", rt, t60, fs);

            double c = 0, el = 0, er = 0;
            for (size_t i = 0; i < l.size(); ++i) { c += static_cast<double> (l[i]) * r[i]; el += static_cast<double> (l[i]) * l[i]; er += static_cast<double> (r[i]) * r[i]; }
            const double corr = c / std::sqrt (el * er + 1e-30);
            CHECK (std::fabs (corr) < 0.35, "wide stereo tail (L/R correlation %.2f)", corr);
        }
    }
    // longest decay on loud noise: bounded, then decays away; on/off and knob moves while a tone plays are click-free
    {
        const double fs = 48000.0;
        vfxa::FdnReverb rv;
        rv.prepare (fs);
        vfxa::FdnReverb::Params p;
        p.on = true; p.decayS = 20.0; p.mix = 1.0; p.size = 1.0;
        std::mt19937 rng (2); std::normal_distribution<float> nd (0.0f, 0.5f);
        std::vector<float> x (static_cast<size_t> (fs * 6.0), 0.0f);
        for (size_t i = 0; i < static_cast<size_t> (fs); ++i) x[i] = nd (rng);
        std::vector<float> l, r;
        runStereo (x, l, r, 512, [&] (float* a, float* b, int n, int) { rv.process (a, b, n, p); });
        double peak = 0; for (float v : l) peak = std::max (peak, (double) std::fabs (v));
        CHECK (std::isfinite (peak) && peak < 8.0 && rmsOf (l, x.size() - 24000, x.size()) < rmsOf (l, 48000, 96000),
               "20 s decay stays bounded (peak %.2f) and decays", peak);

        vfxa::FdnReverb rv2;
        rv2.prepare (fs);
        const auto t = tone (fs, 4.0, 330.0);
        vfxa::FdnReverb::Params q;
        q.mix = 0.6;
        runStereo (t, l, r, 256, [&] (float* a, float* b, int n, int blk)
        {
            q.on = (blk / 50) % 2 == 0;
            q.size = (blk / 30) % 2 ? 0.2 : 0.9;
            q.decayS = (blk / 20) % 2 ? 0.8 : 4.0;
            q.preDelayMs = (blk / 25) % 2 ? 0.0 : 120.0;
            rv2.process (a, b, n, q);
        });
        bool finite = true; for (float v : l) finite &= std::isfinite (v);
        CHECK (finite && maxStep (l) < 0.25, "reverb on/off and knob moves are click-free (max step %.4f)", maxStep (l));
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
