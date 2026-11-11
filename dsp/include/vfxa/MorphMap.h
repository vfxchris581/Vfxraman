// VFXAamps — mapping of the 1..10 gain/morph knob onto the loaded NAM slots.
//
// Pure functions (no state) so the mapping can be unit-tested on its own.
//   * The knob value p (1.0 .. 10.0) is a continuous slot index s = p - 1 (0 .. 9).
//   * The two loaded slots around s are blended with an equal-power crossfade:
//     at p = 1.5 with slots 1 and 2 loaded, both play at cos(pi/4) = -3 dB.
//   * Empty slots are skipped: with only slots 2 and 5 loaded, the knob blends 2 -> 5
//     across 2..5 (continuous, never a jump), plays slot 2 alone below 2, slot 5 above 5.
#pragma once

#include <cmath>
#include <cstdint>

namespace vfxa
{

constexpr int kNumSlots = 10;
using SlotMask = std::uint32_t; // bit i set = slot i (0-based) has a model

struct MorphPoint
{
    int lo = -1, hi = -1;   // 0-based slots (equal when only one plays); -1 = nothing loaded
    double t = 0.0;         // 0 = all lo, 1 = all hi
    double gainLo = 0.0, gainHi = 0.0;
};

inline bool slotLoaded (SlotMask m, int i) noexcept { return i >= 0 && i < kNumSlots && ((m >> i) & 1u) != 0; }

inline int prevLoaded (SlotMask m, int i) noexcept { for (int k = i - 1; k >= 0; --k) if (slotLoaded (m, k)) return k; return -1; }
inline int nextLoaded (SlotMask m, int i) noexcept { for (int k = i + 1; k < kNumSlots; ++k) if (slotLoaded (m, k)) return k; return -1; }

inline MorphPoint morphAt (double knob, SlotMask mask) noexcept
{
    MorphPoint p;
    if (mask == 0) return p;
    const double s = std::fmin (9.0, std::fmax (0.0, knob - 1.0));
    const int fl = static_cast<int> (std::floor (s));
    int lo = slotLoaded (mask, fl) ? fl : prevLoaded (mask, fl);
    int hi = (s > fl && slotLoaded (mask, fl + 1)) ? fl + 1 : (s == fl && slotLoaded (mask, fl) ? fl : nextLoaded (mask, fl));
    if (lo < 0) lo = hi;
    if (hi < 0) hi = lo;
    p.lo = lo;
    p.hi = hi;
    if (lo == hi) { p.t = 0.0; p.gainLo = 1.0; p.gainHi = 0.0; return p; }
    p.t = (s - lo) / static_cast<double> (hi - lo);
    p.t = std::fmin (1.0, std::fmax (0.0, p.t));
    constexpr double halfPi = 1.57079632679489661923;
    p.gainLo = std::cos (p.t * halfPi);
    p.gainHi = std::sin (p.t * halfPi);
    return p;
}

/** Slots that must be running ("warm") while the knob moves from a to b: every loaded slot that
    can be heard in that range, plus one loaded neighbour on each side so it is ready before it fades in. */
inline SlotMask warmSetFor (double a, double b, SlotMask mask, bool withNeighbours = true) noexcept
{
    if (mask == 0) return 0;
    const double lo = a < b ? a : b, hi = a < b ? b : a;
    const auto pa = morphAt (lo, mask), pb = morphAt (hi, mask);
    int first = pa.lo, last = pb.hi;
    if (first > last) { const int t = first; first = last; last = t; }
    if (withNeighbours)
    {
        // Two audible slots (or one) get a warm neighbour on each side. When the knob crosses a
        // slot within one block (three audible), only the neighbour ahead is kept: at most four run.
        int audible = 0;
        for (int i = first; i <= last; ++i) audible += slotLoaded (mask, i) ? 1 : 0;
        const bool up = b > a;
        if (audible < 3 || ! up) if (const int p = prevLoaded (mask, first); p >= 0) first = p;
        if (audible < 3 || up)   if (const int n = nextLoaded (mask, last); n >= 0) last = n;
    }
    SlotMask w = 0;
    for (int i = first; i <= last; ++i) if (slotLoaded (mask, i)) w |= (1u << i);
    return w;
}

inline int countSlots (SlotMask m) noexcept { int n = 0; for (int i = 0; i < kNumSlots; ++i) n += slotLoaded (m, i) ? 1 : 0; return n; }

} // namespace vfxa
