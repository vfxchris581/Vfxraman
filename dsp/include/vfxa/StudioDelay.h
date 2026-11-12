// VFXAamps — VX-D8 studio delay.
//
// Stereo multi-mode delay. One feedback loop per channel: read (time with tape-style glide, modulation) ->
// mode voicing (tape wow / flutter + saturation, analog darkening, chorus, reverse grains) -> low cut / high cut ->
// optional diffusion (allpass smear) -> feedback (ping-pong or dual-time optional). Ducking lowers the repeats while
// you play. Trails ring out when switched off. 64-bit, buffers allocated in prepare(), no allocation in process().
#pragma once

#include "vfxa/Filters.h"
#include "vfxa/PostFx.h"
#include "vfxa/Smoothers.h"

#include <array>
#include <cmath>

namespace vfxa
{

class StudioDelay
{
public:
    static constexpr double kMaxSeconds = 2.5;
    enum Mode { Digital = 0, Tape, Analog, Modulated, Reverse, Dual, kNumModes };
    static const char* modeName (int m) noexcept
    {
        static const char* names[] { "DIGITAL", "TAPE", "ANALOG", "MOD", "REVERSE", "DUAL" };
        return names[std::clamp (m, 0, kNumModes - 1)];
    }

    struct Params
    {
        bool on = false, pingPong = false;
        int mode = Digital;
        double timeMs = 400.0, feedback = 0.35, mix = 0.3;
        double lowCutHz = 60.0, highCutHz = 12000.0;
        double modDepth = 0.15, modRate = 0.6; // 0..1, Hz
        double duck = 0.0, diffusion = 0.0;    // 0..1
    };

    void prepare (double fs)
    {
        sampleRate = fs;
        // reverse reads back to 2 x the time; dual runs the right side at 1.5 x
        const int max = static_cast<int> (std::ceil (2.0 * kMaxSeconds * fs * 1.05)) + 16;
        for (auto& c : ch) { c.buf.prepare (max); for (auto& a : c.ap) a.prepare (static_cast<int> (std::ceil (0.018 * fs)) + 4); }
        feed.prepare (fs, 0.020); feed.snap (0.0);
        mixS.prepare (fs, 0.030); mixS.snap (0.0);
        fbS.prepare (fs, 0.030); fbS.snap (0.0);
        diffS.prepare (fs, 0.050); diffS.snap (0.0);
        modeFade.prepare (fs, 0.015); modeFade.snap (1.0);
        hpOn.prepare (fs, 0.030); hpOn.snap (0.0);
        lpOn.prepare (fs, 0.030); lpOn.snap (0.0);
        glide = std::exp (-1.0 / (0.12 * fs));
        envAtt = std::exp (-1.0 / (0.002 * fs));
        envRel = std::exp (-1.0 / (0.20 * fs));
        duckSmooth = std::exp (-1.0 / (0.015 * fs));
        currentMode = Digital;
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : ch)
        {
            c.buf.clear();
            for (auto& a : c.ap) a.clear();
            c.hp.reset(); c.lp.reset(); c.lp2.reset(); c.tapeLp.z = 0.0;
            c.time = 0.4 * sampleRate;
        }
        env = 0.0; duckGain = 1.0; revPhase = 0.0; lfo = 0.0; wow = 0.0; flutter = 0.0;
        silent = 0; idle = true;
    }

    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        feed.setTarget (p.on ? 1.0 : 0.0);
        mixS.setTarget (std::clamp (p.mix, 0.0, 1.0));
        fbS.setTarget (std::clamp (p.feedback, 0.0, 0.98));
        diffS.setTarget (std::clamp (p.diffusion, 0.0, 1.0));
        const double target = std::clamp (p.timeMs, 1.0, kMaxSeconds * 1000.0) * 0.001 * sampleRate;
        const int mode = std::clamp (p.mode, 0, kNumModes - 1);

        if (! p.on && ! feed.isSmoothing() && feed.getCurrent() == 0.0 && idle) return;
        if (idle && p.on)
        {
            reset();
            idle = false;
            currentMode = mode;
            ch[0].time = target;
            ch[1].time = target * (mode == Dual ? 1.5 : 1.0);
        }
        // mode change: the repeats fade out briefly while the voicing switches
        if (mode != currentMode)
        {
            modeFade.setTarget (0.0);
            if (! modeFade.isSmoothing() && modeFade.getCurrent() <= 0.0) { currentMode = mode; modeFade.setTarget (1.0); }
        }

        // voicing per mode
        double hiCut = std::clamp (p.highCutHz, 800.0, 20000.0), loCut = std::clamp (p.lowCutHz, 20.0, 1000.0);
        double modMs = std::clamp (p.modDepth, 0.0, 1.0) * 0.6;      // gentle by default
        double sat = 0.0;
        switch (currentMode)
        {
            case Tape:      hiCut = std::min (hiCut, 7500.0); loCut = std::max (loCut, 70.0); sat = 1.0; break;
            case Analog:    hiCut = std::min (hiCut, 3200.0); loCut = std::max (loCut, 50.0); sat = 0.6; modMs *= 0.5; break;
            case Modulated: modMs = std::clamp (p.modDepth, 0.0, 1.0) * 4.0; break;
            default: break;
        }
        // at the ends of their ranges the loop filters switch out entirely (fading), so DIGITAL is truly clean
        hpOn.setTarget (loCut > 20.5 ? 1.0 : 0.0);
        lpOn.setTarget (hiCut < 19900.0 ? 1.0 : 0.0);
        for (auto& c : ch)
        {
            c.hp.setSampleRate (sampleRate); c.lp.setSampleRate (sampleRate); c.lp2.setSampleRate (sampleRate);
            c.hp.set (Svf::Type::HighPass, loCut, 0.7071);
            c.lp.set (Svf::Type::LowPass, hiCut, currentMode == Analog ? 0.6 : 0.7071);
            c.lp2.set (Svf::Type::LowPass, std::min (hiCut * 1.6, 0.45 * sampleRate), 0.5); // analog: steeper, darker
            c.tapeLp.set (12000.0, sampleRate);
        }
        const double modDepthSamples = modMs * 0.001 * sampleRate;
        const double lfoInc = 2.0 * 3.14159265358979323846 * std::clamp (p.modRate, 0.05, 10.0) / sampleRate;
        const double wowInc = 2.0 * 3.14159265358979323846 * 0.55 / sampleRate, flutterInc = 2.0 * 3.14159265358979323846 * 6.3 / sampleRate;
        const double duckAmt = std::clamp (p.duck, 0.0, 1.0);

        for (int i = 0; i < n; ++i)
        {
            const double g = feed.next(), m = mixS.next(), fb = fbS.next(), df = diffS.next(), mf = modeFade.next();
            const double hpAmt = hpOn.next(), lpAmt = lpOn.next();
            const double inL = left[i], inR = right[i];

            // ducking: repeats dip while the guitar plays, come back as it decays
            const double a = 0.5 * (std::fabs (inL) + std::fabs (inR));
            env = a > env ? a + envAtt * (env - a) : a + envRel * (env - a);
            const double wantDuck = 1.0 - duckAmt * std::clamp ((env - 0.006) / 0.09, 0.0, 1.0) * 0.92;
            duckGain = wantDuck + duckSmooth * (duckGain - wantDuck);

            lfo += lfoInc; if (lfo > 6.283185307179586) lfo -= 6.283185307179586;
            wow += wowInc; if (wow > 6.283185307179586) wow -= 6.283185307179586;
            flutter += flutterInc; if (flutter > 6.283185307179586) flutter -= 6.283185307179586;

            std::array<double, 2> rep {};
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[static_cast<size_t> (c)];
                const double tgt = target * ((currentMode == Dual && c == 1) ? 1.5 : 1.0);
                s.time = tgt + glide * (s.time - tgt);
                double d = s.time;
                d += modDepthSamples * std::sin (lfo + (c == 0 ? 0.0 : 1.5707963));
                if (currentMode == Tape)
                    d *= 1.0 + (0.0022 * std::sin (wow + 0.3 * c) + 0.0004 * std::sin (flutter)) * (0.5 + std::clamp (p.modDepth, 0.0, 1.0));

                double y;
                if (currentMode == Reverse)
                {
                    // two windowed read heads, half a segment apart, each sweeping backwards through the last segment
                    const double seg = std::max (64.0, s.time);
                    double out = 0.0;
                    for (int h = 0; h < 2; ++h)
                    {
                        double ph = std::fmod (revPhase + 0.5 * h, 1.0);
                        const double win = std::sin (3.14159265358979323846 * ph);
                        out += win * win * s.buf.read (2.0 * ph * seg + 2.0);
                    }
                    y = out;
                }
                else
                {
                    y = s.buf.read (std::max (2.0, d));
                }

                // loop voicing
                const double h = s.hp.process (y);
                const double a1 = y + hpAmt * (h - y);
                const double lo = s.lp.process (a1);
                double v = a1 + lpAmt * (lo - a1);
                if (currentMode == Analog) v = s.lp2.process (v);
                if (currentMode == Tape) v = s.tapeLp.process (v);
                if (sat > 0.0) v = saturate (v, sat);
                // diffusion: smear the repeats through a short allpass chain
                double dv = v;
                for (int k = 0; k < 4; ++k) dv = allpass (s.ap[static_cast<size_t> (k)], dv, kApMs[static_cast<size_t> (k)] * (c ? 1.07 : 1.0) * 0.001 * sampleRate, 0.62);
                v += df * (dv - v);
                rep[static_cast<size_t> (c)] = v;
            }

            if (currentMode == Reverse)
            {
                revPhase += 1.0 / std::max (64.0, ch[0].time);
                if (revPhase >= 1.0) revPhase -= 1.0;
            }

            // write: input + feedback (ping-pong crosses the channels)
            const double fl = safety (rep[0] * fb), fr = safety (rep[1] * fb);
            if (p.pingPong)
            {
                ch[0].buf.push (0.5 * (inL + inR) * g + fr);
                ch[1].buf.push (fl);
            }
            else
            {
                ch[0].buf.push (inL * g + fl);
                ch[1].buf.push (inR * g + fr);
            }

            const double wl = rep[0] * duckGain * mf, wr = rep[1] * duckGain * mf;
            left[i] = static_cast<float> (inL + m * wl);
            right[i] = static_cast<float> (inR + m * wr);

            if (g == 0.0 && std::fabs (rep[0]) + std::fabs (rep[1]) < 1e-7) { if (++silent > static_cast<int> (2.0 * sampleRate)) idle = true; }
            else silent = 0;
        }
    }

private:
    static constexpr std::array<double, 4> kApMs { 4.3, 7.1, 11.3, 15.7 };

    struct Channel
    {
        fxdetail::DelayLine buf;
        std::array<fxdetail::DelayLine, 4> ap;
        Svf hp, lp, lp2;
        fxdetail::OnePole tapeLp;
        double time = 0.0;
    };

    /** Exactly linear for small signals; tape / analog voicings round off peaks progressively. */
    static double saturate (double x, double amount) noexcept
    {
        const double k = 0.6 * amount;
        return std::tanh (k * x) / k;
    }
    /** Linear up to full scale; beyond, eases towards 2.0 so runaway feedback can't blow up. */
    static double safety (double x) noexcept
    {
        const double a = std::fabs (x);
        return a <= 1.0 ? x : std::copysign (1.0 + std::tanh (a - 1.0), x);
    }
    static double allpass (fxdetail::DelayLine& d, double x, double delaySamples, double g) noexcept
    {
        const double z = d.read (std::max (2.0, delaySamples));
        const double v = x + g * z;
        d.push (v);
        return z - g * v;
    }

    std::array<Channel, 2> ch;
    LinearSmoother feed, mixS, fbS, diffS, modeFade, hpOn, lpOn;
    int currentMode = Digital;
    double sampleRate = 48000.0, glide = 0.0;
    double env = 0.0, envAtt = 0.0, envRel = 0.0, duckGain = 1.0, duckSmooth = 0.0;
    double revPhase = 0.0, lfo = 0.0, wow = 0.0, flutter = 0.0;
    int silent = 0;
    bool idle = true;
};

} // namespace vfxa
