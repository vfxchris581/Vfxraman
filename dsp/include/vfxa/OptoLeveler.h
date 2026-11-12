// VFXAamps — VX-O2 tube opto leveling amplifier.
//
// Feedback leveling amp: the detector listens to the attenuated signal (so the gain reduction settles the way an
// optical cell in a feedback loop does), and drives a model of an electro-luminescent panel + photoresistor:
//   - the panel follows the detector with a ~10 ms attack and a fast ~60 ms decay,
//   - the photoresistor also builds up a slow "memory" while it is lit for long, which decays over 1-5 s,
//   so release is two-stage and program-dependent: short peaks let go quickly, long passages recover slowly.
// The light sets the attenuation (g = 1 / (1 + L^gamma)); COMPRESS (~3:1, very soft knee) or LIMIT (~7:1, firmer).
// After the attenuator: makeup gain and a tube output stage (asymmetric soft saturation, mostly 2nd harmonic,
// unity small-signal gain). Optional HF emphasis in the detector, parallel mix.
// Mono or stereo-linked. 64-bit; nothing allocated or locked in process(). Off = bit-exact passthrough.
#pragma once

#include "vfxa/Filters.h"
#include "vfxa/Smoothers.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace vfxa
{

class OptoLeveler
{
public:
    struct Params
    {
        bool active = false;       // on, and at this position in the chain
        double peakReduction = 0.4;// 0..1
        double gainDb = 0.0;       // makeup, -12..+24
        bool limit = false;
        double emphasis = 0.0;     // 0..1: detector high-shelf 0..+12 dB at 3 kHz
        double mix = 1.0;          // 0..1 (parallel)
    };

    void prepare (double fs) noexcept
    {
        sampleRate = fs;
        amount.prepare (fs, 0.020); amount.snap (0.0);
        makeup.prepare (fs, 0.030);
        mixS.prepare (fs, 0.030);
        scGain.prepare (fs, 0.030);
        gammaS.prepare (fs, 0.050);
        emph.setSampleRate (fs);
        aAtt = coeff (0.010);
        aFast = coeff (0.085);
        dcCoef = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 1.0 / fs); // 1 Hz DC blocker
        aCharge = coeff (1.2);
        aMeter = coeff (0.300);
        reset();
    }

    void reset() noexcept
    {
        fast = slow = 0.0;
        gain = 1.0;
        dcL = dcR = 0.0;
        emph.reset();
        grDb.store (0.0f, std::memory_order_relaxed);
        meterOut = 0.0;
        outDb.store (-100.0f, std::memory_order_relaxed);
    }

    /** right may be nullptr (mono). */
    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        amount.setTarget (p.active ? 1.0 : 0.0);
        if (! p.active && ! amount.isSmoothing() && amount.getCurrent() == 0.0)
        {
            if (! idle) { reset(); idle = true; }
            return;
        }
        if (idle) { reset(); idle = false; snapControls (p); }

        makeup.setDb (std::clamp (p.gainDb, -12.0, 24.0));
        mixS.setTarget (std::clamp (p.mix, 0.0, 1.0));
        // PEAK REDUCTION -> detector gain: 0 = no compression, 1 = +34 dB into the cell
        const double pr = std::clamp (p.peakReduction, 0.0, 1.0);
        scGain.setTarget (pr <= 0.0 ? 0.0 : std::pow (10.0, (-6.0 + 40.0 * pr) / 20.0));
        gammaS.setTarget (p.limit ? kGammaLimit : kGammaCompress);
        emph.set (Svf::Type::HighShelf, 3000.0, 0.7071, 12.0 * std::clamp (p.emphasis, 0.0, 1.0));

        double minG = 1.0;
        for (int i = 0; i < n; ++i)
        {
            const double xl = left[i], xr = right != nullptr ? right[i] : xl;
            const double a = amount.next(), mk = makeup.next(), mx = mixS.next(), sg = scGain.next(), gm = gammaS.next();

            // feedback detector: the attenuated signal (before makeup), optionally HF-emphasised
            const double att = gain;
            const double yl = xl * att, yr = xr * att;
            const double d = std::fabs (emph.process (0.5 * (yl + yr))) * 0.5 + 0.5 * std::max (std::fabs (yl), std::fabs (yr));
            const double drive = d * sg;

            // electro-luminescent panel: 10 ms rise, 60 ms fall
            fast = drive > fast ? drive + aAtt * (fast - drive) : drive + aFast * (fast - drive);
            // photoresistor memory: charges slowly while lit, discharges over 1-5 s (slower the longer it was charged)
            if (fast > slow) slow = fast + aCharge * (slow - fast);
            else
            {
                const double tau = 1.0 + 4.0 * std::min (1.0, slow / (fast + 1e-9) - 1.0) * std::min (1.0, slow * 2.0);
                slow = fast + std::exp (-1.0 / (tau * sampleRate)) * (slow - fast);
            }
            const double light = 0.55 * fast + 0.45 * slow;
            gain = 1.0 / (1.0 + std::pow (light, gm));
            minG = std::min (minG, gain);

            // makeup + tube output stage
            const double wl = tube (yl * mk, dcL), wr = tube (yr * mk, dcR);
            const double ol = xl + mx * (wl - xl), orr = xr + mx * (wr - xr);
            const double fl = xl + a * (ol - xl), fr = xr + a * (orr - xr);
            left[i] = static_cast<float> (fl);
            if (right != nullptr) right[i] = static_cast<float> (fr);

            const double lvl = std::max (std::fabs (fl), std::fabs (fr));
            meterOut = lvl > meterOut ? lvl : lvl + aMeter * (meterOut - lvl);
        }
        grDb.store (static_cast<float> (-20.0 * std::log10 (std::max (1e-6, minG))), std::memory_order_relaxed);
        outDb.store (static_cast<float> (20.0 * std::log10 (std::max (1e-5, meterOut))), std::memory_order_relaxed);
    }

    /** Current gain reduction in dB (>= 0) and output peak level in dBFS, for the VU meter (any thread). */
    float getGainReductionDb() const noexcept { return grDb.load (std::memory_order_relaxed); }
    float getOutputDb() const noexcept { return outDb.load (std::memory_order_relaxed); }
    bool isActive() const noexcept { return ! idle; }

private:
    double coeff (double seconds) const noexcept { return std::exp (-1.0 / (seconds * sampleRate)); }

    void snapControls (const Params& p) noexcept
    {
        makeup.snapDb (std::clamp (p.gainDb, -12.0, 24.0));
        mixS.snap (std::clamp (p.mix, 0.0, 1.0));
        const double pr = std::clamp (p.peakReduction, 0.0, 1.0);
        scGain.snap (pr <= 0.0 ? 0.0 : std::pow (10.0, (-6.0 + 40.0 * pr) / 20.0));
        gammaS.snap (p.limit ? kGammaLimit : kGammaCompress);
    }

    /** Tube stage: asymmetric soft saturation (mostly 2nd harmonic), unity gain for small signals, DC removed. */
    double tube (double x, double& dc) noexcept
    {
        // gentle: ~0.1 % THD at -18 dBFS (nominal level), ~0.25 % at -12 dBFS, rising smoothly when driven hard
        constexpr double bias = 0.05, drive = 0.35;
        static const double t0 = std::tanh (bias), slope = 1.0 - t0 * t0;
        const double y = (std::tanh (drive * x + bias) - t0) / (drive * slope);
        dc += dcCoef * (y - dc);
        return y - dc;
    }

    double sampleRate = 48000.0;
    LinearSmoother amount, mixS, scGain, gammaS;
    GainSmoother makeup;
    Svf emph;
    static constexpr double kGammaCompress = 2.4, kGammaLimit = 11.0; // ~3:1 soft knee / ~10:1+ firm
    double aAtt = 0.0, aFast = 0.0, aCharge = 0.0, aMeter = 0.0, dcCoef = 0.0;
    double fast = 0.0, slow = 0.0, gain = 1.0, dcL = 0.0, dcR = 0.0, meterOut = 0.0;
    std::atomic<float> grDb { 0.0f }, outDb { -100.0f };
    bool idle = true;
};

} // namespace vfxa
