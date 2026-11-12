// VFXAamps — post-amp effects: 10-band graphic EQ and shared building blocks (delay lines, one-poles).
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

} // namespace vfxa
