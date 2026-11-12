// VFXAamps — dual IR loader implementation.

#include "DualIr.h"

#include <cmath>

namespace vfxa
{

IrChannel::IrChannel() = default;

int IrChannel::resampledLength (int len, double from, double to) noexcept
{
    // Same rule as juce::dsp::Convolution's resampleImpulseResponse().
    if (len <= 0 || from <= 0.0 || to <= 0.0) return 0;
    if (juce::approximatelyEqual (from, to)) return len;
    return juce::roundToInt (std::max (1.0, len / (from / to)));
}

void IrChannel::prepare (double fs, int maxBlock)
{
    sampleRate = fs;
    expectedIrSize.store (resampledLength (irLength.load(), irRateHz.load(), fs)); // JUCE rebuilds the engine at the new rate
    conv.prepare ({ fs, static_cast<juce::uint32> (std::max (1, maxBlock)), 2 });
    work.setSize (2, std::max (1, maxBlock), false, true, true);
    cutL.prepare (fs);
    cutR.prepare (fs);
    gain.prepare (fs, 0.030);
    panPos.prepare (fs, 0.030);
    polarity.prepare (fs, 0.015);
    delayFade.prepare (fs, 0.030);
    lowCutLog.prepare (fs, 0.040);
    lowOn.prepare (fs, 0.030);
    highOn.prepare (fs, 0.030);
    lowOn.snap (0.0);
    highOn.snap (0.0);
    highCutLog.prepare (fs, 0.040);
    gain.snap (0.0);
    panPos.snap (0.0);
    polarity.snap (1.0);
    delayFade.snap (1.0);
    delayFrom = delayTo = 0.0;
    lowCutLog.snap (std::log2 (20.0));
    highCutLog.snap (std::log2 (20000.0));
    reset();
}

void IrChannel::reset()
{
    conv.reset();
    std::fill (std::begin (delayL), std::end (delayL), 0.0f);
    std::fill (std::begin (delayR), std::end (delayR), 0.0f);
    cutL.reset();
    cutR.reset();
    needsReset = false;
}

juce::String IrChannel::readIrFile (const juce::File& f, juce::AudioBuffer<float>& out, double& rate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) return "Not an audio file this build can read: " + f.getFileName();
    if (reader->lengthInSamples <= 0) return "Empty IR: " + f.getFileName();
    const auto maxLen = static_cast<juce::int64> (reader->sampleRate * 10.0); // cabinet IRs are short; cap at 10 s
    const int len = static_cast<int> (std::min (reader->lengthInSamples, maxLen));
    const int ch = static_cast<int> (std::min (2u, reader->numChannels));
    out.setSize (ch, len);
    reader->read (&out, 0, len, 0, true, ch > 1);
    rate = reader->sampleRate;

    // Unit-energy normalisation (by the louder channel), so a cab IR keeps roughly the amp's level
    // and a single-sample impulse is transparent.
    double energy = 0.0;
    for (int c = 0; c < ch; ++c)
    {
        double e = 0.0;
        for (int i = 0; i < len; ++i) e += static_cast<double> (out.getSample (c, i)) * out.getSample (c, i);
        energy = std::max (energy, e);
    }
    if (energy <= 1e-12) return "IR is silent: " + f.getFileName();
    out.applyGain (static_cast<float> (1.0 / std::sqrt (energy)));
    return {};
}

void IrChannel::load (juce::AudioBuffer<float>&& ir, double irRate, const juce::String& p)
{
    std::lock_guard<std::mutex> lock (loadLock);
    const bool stereo = ir.getNumChannels() > 1;
    irLength.store (ir.getNumSamples());
    irRateHz.store (irRate);
    expectedIrSize.store (resampledLength (ir.getNumSamples(), irRate, sampleRate));
    // No trimming: keeps the IR's own timing so A/B mic pairs stay phase-aligned.
    conv.loadImpulseResponse (std::move (ir), irRate,
                              stereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no); // normalised in readIrFile
    path = p;
    loaded.store (true);
}

void IrChannel::clear()
{
    std::lock_guard<std::mutex> lock (loadLock);
    path = {};
    loaded.store (false); // fades out on the audio thread; the convolution idles once silent
}

juce::String IrChannel::getPath() const
{
    std::lock_guard<std::mutex> lock (loadLock);
    return path;
}

double IrChannel::readDelay (const float* buf, double d) const noexcept
{
    // 3rd-order Lagrange interpolation around (write - 1 - d)
    const double pos = static_cast<double> (delayWrite - 1) - d;
    const int i = static_cast<int> (std::floor (pos));
    const double f = pos - i;
    auto at = [buf] (int k) { return static_cast<double> (buf[k & (kDelaySize - 1)]); };
    const double xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const double c0 = -f * (f - 1.0) * (f - 2.0) / 6.0;
    const double c1 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
    const double c2 = -(f + 1.0) * f * (f - 2.0) / 2.0;
    const double c3 = (f + 1.0) * f * (f - 1.0) / 6.0;
    return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
}

void IrChannel::process (const float* in, float* outL, float* outR, int n, const IrParams& p, double weight) noexcept
{
    const bool active = p.on && isReady();
    // A newly loaded IR is only installed by JUCE while the convolution is running, so keep running it
    // (silently: gain stays at 0) until our IR is in, even if this channel had gone idle.
    const bool pending = loaded.load (std::memory_order_relaxed) && ! isReady();
    gain.setTarget (active ? GainSmoother::dbToGain (p.levelDb) * weight : 0.0);

    if (! active && ! pending && ! gain.isSmoothing() && gain.getCurrent() == 0.0)
    {
        // Silent: stop running the convolution after its tail has died away.
        idleSamples += n;
        if (idleSamples > static_cast<int> (0.5 * sampleRate)) { needsReset = true; return; }
    }
    else
    {
        idleSamples = 0;
    }
    if (needsReset && (active || pending)) reset(); // restart from silence (we fade in from zero anyway)
    if (needsReset) return;

    panPos.setTarget (juce::jlimit (-1.0, 1.0, static_cast<double> (p.pan)));
    polarity.setTarget (p.phaseInvert ? -1.0 : 1.0);
    const double wantDelay = juce::jlimit (0.0, 2.0, static_cast<double> (p.delayMs)) * 0.001 * sampleRate;
    if (! delayFade.isSmoothing() && std::fabs (wantDelay - delayTo) > 1e-6)
    {
        delayFrom = delayTo;   // crossfade from the old tap to the new one
        delayTo = wantDelay;
        delayFade.snap (0.0);
        delayFade.setTarget (1.0);
    }
    lowCutLog.setTarget (std::log2 (juce::jlimit (20.0, 500.0, static_cast<double> (p.lowCutHz))));
    highCutLog.setTarget (std::log2 (juce::jlimit (2000.0, 20000.0, static_cast<double> (p.highCutHz))));
    lowOn.setTarget (p.lowCutHz > 20.5f ? 1.0 : 0.0);     // at the end stops the filters are out of the path
    highOn.setTarget (p.highCutHz < 19900.0f ? 1.0 : 0.0);

    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::copy (work.getWritePointer (ch), in, n);
    juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 2, static_cast<size_t> (n));
    conv.process (juce::dsp::ProcessContextReplacing<float> (block));

    const float* wl = work.getReadPointer (0);
    const float* wr = work.getReadPointer (1);
    constexpr double quarterPi = 0.78539816339744830962;
    for (int i = 0; i < n; ++i)
    {
        delayL[delayWrite] = wl[i];
        delayR[delayWrite] = wr[i];
        delayWrite = (delayWrite + 1) & (kDelaySize - 1);

        if (--coefCountdown <= 0)
        {
            coefCountdown = 16;
            const double lc = std::exp2 (lowCutLog.getCurrent()), hc = std::exp2 (highCutLog.getCurrent());
            cutL.update (lc, hc);
            cutR.update (lc, hc);
        }
        lowCutLog.next();
        highCutLog.next();

        const double xf = delayFade.next();
        const double fadeIn = 0.5 - 0.5 * std::cos (xf * juce::MathConstants<double>::pi);
        const double g = gain.next() * polarity.next();
        const double pan = panPos.next();
        const double gl = std::cos ((pan + 1.0) * quarterPi) * juce::MathConstants<double>::sqrt2;
        const double gr = std::sin ((pan + 1.0) * quarterPi) * juce::MathConstants<double>::sqrt2;
        auto tap = [&] (const float* buf)
        {
            const double now = readDelay (buf, delayTo);
            return fadeIn >= 1.0 ? now : readDelay (buf, delayFrom) + fadeIn * (now - readDelay (buf, delayFrom));
        };
        const double lo = lowOn.next(), hi = highOn.next();
        const double l = cutL.process (tap (delayL), lo, hi);
        const double r = cutR.process (tap (delayR), lo, hi);
        outL[i] += static_cast<float> (l * g * gl);
        outR[i] += static_cast<float> (r * g * gr);
    }
}

//==============================================================================
void DualIr::prepare (double fs, int maxBlock)
{
    a.prepare (fs, maxBlock);
    b.prepare (fs, maxBlock);
    wetMix.prepare (fs, 0.030);
    weightA.prepare (fs, 0.030);
    weightB.prepare (fs, 0.030);
    first = true;
}

void DualIr::reset()
{
    a.reset();
    b.reset();
    first = true;
}

void DualIr::process (const float* in, float* outL, float* outR, int n,
                      bool globalOn, float blend, const IrParams& pa, const IrParams& pb) noexcept
{
    const bool aOn = pa.on && a.isReady(), bOn = pb.on && b.isReady();
    // Linear blend (IRs of one cab are coherent). With only one IR active it plays at full level.
    const double wa = aOn && bOn ? 1.0 - blend : (aOn ? 1.0 : 0.0);
    const double wb = aOn && bOn ? blend : (bOn ? 1.0 : 0.0);
    const double wet = globalOn && (aOn || bOn) ? 1.0 : 0.0;
    if (first) { weightA.snap (wa); weightB.snap (wb); wetMix.snap (wet); first = false; }
    weightA.setTarget (wa);
    weightB.setTarget (wb);
    wetMix.setTarget (wet);

    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);
    // Each IR channel smooths its own gain; the blend weights are applied per block (they are ramped
    // inside the channel by its 30 ms gain smoother, so no per-sample stepping reaches the output).
    const double blockA = weightA.peek (n), blockB = weightB.peek (n);
    weightA.skip (n);
    weightB.skip (n);
    a.process (in, outL, outR, n, pa, blockA);
    b.process (in, outL, outR, n, pb, blockB);

    for (int i = 0; i < n; ++i)
    {
        const double m = wetMix.next();
        if (m >= 1.0) continue;
        const double dry = in[i] * (1.0 - m);
        outL[i] = static_cast<float> (outL[i] * m + dry);
        outR[i] = static_cast<float> (outR[i] * m + dry);
    }
}

} // namespace vfxa
