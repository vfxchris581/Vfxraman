// VFXAamps — streaming, arbitrary-ratio, windowed-sinc resampler.
//
// Kaiser-windowed sinc (32 zero crossings, ~-90 dB stopband, passband to ~91% of the
// lower Nyquist), table-driven with linear interpolation between 1024 phases per input
// sample. The time mapping is exact: output sample j sits at input time j * (fsIn / fsOut),
// so a pair of these (host -> model rate -> host) has an exactly integer latency equal to
// the FIFO prime chosen by ResamplingPair. All memory is allocated in prepare().
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace vfxa
{

class SincResampler
{
public:
    void prepare (double inRate, double outRate, int maxInputBlock, int zeroCrossings = 32)
    {
        step = inRate / outRate;
        const double cutoff = 0.5 * std::min (1.0, outRate / inRate) * 0.91; // cycles per input sample
        halfWidth = zeroCrossings / (2.0 * cutoff);                          // input samples
        taps = static_cast<int> (std::ceil (halfWidth));

        const double beta = 9.0;
        const int tableLen = (static_cast<int> (std::ceil (halfWidth)) + 2) * phases + 2;
        table.assign (static_cast<size_t> (tableLen), 0.0f);
        const double i0b = besselI0 (beta);
        for (int i = 0; i < tableLen; ++i)
        {
            const double u = static_cast<double> (i) / phases;
            if (u > halfWidth) break;
            const double x = 2.0 * cutoff * u;
            const double sinc = x < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double r = u / halfWidth;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            table[static_cast<size_t> (i)] = static_cast<float> (2.0 * cutoff * sinc * w);
        }

        buffer.assign (static_cast<size_t> (maxInputBlock + 2 * taps + 8), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        count = taps;              // `taps` samples of silent pre-history
        pos = static_cast<double> (taps); // output time 0 == first real input sample
    }

    /** Input samples needed beyond an output's time before it can be produced. */
    int lookahead() const noexcept { return taps; }
    double ratio() const noexcept { return step; }

    /** Upper bound on the outputs produced for numIn inputs. */
    int maxOutputFor (int numIn) const noexcept { return static_cast<int> (std::ceil ((numIn + 2) / step)) + 2; }

    /** Pushes numIn samples and writes every output that can now be computed. Returns the count written. */
    int process (const float* in, int numIn, float* out, int maxOut) noexcept
    {
        float* x = buffer.data();
        std::copy (in, in + numIn, x + count);
        count += numIn;

        int produced = 0;
        while (produced < maxOut)
        {
            const int i0 = static_cast<int> (pos);
            if (i0 + taps >= count) break;
            // Taps at distance d = j - frac. Right side (j >= 1): table index j*P - frac*P;
            // left side (j <= 0): |j|*P + frac*P. Each side has one interpolation fraction.
            const double fracP = (pos - i0) * phases;
            const int fi = static_cast<int> (fracP);
            const float ff = static_cast<float> (fracP - fi);
            const float* t = table.data();
            const float* xc = x + i0;
            float acc = 0.0f;
            for (int j = 0; j < taps; ++j)          // left side incl. centre: x[i0 - j]
            {
                const int i = j * phases + fi;
                acc += xc[-j] * (t[i] + ff * (t[i + 1] - t[i]));
            }
            const int fiR = ff > 0.0f ? fi + 1 : fi;   // right side: index j*P - fracP
            const float ffR = ff > 0.0f ? 1.0f - ff : 0.0f;
            for (int j = 1; j <= taps; ++j)          // right side: x[i0 + j]
            {
                const int i = j * phases - fiR;
                acc += xc[j] * (t[i] + ffR * (t[i + 1] - t[i]));
            }
            out[produced++] = acc;
            pos += step;
        }

        // Keep only the history the next output can still reach.
        const int keepFrom = std::max (0, static_cast<int> (pos) - taps + 1);
        if (keepFrom > 0)
        {
            std::copy (x + keepFrom, x + count, x);
            count -= keepFrom;
            pos -= keepFrom;
        }
        return produced;
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr int phases = 1024;

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-14 * sum) break;
        }
        return sum;
    }

    std::vector<float> table, buffer;
    double step = 1.0, halfWidth = 1.0, pos = 0.0;
    int taps = 1, count = 0;
};

/** Fixed-capacity FIFO (no allocation after prepare). */
class SampleFifo
{
public:
    void prepare (int capacity) { data.assign (static_cast<size_t> (std::max (1, capacity)), 0.0f); clear(); }
    void clear() noexcept { readPos = writePos = used = 0; }
    int size() const noexcept { return used; }

    void push (const float* x, int n) noexcept
    {
        const int cap = static_cast<int> (data.size());
        for (int i = 0; i < n && used < cap; ++i)
        {
            data[static_cast<size_t> (writePos)] = x[i];
            if (++writePos == cap) writePos = 0;
            ++used;
        }
    }

    void pushZeros (int n) noexcept
    {
        const int cap = static_cast<int> (data.size());
        for (int i = 0; i < n && used < cap; ++i)
        {
            data[static_cast<size_t> (writePos)] = 0.0f;
            if (++writePos == cap) writePos = 0;
            ++used;
        }
    }

    /** Pops n samples; returns false (and pads with zeros) on underrun. */
    bool pop (float* out, int n) noexcept
    {
        const int cap = static_cast<int> (data.size());
        bool ok = true;
        for (int i = 0; i < n; ++i)
        {
            if (used == 0) { out[i] = 0.0f; ok = false; continue; }
            out[i] = data[static_cast<size_t> (readPos)];
            if (++readPos == cap) readPos = 0;
            --used;
        }
        return ok;
    }

private:
    std::vector<float> data;
    int readPos = 0, writePos = 0, used = 0;
};

/**
    Round trip outer rate -> inner rate -> outer rate around a block-processing callback.
    The callback sees variable-size blocks at the inner rate. Latency is exactly latencySamples()
    outer samples (integer), and output never underruns.
*/
class ResamplingPair
{
public:
    void prepare (double outerRate, double innerRate, int maxOuterBlock)
    {
        active = std::fabs (outerRate - innerRate) > 0.01;
        maxOuter = std::max (1, maxOuterBlock);
        if (! active) { latency = 0; maxInner = maxOuter; return; }

        up.prepare (outerRate, innerRate, maxOuter);
        maxInner = up.maxOutputFor (maxOuter);
        down.prepare (innerRate, outerRate, maxInner);
        innerBuf.assign (static_cast<size_t> (maxInner), 0.0f);
        outBuf.assign (static_cast<size_t> (down.maxOutputFor (maxInner)), 0.0f);

        // up adds up.lookahead() outer samples of delay; down adds down.lookahead() inner samples.
        const double delay = up.lookahead() + down.lookahead() * (outerRate / innerRate);
        latency = static_cast<int> (std::ceil (delay)) + 3; // + block-boundary jitter margin
        fifo.prepare (latency + 4 * maxOuter + 64);
        reset();
    }

    void reset()
    {
        if (! active) return;
        up.reset();
        down.reset();
        fifo.clear();
        fifo.pushZeros (latency);
    }

    bool isActive() const noexcept { return active; }
    int latencySamples() const noexcept { return latency; }
    int maxInnerBlock() const noexcept { return maxInner; }

    /** inner(const float* in, float* out, int n): processes n inner-rate samples. */
    template <typename InnerFn>
    bool process (const float* in, float* out, int n, InnerFn&& inner) noexcept
    {
        if (! active) { inner (in, out, n); return true; }
        bool ok = true;
        for (int start = 0; start < n; start += maxOuter)
        {
            const int k = std::min (maxOuter, n - start);
            const int m = up.process (in + start, k, innerBuf.data(), maxInner);
            if (m > 0)
            {
                inner (innerBuf.data(), innerBuf.data(), m);
                const int got = down.process (innerBuf.data(), m, outBuf.data(), static_cast<int> (outBuf.size()));
                fifo.push (outBuf.data(), got);
            }
            ok = fifo.pop (out + start, k) && ok;
        }
        return ok;
    }

private:
    SincResampler up, down;
    SampleFifo fifo;
    std::vector<float> innerBuf, outBuf;
    bool active = false;
    int latency = 0, maxOuter = 1, maxInner = 1;
};

} // namespace vfxa
