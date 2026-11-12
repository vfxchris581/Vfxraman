// VFXAamps — VX-R16 studio reverb.
//
// True-stereo algorithmic reverb: pre-delay -> program-specific early reflections (L and R) and input diffusion ->
// 16-line feedback delay network with Hadamard mixing. Every line carries a modulated fractional delay, an in-loop
// allpass for echo density, and first-order low / high shelves that set separate decay times below and above the
// program's crossovers (RT60 x LOW RT, RT60, RT60 x HIGH RT). The loop gain of every line is bounded below 1 at all
// frequencies (monotonic first-order shelves), so the tank is stable for any setting.
// 64-bit throughout; buffers are allocated in prepare(), nothing is allocated or locked in process().
#pragma once

#include "vfxa/Filters.h"
#include "vfxa/PostFx.h"
#include "vfxa/Smoothers.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace vfxa
{

class StudioReverb
{
public:
    static constexpr int kLines = 16;
    enum Program { Ambience = 0, Room, Chamber, Hall, Plate, Cathedral, kNumPrograms };
    static const char* programName (int p) noexcept
    {
        static const char* names[] { "AMBIENCE", "ROOM", "CHAMBER", "HALL", "PLATE", "CATHEDRAL" };
        return names[std::clamp (p, 0, kNumPrograms - 1)];
    }

    struct Params
    {
        bool on = false;
        int program = Hall;
        double decayS = 2.4;      // RT60 (mid band)
        double preDelayMs = 20.0;
        double size = 0.7;        // 0..1
        double diffusion = 0.75;  // 0..1
        double mod = 0.35;        // 0..1
        double lowRT = 1.1;       // x RT60 below the low crossover (0.5..2)
        double highRT = 0.5;      // x RT60 above the high crossover (0.1..1)
        double early = 0.5;       // early reflections level 0..1
        double lowCutHz = 80.0;   // wet
        double highCutHz = 9000.0;// wet
        double width = 1.0;       // 0 (mono) .. 1.5 (extra wide)
        double mix = 0.25;
    };

    void prepare (double fs)
    {
        sampleRate = fs;
        for (auto& l : lines) l.prepare (static_cast<int> (std::ceil ((kMaxLineMs + 3.0) * 0.001 * fs)));
        for (auto& a : loopAp) a.prepare (static_cast<int> (std::ceil (6.0 * 0.001 * fs)) + 4);
        for (auto* d : { &preL, &preR }) d->prepare (static_cast<int> (std::ceil (0.26 * fs)));
        for (auto* d : { &erL, &erR }) d->prepare (static_cast<int> (std::ceil (0.16 * fs)) + 4); // longest tap: 118 ms x 1.13 at full size
        for (auto& a : inApL) a.prepare (static_cast<int> (std::ceil (14.0 * 0.001 * fs)) + 4);
        for (auto& a : inApR) a.prepare (static_cast<int> (std::ceil (14.0 * 0.001 * fs)) + 4);
        for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR }) { f->setSampleRate (fs); f->reset(); }
        feed.prepare (fs, 0.020); feed.snap (0.0);
        mixS.prepare (fs, 0.030); mixS.snap (0.0);
        progFade.prepare (fs, 0.025); progFade.snap (1.0);
        rtS.prepare (fs, 0.050);
        glide = std::exp (-1.0 / (0.25 * fs));
        currentProgram = Hall;
        buildProgram (currentProgram);
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.clear();
        for (auto& a : loopAp) a.clear();
        preL.clear(); preR.clear(); erL.clear(); erR.clear();
        for (auto& a : inApL) a.clear();
        for (auto& a : inApR) a.clear();
        for (auto& s : lowShelf) s.reset();
        for (auto& s : highShelf) s.reset();
        for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR }) f->reset();
        for (size_t k = 0; k < lfoPhase.size(); ++k) lfoPhase[k] = 0.7 * static_cast<double> (k);
        silent = 0;
        idle = true;
    }

    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        feed.setTarget (p.on ? 1.0 : 0.0);
        mixS.setTarget (std::clamp (p.mix, 0.0, 1.0));
        if (! p.on && ! feed.isSmoothing() && feed.getCurrent() == 0.0 && idle) return;
        if (idle && p.on)
        {
            reset();
            idle = false;
            if (p.program != currentProgram) { currentProgram = std::clamp (p.program, 0, kNumPrograms - 1); buildProgram (currentProgram); }
            sizeS = sizeScale (p.size);
            preS = std::clamp (p.preDelayMs, 0.0, 250.0) * 0.001 * sampleRate;
            rtS.snap (std::clamp (p.decayS, 0.1, 20.0));
            updateGains (p);
        }

        // program change: fade the wet out, rebuild, fade back in (like the hardware, the old tail is cut)
        const int prog = std::clamp (p.program, 0, kNumPrograms - 1);
        if (prog != currentProgram)
        {
            progFade.setTarget (0.0);
            if (! progFade.isSmoothing() && progFade.getCurrent() <= 0.0)
            {
                currentProgram = prog;
                buildProgram (prog);
                for (auto& l : lines) l.clear();
                for (auto& a : loopAp) a.clear();
                erL.clear(); erR.clear();
                progFade.setTarget (1.0);
            }
        }

        const double sizeTarget = sizeScale (p.size);
        const double preTarget = std::clamp (p.preDelayMs, 0.0, 250.0) * 0.001 * sampleRate;
        rtS.setTarget (std::clamp (p.decayS, 0.1, 20.0));
        const double width = std::clamp (p.width, 0.0, 1.5);
        const double early = std::clamp (p.early, 0.0, 1.0) * prog_.erLevel;
        const double diff = std::clamp (p.diffusion, 0.0, 1.0);
        const double inG = 0.35 + 0.37 * diff;            // input diffusers
        const double loopG = prog_.density * (0.25 + 0.45 * diff); // in-loop allpasses
        const double modDepth = std::clamp (p.mod, 0.0, 1.0) * 0.0011 * sampleRate * (0.5 + 0.5 * sizeTarget);
        lowCutL.set (Svf::Type::HighPass, std::clamp (p.lowCutHz, 20.0, 1000.0), 0.7071);
        lowCutR.set (Svf::Type::HighPass, std::clamp (p.lowCutHz, 20.0, 1000.0), 0.7071);
        highCutL.set (Svf::Type::LowPass, std::clamp (p.highCutHz, 1000.0, 20000.0), 0.7071);
        highCutR.set (Svf::Type::LowPass, std::clamp (p.highCutHz, 1000.0, 20000.0), 0.7071);
        updateGains (p);

        constexpr double twoPi = 6.283185307179586;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 31) == 0 && (std::fabs (sizeS - sizeTarget) > 1e-6 || rtS.isSmoothing())) updateGains (p);
            sizeS = sizeTarget + glide * (sizeS - sizeTarget);
            preS = preTarget + glide * (preS - preTarget);
            rtS.next();
            const double fg = feed.next(), m = mixS.next(), pf = progFade.next();

            // pre-delay, per channel
            preL.push (left[i] * fg);
            preR.push (right[i] * fg);
            const double xl = preL.read (std::max (2.0, preS)), xr = preR.read (std::max (2.0, preS));

            // early reflections: sparse stereo taps, scaled with size
            erL.push (xl);
            erR.push (xr);
            double el = 0.0, er = 0.0;
            for (int t = 0; t < kTaps; ++t)
            {
                const auto& tp = prog_.taps[static_cast<size_t> (t)];
                el += tp.gl * erL.read (std::max (2.0, tp.ms * sizeS * 0.001 * sampleRate)) + tp.xl * erR.read (std::max (2.0, tp.ms * 1.13 * sizeS * 0.001 * sampleRate));
                er += tp.gr * erR.read (std::max (2.0, tp.ms * 1.07 * sizeS * 0.001 * sampleRate)) + tp.xr * erL.read (std::max (2.0, tp.ms * 0.94 * sizeS * 0.001 * sampleRate));
            }

            // input diffusion (each channel through 4 allpasses)
            double dl = xl + 0.35 * el, dr = xr + 0.35 * er;
            for (int k = 0; k < 4; ++k)
            {
                dl = allpass (inApL[static_cast<size_t> (k)], dl, kInApMs[static_cast<size_t> (k)] * (0.6 + 0.4 * sizeS) * 0.001 * sampleRate, inG);
                dr = allpass (inApR[static_cast<size_t> (k)], dr, kInApMs[static_cast<size_t> (k)] * 1.09 * (0.6 + 0.4 * sizeS) * 0.001 * sampleRate, inG);
            }

            // tank: read (modulated), absorb, densify
            std::array<double, kLines> y {};
            for (int k = 0; k < kLines; ++k)
            {
                const size_t kk = static_cast<size_t> (k);
                lfoPhase[kk] += twoPi * kLfoHz[kk] / sampleRate;
                if (lfoPhase[kk] > twoPi) lfoPhase[kk] -= twoPi;
                const double d = len[kk] + modDepth * std::sin (lfoPhase[kk]);
                double v = lines[kk].read (std::max (3.0, d));
                v = highShelf[kk].process (lowShelf[kk].process (v)) * gMid[kk];
                y[kk] = allpass (loopAp[kk], v, prog_.loopApMs[kk] * 0.001 * sampleRate, loopG);
            }
            hadamard16 (y);

            // inject the input (L into the first half, R into the second) and write back
            constexpr double inScale = 0.25;
            for (int k = 0; k < kLines; ++k)
                lines[static_cast<size_t> (k)].push (y[static_cast<size_t> (k)] + inScale * (k < 8 ? dl : dr) * ((k & 1) ? -1.0 : 1.0));

            // outputs: two orthogonal Hadamard rows -> decorrelated L / R
            double ol = 0.0, orr = 0.0;
            for (int k = 0; k < kLines; ++k)
            {
                ol += kOutL[static_cast<size_t> (k)] * y[static_cast<size_t> (k)];
                orr += kOutR[static_cast<size_t> (k)] * y[static_cast<size_t> (k)];
            }
            ol = ol * kOutGain * outComp + early * el;
            orr = orr * kOutGain * outComp + early * er;

            // width (mid/side), wet EQ
            const double mid = 0.5 * (ol + orr), side = 0.5 * (ol - orr) * width;
            double wl = highCutL.process (lowCutL.process (mid + side));
            double wr = highCutR.process (lowCutR.process (mid - side));
            wl *= pf; wr *= pf;
            left[i] = static_cast<float> (left[i] + m * wl);
            right[i] = static_cast<float> (right[i] + m * wr);

            if (fg == 0.0 && std::fabs (wl) + std::fabs (wr) < 1e-7) { if (++silent > static_cast<int> (sampleRate)) idle = true; }
            else silent = 0;
        }
    }

private:
    static constexpr int kTaps = 14;
    static constexpr double kMaxLineMs = 175.0;
    static constexpr double kOutGain = 0.45;
    static constexpr std::array<double, 4> kInApMs { 4.31, 6.87, 9.93, 13.07 };
    static constexpr std::array<double, kLines> kLfoHz { 0.31, 0.47, 0.53, 0.71, 0.83, 0.97, 1.13, 0.37,
                                                          0.59, 0.67, 0.89, 1.07, 0.43, 0.79, 1.21, 0.61 };
    // rows 1 and 2 of a 16 x 16 Hadamard matrix (orthogonal to each other)
    static constexpr std::array<double, kLines> kOutL { 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1 };
    static constexpr std::array<double, kLines> kOutR { 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1 };

    struct Tap { double ms, gl, gr, xl, xr; };
    struct ProgramData
    {
        std::array<double, kLines> lineMs {};
        std::array<double, kLines> loopApMs {};
        std::array<Tap, kTaps> taps {};
        double lowXHz = 300.0, highXHz = 3500.0, erLevel = 1.0, density = 1.0;
    };

    /** First-order shelf (bilinear): low shelf (gain at DC, unity at Nyquist) or high shelf (unity at DC, gain at Nyquist).
        Its magnitude moves monotonically between the two, so it never exceeds the larger of them. */
    struct Shelf
    {
        double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
        void reset() noexcept { x1 = y1 = 0.0; }
        void setLow (double gainAtDc, double fc, double fs) noexcept
        {
            const double K = std::tan (3.14159265358979323846 * std::min (fc, 0.45 * fs) / fs);
            b0 = (1.0 + gainAtDc * K) / (1.0 + K); b1 = (gainAtDc * K - 1.0) / (1.0 + K); a1 = (K - 1.0) / (1.0 + K);
        }
        void setHigh (double gainAtNyq, double fc, double fs) noexcept
        {
            const double K = std::tan (3.14159265358979323846 * std::min (fc, 0.45 * fs) / fs);
            b0 = (gainAtNyq + K) / (1.0 + K); b1 = (K - gainAtNyq) / (1.0 + K); a1 = (K - 1.0) / (1.0 + K);
        }
        double process (double x) noexcept { const double y = b0 * x + b1 * x1 - a1 * y1; x1 = x; y1 = y; return y; }
    };

    static double sizeScale (double size) noexcept { return 0.45 + 0.55 * std::clamp (size, 0.0, 1.0); }

    static double allpass (fxdetail::DelayLine& d, double x, double delaySamples, double g) noexcept
    {
        const double z = d.read (std::max (2.0, delaySamples));
        const double v = x + g * z;
        d.push (v);
        return z - g * v;
    }

    static void hadamard16 (std::array<double, kLines>& v) noexcept
    {
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const double a = v[static_cast<size_t> (j)], b = v[static_cast<size_t> (j + h)];
                    v[static_cast<size_t> (j)] = a + b;
                    v[static_cast<size_t> (j + h)] = a - b;
                }
        for (auto& x : v) x *= 0.25; // 1 / sqrt (16): orthonormal
    }

    void buildProgram (int prog) noexcept
    {
        struct Shape { double minMs, maxMs, erMin, erMax, erDecayMs, lowX, highX, erLevel, density; };
        static const Shape shapes[] {
            { 9.0, 31.0, 1.5, 18.0, 9.0, 450.0, 5000.0, 1.0, 1.0 },     // ambience
            { 15.0, 48.0, 2.5, 34.0, 14.0, 380.0, 4200.0, 1.0, 1.0 },   // room
            { 23.0, 71.0, 4.0, 52.0, 22.0, 320.0, 3800.0, 0.9, 1.1 },   // chamber
            { 37.0, 113.0, 7.0, 82.0, 36.0, 260.0, 3200.0, 0.8, 1.0 },  // hall
            { 10.0, 47.0, 1.0, 4.0, 2.0, 500.0, 6500.0, 0.0, 1.35 },    // plate: no reflections, very dense
            { 57.0, 168.0, 12.0, 118.0, 55.0, 220.0, 2600.0, 0.7, 0.9 } // cathedral
        };
        const auto& s = shapes[std::clamp (prog, 0, kNumPrograms - 1)];
        prog_.lowXHz = s.lowX;
        prog_.highXHz = s.highX;
        prog_.erLevel = s.erLevel;
        prog_.density = s.density;
        for (int k = 0; k < kLines; ++k)
        {
            // geometric spread with an irrational wobble so no two lines share a common period
            const double t = static_cast<double> (k) / (kLines - 1);
            prog_.lineMs[static_cast<size_t> (k)] = s.minMs * std::pow (s.maxMs / s.minMs, t) * (1.0 + 0.031 * std::sin (2.71828 * k + 0.5));
            prog_.loopApMs[static_cast<size_t> (k)] = 1.13 + 2.9 * std::fmod (0.618034 * (k + 1), 1.0);
        }
        // reflection pattern: deterministic per program, decaying, alternating signs, slightly different L / R
        uint32_t seed = 0x9e3779b9u * static_cast<uint32_t> (prog + 1);
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<double> (seed >> 8) / 16777216.0; };
        for (int t = 0; t < kTaps; ++t)
        {
            const double u = (t + rnd()) / kTaps;
            const double ms = s.erMin + (s.erMax - s.erMin) * u * u;
            const double a = std::exp (-ms / s.erDecayMs) * 0.55;
            prog_.taps[static_cast<size_t> (t)] = { ms, a * (rnd() < 0.5 ? -1.0 : 1.0) * (0.8 + 0.4 * rnd()), a * (rnd() < 0.5 ? -1.0 : 1.0) * (0.8 + 0.4 * rnd()),
                                                    0.3 * a * (rnd() - 0.5), 0.3 * a * (rnd() - 0.5) };
        }
    }

    void updateGains (const Params& p) noexcept
    {
        const double rt = std::max (0.1, rtS.getCurrent());
        // Level compensation: a tank's steady-state energy grows with RT60 / mean line length. Equalise across
        // programs and sizes, and keep half of the decay dependence (in dB) so longer tails still feel bigger.
        double meanMs = 0.0;
        for (int k = 0; k < kLines; ++k) meanMs += prog_.lineMs[static_cast<size_t> (k)] * sizeS + prog_.loopApMs[static_cast<size_t> (k)];
        meanMs /= kLines;
        outComp = std::sqrt (meanMs / 30.0) * std::pow (rt / 2.2, -0.25);
        const double lowMul = std::clamp (p.lowRT, 0.5, 2.0), highMul = std::clamp (p.highRT, 0.1, 1.0);
        for (int k = 0; k < kLines; ++k)
        {
            const size_t kk = static_cast<size_t> (k);
            len[kk] = prog_.lineMs[kk] * sizeS * 0.001 * sampleRate;
            // per-line delay incl. its in-loop allpass (average group delay ~ its length)
            const double secs = (len[kk] + prog_.loopApMs[kk] * 0.001 * sampleRate) / sampleRate;
            const double gm = std::pow (10.0, -3.0 * secs / rt);
            const double gl = std::pow (10.0, -3.0 * secs / (rt * lowMul));
            const double gh = std::pow (10.0, -3.0 * secs / (rt * highMul));
            // bound the loop gain below 1 at all frequencies: |H| <= gm * max (1, gl/gm) * max (1, gh/gm)
            double bound = std::max (gm, gl) * std::max (1.0, gh / gm);
            const double safety = bound > 0.9995 ? 0.9995 / bound : 1.0;
            gMid[kk] = gm * safety;
            lowShelf[kk].setLow (gl / gm, prog_.lowXHz, sampleRate);
            highShelf[kk].setHigh (gh / gm, prog_.highXHz, sampleRate);
        }
    }

    std::array<fxdetail::DelayLine, kLines> lines;
    std::array<fxdetail::DelayLine, kLines> loopAp;
    std::array<Shelf, kLines> lowShelf, highShelf;
    std::array<double, kLines> len {}, gMid {}, lfoPhase {};
    fxdetail::DelayLine preL, preR, erL, erR;
    std::array<fxdetail::DelayLine, 4> inApL, inApR;
    Svf lowCutL, lowCutR, highCutL, highCutR;
    LinearSmoother feed, mixS, progFade, rtS;
    ProgramData prog_;
    int currentProgram = Hall;
    double sampleRate = 48000.0, sizeS = 0.7, preS = 0.0, glide = 0.0, outComp = 1.0;
    int silent = 0;
    bool idle = true;
};

} // namespace vfxa
