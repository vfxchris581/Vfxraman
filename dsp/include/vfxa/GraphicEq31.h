// VFXAamps — VX-31 dual 31-band graphic EQ / limiter.
//
// Per channel: input gain (+/-12 dB) -> low cut (40 Hz, 18 dB/oct Bessel high-pass) -> noise reduction (up to 10 dB
// broadband downward expansion) -> 31 constant-Q 1/3-octave bands on the ISO centres (+/-6 or +/-15 dB) -> peak
// limiter (fast, with an absolute ceiling). Channel A = left, B = right; LINK runs B on A's settings.
// Off, or on with everything flat / neutral: bit-exact passthrough. 64-bit; nothing allocated in process().
#pragma once

#include "vfxa/Filters.h"
#include "vfxa/Smoothers.h"

#include <array>
#include <atomic>
#include <cmath>

namespace vfxa
{

class GraphicEq31
{
public:
    static constexpr int kBands = 31;
    static constexpr std::array<double, kBands> kFreqs { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
                                                         800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
    /** Constant Q for a 1/3-octave band: sqrt(2^N) / (2^N - 1), N = 1/3. Bandwidth is the same at every fader setting. */
    static double bandQ() noexcept { const double r = std::pow (2.0, 1.0 / 3.0); return std::sqrt (r) / (r - 1.0); }

    struct Channel
    {
        std::array<double, kBands> gainDb {};
        double inputDb = 0.0;
        bool lowCut = false, limit = false, nr = false;
        double thresholdDb = -6.0; // limiter, dBFS
    };
    struct Params
    {
        bool on = false, link = true;
        std::array<Channel, 2> ch;
    };

    void prepare (double fs) noexcept
    {
        sampleRate = fs;
        for (auto& c : chans) c.prepare (fs);
        onS.prepare (fs, 0.020); onS.snap (0.0);
        idle = true;
    }

    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        onS.setTarget (p.on ? 1.0 : 0.0);
        if (! p.on && ! onS.isSmoothing() && onS.getCurrent() == 0.0)
        {
            if (! idle) { idle = true; for (auto& c : chans) c.resetMeters(); }
            return;
        }
        if (idle) { idle = false; for (int k = 0; k < 2; ++k) chans[static_cast<size_t> (k)].start (p.ch[static_cast<size_t> (p.link ? 0 : k)]); }
        float* io[2] { left, right };
        for (int k = 0; k < 2; ++k)
            chans[static_cast<size_t> (k)].process (io[k], n, p.ch[static_cast<size_t> (p.link ? 0 : k)], onS);
        onS.skip (n);
    }

    // meters (any thread)
    float getOutputDb (int ch) const noexcept { return chans[static_cast<size_t> (ch)].outDb.load (std::memory_order_relaxed); }
    float getGainReductionDb (int ch) const noexcept { return chans[static_cast<size_t> (ch)].grDb.load (std::memory_order_relaxed); }
    float getNoiseReductionDb (int ch) const noexcept { return chans[static_cast<size_t> (ch)].nrDb.load (std::memory_order_relaxed); }

private:
    struct OnePoleHp
    {
        double a = 0.0, x1 = 0.0, y1 = 0.0;
        void set (double fc, double fs) noexcept { const double k = std::tan (3.14159265358979323846 * fc / fs); a = (1.0 - k) / (1.0 + k); }
        void reset() noexcept { x1 = y1 = 0.0; }
        double process (double x) noexcept { const double y = 0.5 * (1.0 + a) * (x - x1) + a * y1; x1 = x; y1 = y; return y; }
    };

    struct ChannelDsp
    {
        double fs = 48000.0;
        std::array<Svf, kBands> bands;
        std::array<LinearSmoother, kBands> gains;
        GainSmoother input;
        LinearSmoother lowCutMix, limitMix, nrMix;
        Svf hp2;
        OnePoleHp hp1;
        double nrEnv = 0.0, nrGain = 1.0, limGain = 1.0, meter = 0.0;
        double aNrAtt = 0, aNrRel = 0, aNrGain = 0, aLimAtt = 0, aLimRel = 0, aMeter = 0;
        int countdown = 0;
        bool eqIdle = true;
        std::atomic<float> outDb { -100.0f }, grDb { 0.0f }, nrDb { 0.0f };

        void prepare (double sr) noexcept
        {
            fs = sr;
            for (auto& b : bands) { b.setSampleRate (fs); b.reset(); }
            for (auto& g : gains) { g.prepare (fs, 0.040); g.snap (0.0); }
            input.prepare (fs, 0.030); input.snapDb (0.0);
            for (auto* m : { &lowCutMix, &limitMix, &nrMix }) { m->prepare (fs, 0.030); m->snap (0.0); }
            // 3rd-order Bessel high-pass, -3 dB at 40 Hz. Bessel LP 15 / (s^3 + 6s^2 + 15s + 15), rescaled so -3 dB is at
            // w = 1 (divide by 1.75567): poles -1.32268 and -1.04741 +/- j0.99926. As a high-pass at 40 Hz that is a
            // 1st-order section at 40 / 1.32268 Hz and a 2nd-order one at 40 / 1.44759 Hz with Q 0.69105.
            hp2.setSampleRate (fs);
            hp2.set (Svf::Type::HighPass, 40.0 / 1.44759, 0.69105);
            hp1.set (40.0 / 1.32268, fs);
            hp2.reset(); hp1.reset();
            auto c = [this] (double s) { return std::exp (-1.0 / (s * fs)); };
            aNrAtt = c (0.005); aNrRel = c (0.120); aNrGain = c (0.030);
            aLimAtt = c (0.0002); aLimRel = c (0.150); aMeter = c (0.300);
            nrEnv = 0.0; nrGain = 1.0; limGain = 1.0; meter = 0.0;
            eqIdle = true;
            resetMeters();
        }

        void resetMeters() noexcept { outDb.store (-100.0f); grDb.store (0.0f); nrDb.store (0.0f); }

        void start (const Channel& c) noexcept
        {
            for (int b = 0; b < kBands; ++b) gains[static_cast<size_t> (b)].snap (std::clamp (c.gainDb[static_cast<size_t> (b)], -15.0, 15.0));
            input.snapDb (std::clamp (c.inputDb, -12.0, 12.0));
            lowCutMix.snap (c.lowCut ? 1.0 : 0.0);
            limitMix.snap (c.limit ? 1.0 : 0.0);
            nrMix.snap (c.nr ? 1.0 : 0.0);
            for (auto& b : bands) b.reset();
            hp2.reset(); hp1.reset();
            nrEnv = 0.0; nrGain = 1.0; limGain = 1.0;
            eqIdle = true;
            countdown = 0;
        }

        void updateCoefficients() noexcept
        {
            const double q = bandQ();
            for (int b = 0; b < kBands; ++b)
                bands[static_cast<size_t> (b)].set (Svf::Type::Bell, kFreqs[static_cast<size_t> (b)], q,
                                                    gains[static_cast<size_t> (b)].getCurrent());
        }

        void process (float* x, int n, const Channel& c, const LinearSmoother& onShared) noexcept
        {
            for (int b = 0; b < kBands; ++b) gains[static_cast<size_t> (b)].setTarget (std::clamp (c.gainDb[static_cast<size_t> (b)], -15.0, 15.0));
            input.setDb (std::clamp (c.inputDb, -12.0, 12.0));
            lowCutMix.setTarget (c.lowCut ? 1.0 : 0.0);
            limitMix.setTarget (c.limit ? 1.0 : 0.0);
            nrMix.setTarget (c.nr ? 1.0 : 0.0);
            const double thr = std::pow (10.0, std::clamp (c.thresholdDb, -30.0, 0.0) / 20.0);

            bool eqActive = false;
            for (auto& g : gains) eqActive |= g.isSmoothing() || g.getCurrent() != 0.0;
            if (eqActive && eqIdle) { for (auto& b : bands) b.reset(); eqIdle = false; countdown = 0; }
            if (! eqActive) eqIdle = true;

            LinearSmoother on = onShared; // each channel steps its own copy of the shared on/off fade
            double minLim = 1.0, maxNr = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double dry = x[i];
                double v = dry * input.next();

                // low cut (crossfaded in / out)
                const double lc = lowCutMix.next();
                if (lc > 0.0) { const double h = hp1.process (hp2.process (v)); v += lc * (h - v); }
                else { hp2.reset(); hp1.reset(); }

                // noise reduction: below -58 dBFS, 1:2 downward expansion, at most 10 dB
                const double nm = nrMix.next();
                if (nm > 0.0)
                {
                    const double a = std::fabs (v);
                    nrEnv = a > nrEnv ? a + aNrAtt * (nrEnv - a) : a + aNrRel * (nrEnv - a);
                    constexpr double knee = 0.00126; // -58 dBFS
                    const double want = nrEnv >= knee ? 1.0 : std::max (0.316, nrEnv / knee); // 1:2 below the knee, floor -10 dB
                    nrGain = want + aNrGain * (nrGain - want);
                    v *= 1.0 + nm * (nrGain - 1.0);
                    maxNr = std::max (maxNr, -20.0 * std::log10 (nrGain));
                }

                // 31 bands
                if (eqActive)
                {
                    if (--countdown <= 0)
                    {
                        countdown = 16;
                        updateCoefficients();
                    }
                    for (auto& g : gains) g.next();
                    for (auto& b : bands) v = b.process (v);
                }

                // peak limiter: near-instant attack, 150 ms release, absolute ceiling 1 dB above the threshold
                const double lm = limitMix.next();
                if (lm > 0.0)
                {
                    const double a = std::fabs (v);
                    const double want = a > thr ? thr / a : 1.0;
                    limGain = want < limGain ? want + aLimAtt * (limGain - want) : want + aLimRel * (limGain - want);
                    double y = v * limGain;
                    const double ceiling = thr * 1.122;
                    if (std::fabs (y) > thr) // soft ceiling between thr and thr + 1 dB
                    {
                        const double over = (std::fabs (y) - thr) / (ceiling - thr);
                        y = std::copysign (thr + (ceiling - thr) * std::tanh (over), y);
                    }
                    v += lm * (y - v);
                    minLim = std::min (minLim, limGain);
                }

                const double o = on.next();
                const double out = dry + o * (v - dry);
                x[i] = static_cast<float> (out);
                const double lvl = std::fabs (out);
                meter = lvl > meter ? lvl : lvl + aMeter * (meter - lvl);
            }
            outDb.store (static_cast<float> (20.0 * std::log10 (std::max (1e-5, meter))), std::memory_order_relaxed);
            grDb.store (static_cast<float> (-20.0 * std::log10 (std::max (1e-6, minLim))), std::memory_order_relaxed);
            nrDb.store (static_cast<float> (maxNr), std::memory_order_relaxed);
        }

    };

    double sampleRate = 48000.0;
    std::array<ChannelDsp, 2> chans;
    LinearSmoother onS;
    bool idle = true;
};

} // namespace vfxa
