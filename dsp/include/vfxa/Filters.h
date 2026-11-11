// VFXAamps — modulation-safe filters (Simper / Cytomic trapezoidal SVF), 64-bit state.
#pragma once

#include <algorithm>
#include <cmath>

namespace vfxa
{

class Svf
{
public:
    enum class Type { LowPass, HighPass, Bell, LowShelf, HighShelf };

    void setSampleRate (double fs) noexcept { sampleRate = fs; }
    void reset() noexcept { ic1 = ic2 = 0.0; }

    /** Recomputes coefficients; safe to call every few samples while audio runs (no clicks). */
    void set (Type type, double freq, double q, double gainDb = 0.0) noexcept
    {
        freq = std::clamp (freq, 5.0, 0.49 * sampleRate);
        const double A = std::pow (10.0, gainDb / 40.0);
        double g = std::tan (pi * freq / sampleRate);
        const double k0 = 1.0 / q;
        double k = k0;
        switch (type)
        {
            case Type::LowPass:  m0 = 0; m1 = 0; m2 = 1; break;
            case Type::HighPass: m0 = 1; m1 = -k; m2 = -1; break;
            case Type::Bell:     k = k0 / A; m0 = 1; m1 = k * (A * A - 1.0); m2 = 0; break;
            case Type::LowShelf: g /= std::sqrt (A); m0 = 1; m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
            case Type::HighShelf:g *= std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
        }
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        if (type == Type::HighPass) { m0 = 1; m1 = -k; m2 = -1; }
    }

    double process (double v0) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    double sampleRate = 48000.0;
    double a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
    double ic1 = 0, ic2 = 0;
};

/** Amp-style tone stack: bass / mid / treble + presence, knobs 0..10 with 5 = flat. */
class ToneStack
{
public:
    void prepare (double fs) noexcept
    {
        for (auto* f : { &bass, &mid, &treble, &presence }) { f->setSampleRate (fs); f->reset(); }
        update (5, 5, 5, 5);
    }

    void reset() noexcept { for (auto* f : { &bass, &mid, &treble, &presence }) f->reset(); }

    /** Knob values 0..10 (already smoothed by the caller). */
    void update (double b, double m, double t, double p) noexcept
    {
        bass.set (Svf::Type::LowShelf, 110.0, 0.66, knobToDb (b, 12.0));
        mid.set (Svf::Type::Bell, 650.0, 0.75, knobToDb (m, 10.0));
        treble.set (Svf::Type::HighShelf, 2800.0, 0.66, knobToDb (t, 12.0));
        presence.set (Svf::Type::Bell, 5200.0, 0.9, knobToDb (p, 9.0));
    }

    double process (double x) noexcept { return presence.process (treble.process (mid.process (bass.process (x)))); }

    static double knobToDb (double knob, double range) noexcept { return (std::clamp (knob, 0.0, 10.0) - 5.0) / 5.0 * range; }

private:
    Svf bass, mid, treble, presence;
};

/** Low-cut (12 dB/oct) + high-cut (12 dB/oct) pair. */
class CutFilters
{
public:
    void prepare (double fs) noexcept { hp.setSampleRate (fs); lp.setSampleRate (fs); reset(); update (20.0, 20000.0); }
    void reset() noexcept { hp.reset(); lp.reset(); }
    void update (double lowCutHz, double highCutHz) noexcept
    {
        hp.set (Svf::Type::HighPass, lowCutHz, 0.7071);
        lp.set (Svf::Type::LowPass, highCutHz, 0.7071);
    }
    double process (double x) noexcept { return lp.process (hp.process (x)); }

    /** With engagement amounts 0..1: at 0 a filter is bypassed (exactly transparent), at 1 fully in. */
    double process (double x, double lowAmount, double highAmount) noexcept
    {
        const double h = hp.process (x);
        const double a = x + lowAmount * (h - x);
        const double l = lp.process (a);
        return a + highAmount * (l - a);
    }

private:
    Svf hp, lp;
};

} // namespace vfxa
