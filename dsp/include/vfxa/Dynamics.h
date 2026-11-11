// VFXAamps — noise gate and zero-latency output limiter (64-bit).
#pragma once

#include <algorithm>
#include <cmath>

namespace vfxa
{

/** Downward gate with hysteresis, hold and smooth (click-free) attack/release. */
class NoiseGate
{
public:
    void prepare (double fs) noexcept
    {
        sampleRate = fs;
        envCoef = std::exp (-1.0 / (0.002 * fs)); // 2 ms peak detector release
        attackStep = 1.0 / (0.0008 * fs);          // fully open in 0.8 ms
        holdSamples = static_cast<int> (0.025 * fs);
        setRelease (80.0);
        reset();
    }

    void reset() noexcept { env = 0.0; gain = 0.0; open = false; hold = 0; }

    void setThresholdDb (double db) noexcept
    {
        openThresh = std::pow (10.0, db / 20.0);
        closeThresh = std::pow (10.0, (db - 6.0) / 20.0); // 6 dB hysteresis
    }

    void setRelease (double ms) noexcept { releaseCoef = std::exp (-1.0 / (std::max (1.0, ms) * 0.001 * sampleRate)); }

    /** Returns the gain to apply to this sample (the caller decides whether the gate is enabled). */
    double process (double x) noexcept
    {
        const double a = std::fabs (x);
        env = a > env ? a : env * envCoef + a * (1.0 - envCoef);

        if (env >= openThresh) { open = true; hold = holdSamples; }
        else if (env < closeThresh && open) { if (hold > 0) --hold; else open = false; }
        else if (hold > 0) --hold;

        if (open) gain = std::min (1.0, gain + attackStep);
        else gain *= releaseCoef;
        if (gain < 1e-6) gain = 0.0;
        return gain;
    }

    double currentGain() const noexcept { return gain; }

private:
    double sampleRate = 48000.0, env = 0.0, gain = 0.0;
    double envCoef = 0.0, attackStep = 1.0, releaseCoef = 0.0;
    double openThresh = 0.001, closeThresh = 0.0005;
    int holdSamples = 0, hold = 0;
    bool open = false;
};

/**
    Zero-latency stereo-linked limiter: instant-attack peak gain computer with smooth release,
    followed by a soft knee that keeps the output under the ceiling (benefits from oversampling).
*/
class Limiter
{
public:
    void prepare (double fs) noexcept
    {
        releaseCoef = std::exp (-1.0 / (0.08 * fs));
        reset();
    }
    void reset() noexcept { gain = 1.0; }
    void setCeilingDb (double db) noexcept { ceiling = std::pow (10.0, db / 20.0); }

    void process (double& l, double& r) noexcept
    {
        const double peak = std::max (std::fabs (l), std::fabs (r));
        const double wanted = peak > ceiling ? ceiling / peak : 1.0;
        gain = wanted < gain ? wanted : wanted + (gain - wanted) * releaseCoef;
        l = softClip (l * gain);
        r = softClip (r * gain);
    }

    double gainReduction() const noexcept { return gain; }

private:
    double softClip (double x) const noexcept
    {
        // Linear below 85% of the ceiling, smooth tanh knee above, never exceeds the ceiling.
        const double knee = 0.85 * ceiling;
        const double a = std::fabs (x);
        if (a <= knee) return x;
        const double range = ceiling - knee;
        const double y = knee + range * std::tanh ((a - knee) / range);
        return x < 0 ? -y : y;
    }

    double gain = 1.0, releaseCoef = 0.999, ceiling = 0.966; // -0.3 dBFS
};

} // namespace vfxa
