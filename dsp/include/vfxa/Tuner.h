// VFXAamps — guitar tuner: lock-free capture on the audio thread, YIN pitch detection elsewhere.
#pragma once

#include "vfxa/Filters.h"

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vfxa
{

struct TunerReading
{
    bool valid = false;
    double hz = 0.0;
    int midiNote = 0;     // nearest equal-tempered note (A4 = 69)
    double cents = 0.0;   // -50 .. +50 from that note
    double clarity = 0.0; // 0..1 (1 = clean periodic signal)

    static const char* noteName (int midi) noexcept
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midi % 12) + 12) % 12];
    }
    int octave() const noexcept { return midiNote / 12 - 1; }
};

class Tuner
{
public:
    static constexpr int kRing = 8192;
    static constexpr double kLowestHz = 27.0;   // below low F# on an 8-string (46 Hz) and low B on a 7 (30.9 Hz)
    static constexpr double kHighestHz = 1400.0;

    /** Non-real-time: set up for a host sample rate. */
    void prepare (double fs) noexcept
    {
        decim = std::max (1, static_cast<int> (std::lround (fs / 12000.0)));
        rate = fs / decim;
        for (auto* f : { &aa1, &aa2 }) { f->setSampleRate (fs); f->reset(); f->set (Svf::Type::LowPass, 2000.0, 0.7071); }
        phase = 0;
        for (auto& v : ring) v.store (0.0f, std::memory_order_relaxed);
        writePos.store (0, std::memory_order_release);
    }

    /** Audio thread: feed the guitar input. */
    void push (const float* x, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const double y = aa2.process (aa1.process (x[i]));
            if (++phase >= decim)
            {
                phase = 0;
                ring[static_cast<size_t> (w & (kRing - 1))].store (static_cast<float> (y), std::memory_order_relaxed);
                ++w;
            }
        }
        writePos.store (w, std::memory_order_release);
    }

    /** Any non-audio thread: detect the pitch of the most recent input. */
    TunerReading analyse (double a4 = 440.0)
    {
        TunerReading r;
        const int tauMax = static_cast<int> (rate / kLowestHz) + 2;
        const int tauMin = std::max (2, static_cast<int> (rate / kHighestHz));
        const int window = std::max (1024, tauMax * 2);
        const int need = window + tauMax + 2;
        if (need >= kRing) return r;

        x.resize (static_cast<size_t> (need));
        const int w = writePos.load (std::memory_order_acquire);
        if (w < need) return r;
        double energy = 0.0;
        for (int i = 0; i < need; ++i)
        {
            const float v = ring[static_cast<size_t> ((w - need + i) & (kRing - 1))].load (std::memory_order_relaxed);
            x[static_cast<size_t> (i)] = v;
            energy += static_cast<double> (v) * v;
        }
        if (std::sqrt (energy / need) < 0.0018) return r; // ~ -55 dBFS: not playing

        // YIN: difference function -> cumulative mean normalised difference
        d.assign (static_cast<size_t> (tauMax + 2), 0.0);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const double diff = x[static_cast<size_t> (j)] - x[static_cast<size_t> (j + tau)];
                s += diff * diff;
            }
            d[static_cast<size_t> (tau)] = s;
        }
        raw = d; // un-normalised difference: unbiased for the sub-sample refinement below
        double running = 0.0;
        d[0] = 1.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[static_cast<size_t> (tau)];
            d[static_cast<size_t> (tau)] = running > 0.0 ? d[static_cast<size_t> (tau)] * tau / running : 1.0;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (d[static_cast<size_t> (tau)] < 0.12)
            {
                while (tau + 1 < tauMax && d[static_cast<size_t> (tau + 1)] < d[static_cast<size_t> (tau)]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            // no dip under the threshold: take the global minimum if it is reasonably clear
            double m = 1.0;
            for (int tau = tauMin; tau < tauMax; ++tau) if (d[static_cast<size_t> (tau)] < m) { m = d[static_cast<size_t> (tau)]; best = tau; }
            if (m > 0.3) return r;
        }

        // parabolic interpolation around the dip of the raw difference function
        double t = best;
        if (best > 1 && best < tauMax)
        {
            const double a = raw[static_cast<size_t> (best - 1)], b = raw[static_cast<size_t> (best)], c = raw[static_cast<size_t> (best + 1)];
            const double den = a - 2.0 * b + c;
            if (std::fabs (den) > 1e-12) t = best + 0.5 * (a - c) / den;
        }

        r.hz = rate / t;
        if (r.hz < kLowestHz || r.hz > kHighestHz) return {};
        const double midi = 69.0 + 12.0 * std::log2 (r.hz / a4);
        r.midiNote = static_cast<int> (std::lround (midi));
        r.cents = (midi - r.midiNote) * 100.0;
        r.clarity = std::clamp (1.0 - d[static_cast<size_t> (best)], 0.0, 1.0);
        r.valid = true;
        return r;
    }

private:
    std::array<std::atomic<float>, kRing> ring {};
    std::atomic<int> writePos { 0 };
    Svf aa1, aa2;
    int decim = 4, phase = 0;
    double rate = 12000.0;
    std::vector<float> x;
    std::vector<double> d, raw;
};

} // namespace vfxa
