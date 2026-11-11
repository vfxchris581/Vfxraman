// VFXAamps — per-sample parameter smoothing (64-bit).
#pragma once

#include <cmath>

namespace vfxa
{

/** Linear ramp to a target over a fixed time; reaches the target exactly (no asymptotic tail). */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = rampSeconds * sampleRate > 1.0 ? static_cast<int> (std::lround (rampSeconds * sampleRate)) : 1;
        snap (target);
    }

    void snap (double v) noexcept { current = target = v; stepsLeft = 0; step = 0.0; }

    void setTarget (double v) noexcept
    {
        if (v == target) return;
        target = v;
        stepsLeft = rampLength;
        step = (target - current) / static_cast<double> (rampLength);
    }

    double next() noexcept
    {
        if (stepsLeft > 0)
        {
            current += step;
            if (--stepsLeft == 0) current = target;
        }
        return current;
    }

    /** Advance n samples at once (used when a block's values are not needed sample by sample). */
    void skip (int n) noexcept
    {
        if (stepsLeft <= 0) return;
        if (n >= stepsLeft) { current = target; stepsLeft = 0; return; }
        current += step * n;
        stepsLeft -= n;
    }

    double getCurrent() const noexcept { return current; }
    double getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return stepsLeft > 0; }
    /** Value the ramp will have after n more samples. */
    double peek (int n) const noexcept { return n >= stepsLeft ? target : current + step * n; }

private:
    double current = 0.0, target = 0.0, step = 0.0;
    int stepsLeft = 0, rampLength = 1;
};

/** Smoothed gain set in decibels, ramped linearly in the gain domain. */
class GainSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept { s.prepare (sampleRate, rampSeconds); }
    void snapDb (double db) noexcept { s.snap (dbToGain (db)); }
    void setDb (double db) noexcept { s.setTarget (dbToGain (db)); }
    void setGain (double g) noexcept { s.setTarget (g); }
    void snapGain (double g) noexcept { s.snap (g); }
    double next() noexcept { return s.next(); }
    void skip (int n) noexcept { s.skip (n); }
    bool isSmoothing() const noexcept { return s.isSmoothing(); }
    double getCurrent() const noexcept { return s.getCurrent(); }
    double getTarget() const noexcept { return s.getTarget(); }

    static double dbToGain (double db) noexcept { return db <= -120.0 ? 0.0 : std::pow (10.0, db / 20.0); }

private:
    LinearSmoother s;
};

} // namespace vfxa
