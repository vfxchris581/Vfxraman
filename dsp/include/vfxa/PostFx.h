// VFXAamps — post-amp effects rack: 10-band graphic EQ, stereo delay, FDN reverb.
// 64-bit processing, buffers allocated in prepare(), nothing allocated or locked in process().
#pragma once

#include "vfxa/Filters.h"
#include "vfxa/Smoothers.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vfxa
{

namespace fxdetail
{
inline int nextPow2 (int n) noexcept { int p = 1; while (p < n) p <<= 1; return p; }

/** Circular delay line with 4-point Hermite interpolation (smooth for time modulation). */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        const int size = nextPow2 (maxDelaySamples + 8);
        buf.assign (static_cast<size_t> (size), 0.0);
        mask = size - 1;
        w = 0;
    }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0); }
    void push (double x) noexcept { buf[static_cast<size_t> (w)] = x; w = (w + 1) & mask; }
    /** Sample written d samples ago (d >= 1; the most recent push is d = 1). */
    double read (double d) const noexcept
    {
        const double pos = static_cast<double> (w) - d;
        const double fl = std::floor (pos);
        const int i = static_cast<int> (fl);
        const double t = pos - fl;
        auto at = [this] (int k) { return buf[static_cast<size_t> (k & mask)]; };
        const double y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
private:
    std::vector<double> buf { 0.0 };
    int mask = 0, w = 0;
};

/** One-pole low-pass (6 dB/oct), used for damping inside feedback loops. */
struct OnePole
{
    double a = 0.0, z = 0.0;
    void set (double fc, double fs) noexcept { a = std::exp (-2.0 * 3.14159265358979323846 * std::min (fc, 0.45 * fs) / fs); }
    double process (double x) noexcept { z = x + a * (z - x); return z; }
};
} // namespace fxdetail

//==============================================================================
/** 10-band graphic EQ, ±12 dB, octave bands 31 Hz .. 16 kHz (constant-Q bells). At 0 dB a band is exactly transparent. */
class GraphicEq
{
public:
    static constexpr int kBands = 10;
    static constexpr std::array<double, kBands> kFreqs { 31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
    static constexpr double kQ = 1.41;

    void prepare (double fs) noexcept
    {
        sampleRate = fs;
        for (int b = 0; b < kBands; ++b)
        {
            for (auto* f : { &l[static_cast<size_t> (b)], &r[static_cast<size_t> (b)] }) { f->setSampleRate (fs); f->reset(); }
            gain[static_cast<size_t> (b)].prepare (fs, 0.040);
            gain[static_cast<size_t> (b)].snap (0.0);
        }
        updateCoefficients();
        idle = true;
    }

    void reset() noexcept { for (int b = 0; b < kBands; ++b) { l[static_cast<size_t> (b)].reset(); r[static_cast<size_t> (b)].reset(); } }

    /** Band gains in dB (targets; smoothed here). Pass all zeros (or on = false) to bypass. */
    void setGains (const std::array<double, kBands>& db, bool on) noexcept
    {
        for (int b = 0; b < kBands; ++b) gain[static_cast<size_t> (b)].setTarget (on ? std::clamp (db[static_cast<size_t> (b)], -12.0, 12.0) : 0.0);
    }

    void process (float* left, float* right, int n) noexcept
    {
        bool any = false;
        for (auto& g : gain) any |= g.isSmoothing() || g.getCurrent() != 0.0;
        if (! any) { idle = true; return; }      // flat: exact passthrough
        if (idle) { reset(); idle = false; }      // engaging from flat: start from clean filter state

        for (int i = 0; i < n; ++i)
        {
            if (--countdown <= 0)
            {
                countdown = 16;
                bool smoothing = false;
                for (auto& g : gain) smoothing |= g.isSmoothing();
                if (smoothing || needsUpdate) { updateCoefficients(); needsUpdate = smoothing; }
            }
            for (auto& g : gain) g.next();
            double a = left[i], c = right[i];
            for (int b = 0; b < kBands; ++b) { a = l[static_cast<size_t> (b)].process (a); c = r[static_cast<size_t> (b)].process (c); }
            left[i] = static_cast<float> (a);
            right[i] = static_cast<float> (c);
        }
    }

private:
    void updateCoefficients() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            const double f = std::min (kFreqs[static_cast<size_t> (b)], 0.45 * sampleRate);
            l[static_cast<size_t> (b)].set (Svf::Type::Bell, f, kQ, gain[static_cast<size_t> (b)].getCurrent());
            r[static_cast<size_t> (b)].set (Svf::Type::Bell, f, kQ, gain[static_cast<size_t> (b)].getCurrent());
        }
    }

    double sampleRate = 48000.0;
    std::array<Svf, kBands> l, r;
    std::array<LinearSmoother, kBands> gain;
    int countdown = 0;
    bool needsUpdate = true, idle = true;
};

//==============================================================================
/** Stereo delay: tape-style time glide, filtered + soft-limited feedback, optional ping-pong.
    Switching it off stops new input and lets the repeats ring out (trails). */
class StereoDelay
{
public:
    static constexpr double kMaxSeconds = 2.5;

    struct Params
    {
        bool on = false, pingPong = false;
        double timeMs = 400.0, feedback = 0.35, mix = 0.3, toneHz = 6000.0;
    };

    void prepare (double fs)
    {
        sampleRate = fs;
        const int max = static_cast<int> (std::ceil (kMaxSeconds * fs)) + 4;
        dl.prepare (max);
        dr.prepare (max);
        feed.prepare (fs, 0.020); feed.snap (0.0);
        mixS.prepare (fs, 0.030); mixS.snap (0.0);
        fbS.prepare (fs, 0.030); fbS.snap (0.0);
        timeSamples = 0.4 * fs;
        glide = std::exp (-1.0 / (0.12 * fs)); // ~120 ms time glide
        for (auto* f : { &hpL, &hpR }) { f->setSampleRate (fs); f->reset(); f->set (Svf::Type::HighPass, 90.0, 0.7071); }
        lpL.set (6000.0, fs); lpR.set (6000.0, fs);
        reset();
    }

    void reset() noexcept
    {
        dl.clear(); dr.clear();
        hpL.reset(); hpR.reset(); lpL.z = lpR.z = 0.0;
        silent = 0; idle = true;
    }

    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        feed.setTarget (p.on ? 1.0 : 0.0);
        mixS.setTarget (std::clamp (p.mix, 0.0, 1.0));
        fbS.setTarget (std::clamp (p.feedback, 0.0, 0.95));
        const double target = std::clamp (p.timeMs, 1.0, kMaxSeconds * 1000.0) * 0.001 * sampleRate;
        lpL.set (std::clamp (p.toneHz, 800.0, 20000.0), sampleRate);
        lpR.a = lpL.a;

        if (! p.on && ! feed.isSmoothing() && feed.getCurrent() == 0.0 && idle) { timeSamples = target; return; }
        if (idle && p.on) { reset(); idle = false; timeSamples = target; }

        for (int i = 0; i < n; ++i)
        {
            timeSamples = target + glide * (timeSamples - target);
            const double d = std::max (2.0, timeSamples);
            const double wl = dl.read (d), wr = dr.read (d);
            const double fb = fbS.next(), g = feed.next(), m = mixS.next();
            const double fl = softLimit (lpL.process (hpL.process (wl))) * fb;
            const double fr = softLimit (lpR.process (hpR.process (wr))) * fb;
            const double inL = left[i] * g, inR = right[i] * g;
            if (p.pingPong)
            {
                dl.push (0.5 * (inL + inR) + fr); // input enters on the left and bounces across
                dr.push (fl);
            }
            else
            {
                dl.push (inL + fl);
                dr.push (inR + fr);
            }
            left[i] = static_cast<float> (left[i] + m * wl);
            right[i] = static_cast<float> (right[i] + m * wr);
            // idle detection once off and the repeats have died away (~1 s below -140 dB)
            if (g == 0.0 && std::fabs (wl) + std::fabs (wr) < 1e-7) { if (++silent > static_cast<int> (sampleRate)) idle = true; }
            else silent = 0;
        }
    }

private:
    /** Exactly linear up to full scale; above it, eases towards 2.0 so runaway feedback can't blow up. */
    static double softLimit (double x) noexcept
    {
        const double a = std::fabs (x);
        if (a <= 1.0) return x;
        return std::copysign (1.0 + std::tanh (a - 1.0), x);
    }

    fxdetail::DelayLine dl, dr;
    fxdetail::OnePole lpL, lpR;
    Svf hpL, hpR;
    LinearSmoother feed, mixS, fbS;
    double sampleRate = 48000.0, timeSamples = 0.0, glide = 0.0;
    int silent = 0;
    bool idle = true;
};

//==============================================================================
/** Stereo reverb: pre-delay -> input diffusion -> 8-line feedback delay network (Householder mixing,
    per-line damping and decay set from RT60, slow modulation against metallic ringing). Trails when switched off. */
class FdnReverb
{
public:
    static constexpr int kLines = 8;

    struct Params
    {
        bool on = false;
        double size = 0.7;      // 0..1 (room .. hall)
        double decayS = 2.2;    // RT60 in seconds
        double preDelayMs = 20.0;
        double toneHz = 7000.0; // damping
        double mix = 0.25;
    };

    void prepare (double fs)
    {
        sampleRate = fs;
        for (auto& l : lines) l.prepare (static_cast<int> (std::ceil ((kBaseMs[kLines - 1] * 1.0 + 3.0) * 0.001 * fs)));
        pre.prepare (static_cast<int> (std::ceil (0.26 * fs)));
        for (int k = 0; k < 4; ++k)
        {
            apL[static_cast<size_t> (k)].prepare (static_cast<int> (std::ceil (kApMs[static_cast<size_t> (k)] * 0.001 * fs * 1.07)) + 4);
            apR[static_cast<size_t> (k)].prepare (static_cast<int> (std::ceil (kApMs[static_cast<size_t> (k)] * 0.001 * fs * 1.07)) + 4);
        }
        feed.prepare (fs, 0.020); feed.snap (0.0);
        mixS.prepare (fs, 0.030); mixS.snap (0.0);
        sizeS = 0.7;
        sizeGlide = std::exp (-1.0 / (0.25 * fs));
        preS = 0.02 * fs;
        for (auto* f : { &lowCutL, &lowCutR }) { f->setSampleRate (fs); f->set (Svf::Type::HighPass, 80.0, 0.7071); }
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.clear();
        pre.clear();
        for (auto& a : apL) a.clear();
        for (auto& a : apR) a.clear();
        for (auto& d : damp) d.z = 0.0;
        lowCutL.reset(); lowCutR.reset();
        lfoPhase = 0.0; silent = 0; idle = true;
    }

    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        feed.setTarget (p.on ? 1.0 : 0.0);
        mixS.setTarget (std::clamp (p.mix, 0.0, 1.0));
        if (! p.on && ! feed.isSmoothing() && feed.getCurrent() == 0.0 && idle) return;
        if (idle && p.on) { reset(); idle = false; sizeS = sizeScale (p.size); }

        const double sizeTarget = sizeScale (p.size);
        const double rt60 = std::clamp (p.decayS, 0.2, 20.0);
        const double preTarget = std::clamp (p.preDelayMs, 0.0, 250.0) * 0.001 * sampleRate;
        for (auto& d : damp) d.set (std::clamp (p.toneHz, 1000.0, 18000.0), sampleRate);
        const double lfoInc = 2.0 * 3.14159265358979323846 * 0.37 / sampleRate;
        const double modDepth = 0.0007 * sampleRate; // ±0.7 ms

        // decay gain per line for the current size (recomputed per block; size glides slowly)
        std::array<double, kLines> len {}, g {};
        auto computeGains = [&]
        {
            for (int k = 0; k < kLines; ++k)
            {
                len[static_cast<size_t> (k)] = kBaseMs[static_cast<size_t> (k)] * sizeS * 0.001 * sampleRate;
                g[static_cast<size_t> (k)] = std::pow (10.0, -3.0 * len[static_cast<size_t> (k)] / (rt60 * sampleRate));
            }
        };
        computeGains();

        for (int i = 0; i < n; ++i)
        {
            if ((i & 31) == 0 && std::fabs (sizeS - sizeTarget) > 1e-6) computeGains();
            sizeS = sizeTarget + sizeGlide * (sizeS - sizeTarget);
            preS = preTarget + sizeGlide * (preS - preTarget);
            const double fg = feed.next(), m = mixS.next();

            // pre-delay (mono sum into the tank) and input diffusion, separate for L/R
            pre.push (0.5 * (left[i] + right[i]) * fg);
            const double x = pre.read (std::max (2.0, preS));
            double xl = lowCutL.process (x), xr = lowCutR.process (x);
            for (int k = 0; k < 4; ++k)
            {
                xl = allpass (apL[static_cast<size_t> (k)], xl, kApMs[static_cast<size_t> (k)] * 0.001 * sampleRate, 0.62);
                xr = allpass (apR[static_cast<size_t> (k)], xr, kApMs[static_cast<size_t> (k)] * 1.07 * 0.001 * sampleRate, 0.62);
            }

            // FDN
            lfoPhase += lfoInc;
            if (lfoPhase > 6.283185307179586) lfoPhase -= 6.283185307179586;
            std::array<double, kLines> y {};
            double sum = 0.0;
            for (int k = 0; k < kLines; ++k)
            {
                const double mod = (k & 1 ? 1.0 : -1.0) * modDepth * std::sin (lfoPhase * (1.0 + 0.13 * k) + k);
                const double v = lines[static_cast<size_t> (k)].read (std::max (2.0, len[static_cast<size_t> (k)] + mod));
                y[static_cast<size_t> (k)] = damp[static_cast<size_t> (k)].process (v) * g[static_cast<size_t> (k)];
                sum += y[static_cast<size_t> (k)];
            }
            const double h = 2.0 / kLines * sum;
            double outL = 0.0, outR = 0.0;
            for (int k = 0; k < kLines; ++k)
            {
                const double fbv = y[static_cast<size_t> (k)] - h;           // Householder reflection: energy preserving
                const double in = (k & 1 ? xr : xl) * (k & 2 ? -1.0 : 1.0);
                lines[static_cast<size_t> (k)].push (fbv + in);
                if (k & 1) outR += (k & 4 ? -1.0 : 1.0) * y[static_cast<size_t> (k)];
                else       outL += (k & 4 ? -1.0 : 1.0) * y[static_cast<size_t> (k)];
            }
            outL *= 0.6; outR *= 0.6;
            left[i] = static_cast<float> (left[i] + m * outL);
            right[i] = static_cast<float> (right[i] + m * outR);

            if (fg == 0.0 && std::fabs (outL) + std::fabs (outR) < 1e-7) { if (++silent > static_cast<int> (sampleRate)) idle = true; }
            else silent = 0;
        }
    }

private:
    static constexpr std::array<double, kLines> kBaseMs { 31.7, 37.3, 41.9, 46.1, 53.9, 59.3, 67.1, 73.7 };
    static constexpr std::array<double, 4> kApMs { 4.77, 3.59, 12.73, 9.31 };
    static double sizeScale (double size) noexcept { return 0.35 + 0.65 * std::clamp (size, 0.0, 1.0); }

    static double allpass (fxdetail::DelayLine& d, double x, double delaySamples, double gcoef) noexcept
    {
        const double z = d.read (std::max (1.0, delaySamples));
        const double v = x + gcoef * z;
        d.push (v);
        return z - gcoef * v;
    }

    std::array<fxdetail::DelayLine, kLines> lines;
    std::array<fxdetail::OnePole, kLines> damp;
    fxdetail::DelayLine pre;
    std::array<fxdetail::DelayLine, 4> apL, apR;
    Svf lowCutL, lowCutR;
    LinearSmoother feed, mixS;
    double sampleRate = 48000.0, sizeS = 0.7, sizeGlide = 0.0, preS = 0.0, lfoPhase = 0.0;
    int silent = 0;
    bool idle = true;
};

} // namespace vfxa
