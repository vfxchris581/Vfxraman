// VFXAamps — processor: input trim -> gate -> 10-slot NAM morph -> tone stack + presence -> dual IR
// -> output level -> limiter.

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
std::atomic<float>* param (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    auto* p = s.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

bool isModelAsset (int asset) noexcept { return asset < vfxa::kNumSlots || VFXAampsProcessor::isPedalAsset (asset); }

const juce::Identifier assetsId ("Assets"), assetId ("Asset"), indexId ("index"), pathId ("path"),
                       relHomeId ("relHome"), relBaseId ("relBase");
} // namespace

VFXAampsProcessor::VFXAampsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VFXAamps", vfxa::params::createLayout())
{
    namespace P = vfxa::params;
    pInTrim = param (apvts, P::inTrim);       pGateOn = param (apvts, P::gateOn);
    pGateThresh = param (apvts, P::gateThresh); pGateRel = param (apvts, P::gateRel);
    pMorph = param (apvts, P::morph);         pNormalize = param (apvts, P::normalize);
    pBass = param (apvts, P::bass);           pMid = param (apvts, P::mid);
    pTreble = param (apvts, P::treble);       pPresence = param (apvts, P::presence);
    pIrOn = param (apvts, P::irOn);           pIrBlend = param (apvts, P::irBlend);
    pOut = param (apvts, P::outLevel);        pLimiter = param (apvts, P::limiter);
    pOs = param (apvts, P::oversample);
    pEqOn = param (apvts, P::eqOn);
    for (int b = 0; b < vfxa::GraphicEq::kBands; ++b) pEqBand[static_cast<size_t> (b)] = param (apvts, P::eqBand (b));
    pDlyOn = param (apvts, P::dlyOn);         pDlyTime = param (apvts, P::dlyTime);
    pDlySync = param (apvts, P::dlySync);     pDlyDiv = param (apvts, P::dlyDiv);
    pDlyFb = param (apvts, P::dlyFeedback);   pDlyMix = param (apvts, P::dlyMix);
    pDlyTone = param (apvts, P::dlyTone);     pDlyPing = param (apvts, P::dlyPing);
    pRvbOn = param (apvts, P::rvbOn);         pRvbSize = param (apvts, P::rvbSize);
    pRvbDecay = param (apvts, P::rvbDecay);   pRvbPre = param (apvts, P::rvbPre);
    pRvbTone = param (apvts, P::rvbTone);     pRvbMix = param (apvts, P::rvbMix);
    pDlyMode = param (apvts, P::dlyMode);     pDlyLowCut = param (apvts, P::dlyLowCut);
    pDlyModDepth = param (apvts, P::dlyModDepth); pDlyModRate = param (apvts, P::dlyModRate);
    pDlyDuck = param (apvts, P::dlyDuck);     pDlyDiff = param (apvts, P::dlyDiff);
    pRvbProgram = param (apvts, P::rvbProgram); pRvbDiff = param (apvts, P::rvbDiff);
    pRvbMod = param (apvts, P::rvbMod);       pRvbLowRT = param (apvts, P::rvbLowRT);
    pRvbHighRT = param (apvts, P::rvbHighRT); pRvbEarly = param (apvts, P::rvbEarly);
    pRvbLowCut = param (apvts, P::rvbLowCut); pRvbWidth = param (apvts, P::rvbWidth);
    pCompOn = param (apvts, P::compOn);       pCompPeak = param (apvts, P::compPeakRed);
    pCompGain = param (apvts, P::compGain);   pCompLimit = param (apvts, P::compLimit);
    pCompEmph = param (apvts, P::compEmphasis); pCompMix = param (apvts, P::compMix);
    pCompPos = param (apvts, P::compPosition);
    pEqType = param (apvts, P::eqType);
    pEq31Link = param (apvts, P::eq31Link);
    pEq31Range = param (apvts, P::eq31Range);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = static_cast<size_t> (ch);
        for (int b = 0; b < vfxa::GraphicEq31::kBands; ++b) pEq31Band[c][static_cast<size_t> (b)] = param (apvts, P::eq31Band (ch, b));
        pEq31Input[c] = param (apvts, P::eq31Input (ch));
        pEq31LowCut[c] = param (apvts, P::eq31LowCut (ch));
        pEq31Limit[c] = param (apvts, P::eq31Limit (ch));
        pEq31Thresh[c] = param (apvts, P::eq31Thresh (ch));
        pEq31Nr[c] = param (apvts, P::eq31Nr (ch));
    }
    for (int k = 0; k < vfxa::kNumSlots; ++k) pSlotTrim[static_cast<size_t> (k)] = param (apvts, P::slotTrim (k));
    for (int k = 0; k < vfxa::kNumSlots; ++k) { pSlotIr[static_cast<size_t> (k)] = param (apvts, P::slotIr (k)); pendingSlotIr[static_cast<size_t> (k)].store (0); }
    for (int k = 0; k < kNumPedals; ++k)
    {
        pPedalOn[static_cast<size_t> (k)] = param (apvts, P::pedalOn (k));
        pPedalLevel[static_cast<size_t> (k)] = param (apvts, P::pedalLevel (k));
        pedalMeter[static_cast<size_t> (k)].store (0.0f);
    }
    for (int w = 0; w < kNumIrs; ++w)
        pIr[static_cast<size_t> (w)] = { param (apvts, P::ir (w, "on")),    param (apvts, P::ir (w, "level")),
                                         param (apvts, P::ir (w, "pan")),   param (apvts, P::ir (w, "phase")),
                                         param (apvts, P::ir (w, "delay")), param (apvts, P::ir (w, "lowcut")),
                                         param (apvts, P::ir (w, "highcut")) };

    juce::PropertiesFile::Options o;
    o.applicationName = "VFXAamps";
    o.filenameSuffix = ".settings";
    o.folderName = "VFXAamps";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    settings.setStorageParameters (o);

    for (auto& r : requestId) r.store (0);
    startTimerHz (20);
}

VFXAampsProcessor::~VFXAampsProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 30000); // jobs touch members below; finish them first
    engine.collectGarbage();
}

bool VFXAampsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

//==============================================================================
void VFXAampsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    maxBlock = std::max (16, samplesPerBlock);

    engine.prepare (sampleRate, maxBlock);
    irs.prepare (sampleRate, maxBlock);
    gate.prepare (sampleRate);
    mono.setSize (1, maxBlock, false, true, true);
    left.setSize (1, maxBlock, false, true, true);
    right.setSize (1, maxBlock, false, true, true);

    for (int f = 0; f < 3; ++f)
    {
        const double rate = sampleRate * (1 << f);
        tone[static_cast<size_t> (f)].prepare (rate);
        if (f > 0)
        {
            using OS = juce::dsp::Oversampling<float>;
            osTone[static_cast<size_t> (f)] = std::make_unique<OS> (1, static_cast<size_t> (f), OS::filterHalfBandPolyphaseIIR, true, true);
            osLim[static_cast<size_t> (f)] = std::make_unique<OS> (2, static_cast<size_t> (f), OS::filterHalfBandPolyphaseIIR, true, true);
            osTone[static_cast<size_t> (f)]->initProcessing (static_cast<size_t> (maxBlock));
            osLim[static_cast<size_t> (f)]->initProcessing (static_cast<size_t> (maxBlock));
        }
    }
    limiterL.prepare (sampleRate);
    for (auto& pd : pedals)
    {
        pd.engine.prepare (sampleRate, maxBlock);
        pd.mix.prepare (sampleRate, 0.020);
        pd.mix.snap (0.0);
        pd.level.prepare (sampleRate, 0.020);
        pd.dry.prepare (8192);
        pd.wet.assign (static_cast<size_t> (std::max (1, maxBlock)), 0.0f);
        pd.running = pd.hasModel.load();
        pd.gate.prepare (sampleRate, 0.005);
        pd.gate.snap (1.0);
        pd.hold = 0;
    }
    graphicEq.prepare (sampleRate);
    eq31.prepare (sampleRate);
    eqTypeFade.prepare (sampleRate, 0.020);
    eqTypeFade.snap (pEqType->load() > 0.5f ? 1.0 : 0.0);
    eqTmpL.assign (static_cast<size_t> (maxBlock), 0.0f);
    eqTmpR.assign (static_cast<size_t> (maxBlock), 0.0f);
    comp.prepare (sampleRate);
    compMove.prepare (sampleRate, 0.012);
    compMove.snap (1.0);
    compPosNow.store (static_cast<int> (pCompPos->load()));
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    tuner.prepare (sampleRate);
    tunerGate.prepare (sampleRate, 0.015);
    tunerGate.snap (tunerMute.load() ? 0.0 : 1.0);
    limiterL.setCeilingDb (-0.3);

    inTrim.prepare (sampleRate, 0.020);   inTrim.snapDb (pInTrim->load());
    outGain.prepare (sampleRate, 0.020);  outGain.snapDb (pOut->load());
    gateEnable.prepare (sampleRate, 0.010); gateEnable.snap (pGateOn->load() > 0.5f ? 1.0 : 0.0);
    limitEnable.prepare (sampleRate, 0.020); limitEnable.snap (pLimiter->load() > 0.5f ? 1.0 : 0.0);
    switchFade.prepare (sampleRate, 0.005); switchFade.snap (1.0);
    const std::array<std::atomic<float>*, 4> knobs { pBass, pMid, pTreble, pPresence };
    for (size_t k = 0; k < 4; ++k) { toneKnobs[k].prepare (sampleRate, 0.030); toneKnobs[k].snap (knobs[k]->load()); }
    toneCountdown = 0;

    activeOs = wantedOs = juce::jlimit (0, 2, static_cast<int> (pOs->load()));
    applyOversamplingChoice (activeOs);
    loadMeasurer.reset (sampleRate, maxBlock);
    latencyWanted.store (computeLatency());
    setLatencySamples (latencyWanted.load());
}

void VFXAampsProcessor::applyOversamplingChoice (int choice) noexcept
{
    activeOs = choice;
    for (size_t f = 1; f < 3; ++f) { if (osTone[f]) osTone[f]->reset(); if (osLim[f]) osLim[f]->reset(); }
    tone[static_cast<size_t> (activeOs)].reset();
    limiterL.prepare (sampleRateHz * (1 << activeOs));
    limiterL.setCeilingDb (-0.3);
    toneCountdown = 0;
}

double VFXAampsProcessor::getTailLengthSeconds() const
{
    namespace P = vfxa::params;
    auto value = [this] (const juce::String& id) -> double
    {
        if (auto* v = apvts.getRawParameterValue (id)) return v->load (std::memory_order_relaxed);
        return 0.0;
    };
    double tail = 0.5; // cab IRs
    if (value (P::rvbOn) > 0.5)
        tail = std::max (tail, value (P::rvbDecay) * std::max (1.0, value (P::rvbLowRT)) + value (P::rvbPre) / 1000.0 + 0.5);
    if (value (P::dlyOn) > 0.5)
    {
        const double seconds = value (P::dlySync) > 0.5 ? 4.0 * 60.0 / std::max (20.0, hostBpm.load (std::memory_order_relaxed)) : value (P::dlyTime) / 1000.0;
        const double fb = std::clamp (value (P::dlyFeedback), 0.001, 0.95);
        const double repeats = 60.0 / (-20.0 * std::log10 (fb)) + 1.0; // repeats until -60 dB
        tail = std::max (tail, seconds * repeats + 0.5);
    }
    return std::min (tail, 60.0);
}

int VFXAampsProcessor::computeLatency() const noexcept
{
    int l = engine.latencySamples();
    for (const auto& pd : pedals) if (pd.running) l += pd.engine.latencySamples(); // 0 at 48 kHz
    if (activeOs > 0)
        l += juce::roundToInt (osTone[static_cast<size_t> (activeOs)]->getLatencyInSamples())
           + juce::roundToInt (osLim[static_cast<size_t> (activeOs)]->getLatencyInSamples());
    return l;
}

vfxa::OptoLeveler::Params VFXAampsProcessor::compParamsFor (int position) noexcept
{
    vfxa::OptoLeveler::Params cp;
    cp.active = pCompOn->load (std::memory_order_relaxed) > 0.5f && position == compPosNow.load (std::memory_order_relaxed)
                && compMove.getTarget() > 0.5;
    cp.peakReduction = pCompPeak->load (std::memory_order_relaxed) / 100.0;
    cp.gainDb = pCompGain->load (std::memory_order_relaxed);
    cp.limit = pCompLimit->load (std::memory_order_relaxed) > 0.5f;
    cp.emphasis = pCompEmph->load (std::memory_order_relaxed);
    cp.mix = pCompMix->load (std::memory_order_relaxed);
    return cp;
}

void VFXAampsProcessor::resetAmpHead()
{
    namespace P = vfxa::params;
    for (int k = 0; k < vfxa::kNumSlots; ++k)
    {
        clearAsset (k);
        for (const auto& id : { P::slotTrim (k), P::slotIr (k) })
            if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->getDefaultValue());
        pendingSlotIr[static_cast<size_t> (k)].store (0);
    }
    for (const auto* id : { &P::bass, &P::mid, &P::treble, &P::presence })
        if (auto* prm = apvts.getParameter (*id)) prm->setValueNotifyingHost (prm->convertTo0to1 (5.0f));
}

void VFXAampsProcessor::applyPendingSlotIrSwitches()
{
    for (int k = 0; k < vfxa::kNumSlots; ++k)
        if (const int want = pendingSlotIr[static_cast<size_t> (k)].exchange (0); want != 0)
            if (auto* prm = apvts.getParameter (vfxa::params::slotIr (k)))
                prm->setValueNotifyingHost (want == 1 ? 1.0f : 0.0f);
}

void VFXAampsProcessor::timerCallback()
{
    engine.collectGarbage();
    for (auto& pd : pedals) pd.engine.collectGarbage();
    applyPendingSlotIrSwitches();
    const int l = latencyWanted.load();
    if (l != getLatencySamples()) setLatencySamples (l);
}

//==============================================================================
void VFXAampsProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { process (b); }
void VFXAampsProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process (b); }

template <typename T>
void VFXAampsProcessor::process (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    // Offline render: make sure every model / IR of the session is in place before rendering.
    if (isNonRealtime() && isAnyAssetLoading()) waitForLoads (30000);

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();

    // host tempo for the synced delay
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 20.0 && *bpm < 400.0) hostBpm.store (*bpm, std::memory_order_relaxed);
    float peakIn = 0.0f, peakOut = 0.0f;

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = std::min (maxBlock, total - start);
        float* x = mono.getWritePointer (0);
        if (numIn > 0)
        {
            const T* in = buffer.getReadPointer (0, start); // guitar on the first input
            for (int i = 0; i < n; ++i) { x[i] = static_cast<float> (in[i]); peakIn = std::max (peakIn, std::abs (x[i])); }
        }
        else
        {
            std::fill (x, x + n, 0.0f);
        }

        processChunk (n);

        const float* l = left.getReadPointer (0);
        const float* r = right.getReadPointer (0);
        if (numOut >= 2)
        {
            T* ol = buffer.getWritePointer (0, start);
            T* orr = buffer.getWritePointer (1, start);
            for (int i = 0; i < n; ++i) { ol[i] = static_cast<T> (l[i]); orr[i] = static_cast<T> (r[i]); }
        }
        else if (numOut == 1)
        {
            T* o = buffer.getWritePointer (0, start);
            for (int i = 0; i < n; ++i) o[i] = static_cast<T> (0.5f * (l[i] + r[i]));
        }
        for (int i = 0; i < n; ++i) peakOut = std::max (peakOut, std::max (std::abs (l[i]), std::abs (r[i])));
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, total);

    inPeak.store (std::max (peakIn, inPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    outPeak.store (std::max (peakOut, outPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
}

template void VFXAampsProcessor::process<float> (juce::AudioBuffer<float>&);
template void VFXAampsProcessor::process<double> (juce::AudioBuffer<double>&);

void VFXAampsProcessor::processChunk (int n) noexcept
{
    float* x = mono.getWritePointer (0);
    float* l = left.getWritePointer (0);
    float* r = right.getWritePointer (0);

    tuner.push (x, n); // the tuner listens to the raw guitar input

    // --- input trim + noise gate (64-bit) ---------------------------------------------
    inTrim.setDb (pInTrim->load (std::memory_order_relaxed));
    gateEnable.setTarget (pGateOn->load (std::memory_order_relaxed) > 0.5f ? 1.0 : 0.0);
    gate.setThresholdDb (pGateThresh->load (std::memory_order_relaxed));
    gate.setRelease (pGateRel->load (std::memory_order_relaxed));
    for (int i = 0; i < n; ++i)
    {
        double v = x[i] * inTrim.next();
        const double g = gate.process (v);
        const double e = gateEnable.next();
        v *= 1.0 - e * (1.0 - g);
        x[i] = static_cast<float> (v);
    }

    // --- compressor position: fade out where it is, move, fade back in ------------------------
    {
        const int want = static_cast<int> (pCompPos->load (std::memory_order_relaxed));
        if (want != compPosNow.load (std::memory_order_relaxed))
        {
            compMove.setTarget (0.0); // makes the compressor fade out (to dry) where it is
            if (! comp.isActive()) { compPosNow.store (want, std::memory_order_relaxed); compMove.setTarget (1.0); }
        }
        compMove.skip (n);
    }
    // --- NAM pedals 1 and 2 (in front of the amp) ------------------------------------------
    pedalParams.normalize = false; // keep the capture's own gain: how hard a boost hits the amp matters
    for (int k = 0; k < kNumPedals; ++k)
    {
        auto& pd = pedals[static_cast<size_t> (k)];
        const bool has = pd.hasModel.load (std::memory_order_relaxed);

        // Start / stop the engine with the capture. Away from 48 kHz that changes the latency, so the signal
        // fades out, the change happens in silence, and it fades back in once the engine's history has settled.
        if (has != pd.running)
        {
            if (pd.engine.latencySamples() == 0) pd.running = has; // 48 kHz: nothing to hide
            else
            {
                pd.gate.setTarget (0.0);
                if (! pd.gate.isSmoothing() && pd.gate.getCurrent() <= 0.0)
                {
                    pd.running = has;
                    pd.dry.clear();
                    pd.hold = has ? 2 * pd.engine.latencySamples() + 64 : 0;
                }
            }
        }
        if (pd.hold <= 0 && pd.gate.getTarget() < 1.0 && has == pd.running) pd.gate.setTarget (1.0);

        const bool on = has && pd.running && pPedalOn[static_cast<size_t> (k)]->load (std::memory_order_relaxed) > 0.5f;
        pd.mix.setTarget (on ? 1.0 : 0.0);
        pd.level.setDb (pPedalLevel[static_cast<size_t> (k)]->load (std::memory_order_relaxed));

        if (pd.running)
        {
            // runs even when switched off, so the model is ready the moment the footswitch is pressed;
            // the dry path is delayed to match the engine (sample-rate conversion away from 48 kHz)
            pd.engine.process (x, pd.wet.data(), n, pedalParams);
            const int lat = pd.engine.latencySamples();
            for (int i = 0; i < n; ++i)
            {
                pd.dry.push (x[i]);
                const double d = pd.dry.read (static_cast<double> (lat) + 1.0);
                const double m = pd.mix.next(), g = pd.level.next();
                x[i] = static_cast<float> (d + m * (pd.wet[static_cast<size_t> (i)] * g - d));
            }
        }
        else
        {
            for (int i = 0; i < n; ++i) { pd.mix.next(); pd.level.next(); }
        }

        if (pd.gate.isSmoothing() || pd.gate.getCurrent() < 1.0 || pd.hold > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                double g = 0.0;
                if (pd.hold > 0) --pd.hold;
                else g = pd.gate.next();
                x[i] = static_cast<float> (x[i] * g);
            }
        }
        pedalMeter[static_cast<size_t> (k)].store (static_cast<float> (pd.mix.getCurrent()), std::memory_order_relaxed);
    }

    // --- compressor, PRE AMP position (mono) ------------------------------------------------
    if (compPosNow.load (std::memory_order_relaxed) == 0) comp.process (x, nullptr, n, compParamsFor (0));

    // --- 10-slot NAM morph ----------------------------------------------------------------
    engineParams.morph = pMorph->load (std::memory_order_relaxed);
    engineParams.normalize = pNormalize->load (std::memory_order_relaxed) > 0.5f;
    for (int k = 0; k < vfxa::kNumSlots; ++k)
        engineParams.slotTrimDb[static_cast<size_t> (k)] = pSlotTrim[static_cast<size_t> (k)]->load (std::memory_order_relaxed);
    engine.process (x, x, n, engineParams);

    // Oversampling changes: fade out, switch, fade back in (latency is updated from the message thread).
    const int osChoice = juce::jlimit (0, 2, static_cast<int> (pOs->load (std::memory_order_relaxed)));
    if (osChoice != wantedOs) { wantedOs = osChoice; switchFade.setTarget (0.0); }

    // --- tone stack + presence (optionally oversampled) ---------------------------------
    const std::array<float, 4> knobTargets { pBass->load(), pMid->load(), pTreble->load(), pPresence->load() };
    for (size_t k = 0; k < 4; ++k) toneKnobs[k].setTarget (knobTargets[k]);
    auto& ts = tone[static_cast<size_t> (activeOs)];
    auto stepKnobs = [this, &ts]
    {
        if (--toneCountdown <= 0)
        {
            toneCountdown = 16;
            ts.update (toneKnobs[0].getCurrent(), toneKnobs[1].getCurrent(), toneKnobs[2].getCurrent(), toneKnobs[3].getCurrent());
        }
        for (auto& k : toneKnobs) k.next();
    };
    if (activeOs == 0)
    {
        for (int i = 0; i < n; ++i) { stepKnobs(); x[i] = static_cast<float> (ts.process (x[i])); }
    }
    else
    {
        float* chans[] = { x };
        juce::dsp::AudioBlock<float> block (chans, 1, static_cast<size_t> (n));
        auto& os = *osTone[static_cast<size_t> (activeOs)];
        auto up = os.processSamplesUp (block);
        const int f = 1 << activeOs;
        float* u = up.getChannelPointer (0);
        for (int i = 0; i < n; ++i)
        {
            stepKnobs();
            for (int j = 0; j < f; ++j) u[i * f + j] = static_cast<float> (ts.process (u[i * f + j]));
        }
        os.processSamplesDown (block);
    }

    // --- dual IR --------------------------------------------------------------------------
    std::array<vfxa::IrParams, kNumIrs> ip;
    for (size_t w = 0; w < kNumIrs; ++w)
    {
        const auto& p = pIr[w];
        ip[w].on = p.on->load() > 0.5f;
        ip[w].levelDb = p.level->load();
        ip[w].pan = p.pan->load();
        ip[w].phaseInvert = p.phase->load() > 0.5f;
        ip[w].delayMs = p.delay->load();
        ip[w].lowCutHz = p.lowcut->load();
        ip[w].highCutHz = p.highcut->load();
    }
    // Each slot's IR switch: the cab amount follows the morph (power share of the slots now playing).
    double slotIrAmount = 1.0;
    {
        double total = 0.0, withIr = 0.0;
        for (int k = 0; k < vfxa::kNumSlots; ++k)
        {
            const double g = engine.slotLevel (k);
            total += g * g;
            if (pSlotIr[static_cast<size_t> (k)]->load (std::memory_order_relaxed) > 0.5f) withIr += g * g;
        }
        if (total > 1e-9) slotIrAmount = withIr / total; // no model loaded: the dry input still goes through the IR
    }
    irs.process (x, l, r, n, pIrOn->load() > 0.5f, pIrBlend->load(), ip[0], ip[1], slotIrAmount);

    // --- compressor, POST CAB position ------------------------------------------------------
    if (compPosNow.load (std::memory_order_relaxed) == 1) comp.process (l, r, n, compParamsFor (1));

    // --- effects rack: graphic EQ -> delay -> reverb ----------------------------------------
    {
        std::array<double, vfxa::GraphicEq::kBands> bands {};
        for (size_t b = 0; b < bands.size(); ++b) bands[b] = pEqBand[b]->load (std::memory_order_relaxed);
        const bool eqOnNow = pEqOn->load (std::memory_order_relaxed) > 0.5f;
        graphicEq.setGains (bands, eqOnNow);

        vfxa::GraphicEq31::Params ep;
        ep.on = eqOnNow;
        ep.link = pEq31Link->load (std::memory_order_relaxed) > 0.5f;
        const double range = pEq31Range->load (std::memory_order_relaxed) > 0.5f ? 15.0 : 6.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            auto& e = ep.ch[c];
            for (size_t b = 0; b < e.gainDb.size(); ++b) e.gainDb[b] = range * pEq31Band[c][b]->load (std::memory_order_relaxed);
            e.inputDb = pEq31Input[c]->load (std::memory_order_relaxed);
            e.lowCut = pEq31LowCut[c]->load (std::memory_order_relaxed) > 0.5f;
            e.limit = pEq31Limit[c]->load (std::memory_order_relaxed) > 0.5f;
            e.thresholdDb = pEq31Thresh[c]->load (std::memory_order_relaxed);
            e.nr = pEq31Nr[c]->load (std::memory_order_relaxed) > 0.5f;
        }

        // VX-10 or VX-31; while switching, both run and the output crossfades
        eqTypeFade.setTarget (pEqType->load (std::memory_order_relaxed) > 0.5f ? 1.0 : 0.0);
        if (! eqTypeFade.isSmoothing())
        {
            if (eqTypeFade.getCurrent() < 0.5) graphicEq.process (l, r, n);
            else eq31.process (l, r, n, ep);
        }
        else
        {
            std::copy (l, l + n, eqTmpL.data());
            std::copy (r, r + n, eqTmpR.data());
            graphicEq.process (l, r, n);
            eq31.process (eqTmpL.data(), eqTmpR.data(), n, ep);
            for (int i = 0; i < n; ++i)
            {
                const double f = eqTypeFade.next();
                l[i] = static_cast<float> (l[i] + f * (eqTmpL[static_cast<size_t> (i)] - l[i]));
                r[i] = static_cast<float> (r[i] + f * (eqTmpR[static_cast<size_t> (i)] - r[i]));
            }
        }

        auto ld = [] (std::atomic<float>* v) { return static_cast<double> (v->load (std::memory_order_relaxed)); };
        vfxa::StudioDelay::Params dp;
        dp.on = ld (pDlyOn) > 0.5;
        dp.mode = static_cast<int> (ld (pDlyMode));
        const bool sync = ld (pDlySync) > 0.5;
        dp.timeMs = sync ? vfxa::params::delayDivisionBeats (static_cast<int> (ld (pDlyDiv))) * 60000.0 / hostBpm.load (std::memory_order_relaxed) : ld (pDlyTime);
        dp.timeMs = std::min (dp.timeMs, vfxa::StudioDelay::kMaxSeconds * 1000.0);
        delayTimeMs.store (static_cast<float> (dp.timeMs), std::memory_order_relaxed);
        dp.feedback = ld (pDlyFb);
        dp.mix = ld (pDlyMix);
        dp.highCutHz = ld (pDlyTone);
        dp.lowCutHz = ld (pDlyLowCut);
        dp.modDepth = ld (pDlyModDepth);
        dp.modRate = ld (pDlyModRate);
        dp.duck = ld (pDlyDuck);
        dp.diffusion = ld (pDlyDiff);
        dp.pingPong = ld (pDlyPing) > 0.5;
        delay.process (l, r, n, dp);

        vfxa::StudioReverb::Params rp;
        rp.on = ld (pRvbOn) > 0.5;
        rp.program = static_cast<int> (ld (pRvbProgram));
        rp.size = ld (pRvbSize);
        rp.decayS = ld (pRvbDecay);
        rp.preDelayMs = ld (pRvbPre);
        rp.highCutHz = ld (pRvbTone);
        rp.lowCutHz = ld (pRvbLowCut);
        rp.diffusion = ld (pRvbDiff);
        rp.mod = ld (pRvbMod);
        rp.lowRT = ld (pRvbLowRT);
        rp.highRT = ld (pRvbHighRT);
        rp.early = ld (pRvbEarly);
        rp.width = ld (pRvbWidth);
        rp.mix = ld (pRvbMix);
        reverb.process (l, r, n, rp);
    }

    // --- compressor, END position ---------------------------------------------------------------
    if (compPosNow.load (std::memory_order_relaxed) == 2) comp.process (l, r, n, compParamsFor (2));

    // --- output level + limiter (optionally oversampled) ----------------------------------
    outGain.setDb (pOut->load (std::memory_order_relaxed));
    limitEnable.setTarget (pLimiter->load (std::memory_order_relaxed) > 0.5f ? 1.0 : 0.0);
    for (int i = 0; i < n; ++i)
    {
        const double g = outGain.next();
        l[i] = static_cast<float> (l[i] * g);
        r[i] = static_cast<float> (r[i] * g);
    }
    if (activeOs == 0)
    {
        for (int i = 0; i < n; ++i)
        {
            const double e = limitEnable.next();
            if (e <= 0.0) continue;
            double a = l[i], b = r[i];
            limiterL.process (a, b);
            l[i] = static_cast<float> (l[i] + e * (a - l[i]));
            r[i] = static_cast<float> (r[i] + e * (b - r[i]));
        }
    }
    else
    {
        // Always routed through the oversampler while it is on, so latency does not depend on the limiter switch.
        float* chans[] = { l, r };
        juce::dsp::AudioBlock<float> block (chans, 2, static_cast<size_t> (n));
        auto& os = *osLim[static_cast<size_t> (activeOs)];
        auto up = os.processSamplesUp (block);
        const int f = 1 << activeOs;
        float* ul = up.getChannelPointer (0);
        float* ur = up.getChannelPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double e = limitEnable.next();
            if (e <= 0.0) continue;
            for (int j = 0; j < f; ++j)
            {
                const int k = i * f + j;
                double a = ul[k], b = ur[k];
                limiterL.process (a, b);
                ul[k] = static_cast<float> (ul[k] + e * (a - ul[k]));
                ur[k] = static_cast<float> (ur[k] + e * (b - ur[k]));
            }
        }
        os.processSamplesDown (block);
    }

    // --- oversampling switch fade ---------------------------------------------------------
    if (switchFade.isSmoothing() || switchFade.getCurrent() < 1.0)
    {
        for (int i = 0; i < n; ++i)
        {
            const double g = switchFade.next();
            l[i] = static_cast<float> (l[i] * g);
            r[i] = static_cast<float> (r[i] * g);
        }
        if (! switchFade.isSmoothing() && switchFade.getCurrent() <= 0.0)
        {
            applyOversamplingChoice (wantedOs);
            switchFade.setTarget (1.0);
        }
    }

    // --- mute while tuning --------------------------------------------------------------
    tunerGate.setTarget (tunerMute.load (std::memory_order_relaxed) ? 0.0 : 1.0);
    if (tunerGate.isSmoothing() || tunerGate.getCurrent() < 1.0)
    {
        for (int i = 0; i < n; ++i)
        {
            const double g = tunerGate.next();
            l[i] = static_cast<float> (l[i] * g);
            r[i] = static_cast<float> (r[i] * g);
        }
    }

    latencyWanted.store (computeLatency(), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* VFXAampsProcessor::createEditor() { return new VFXAampsEditor (*this); }

//==============================================================================
// Assets
void VFXAampsProcessor::setStatus (int asset, const std::function<void (AssetStatus&)>& change)
{
    {
        const juce::ScopedLock sl (statusLock);
        change (status[static_cast<size_t> (asset)]);
    }
    sendChangeMessage();
}

VFXAampsProcessor::AssetStatus VFXAampsProcessor::getAssetStatus (int asset) const
{
    const juce::ScopedLock sl (statusLock);
    return status[static_cast<size_t> (juce::jlimit (0, kNumAssets - 1, asset))];
}

bool VFXAampsProcessor::isAnyAssetLoading() const
{
    const juce::ScopedLock sl (statusLock);
    for (auto& s : status) if (s.loading) return true;
    return false;
}

bool VFXAampsProcessor::waitForLoads (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (isAnyAssetLoading() || loaderPool.getNumJobs() > 0)
    {
        if (juce::Time::getMillisecondCounter() > until) return false;
        juce::Thread::sleep (2);
    }
    return true;
}

void VFXAampsProcessor::loadAsset (int asset, const juce::File& file, bool fromUser)
{
    if (asset < 0 || asset >= kNumAssets) return;
    const int id = ++requestId[static_cast<size_t> (asset)];
    const bool model = isModelAsset (asset);
    setStatus (asset, [&] (AssetStatus& s)
    {
        s.loading = true;
        s.missing = false;
        s.error = {};
        s.path = file.getFullPathName();
        if (! s.loaded) s.name = file.getFileNameWithoutExtension();
    });
    getSettings().setValue (model ? "lastModelDir" : "lastIrDir", file.getParentDirectory().getFullPathName());

    loaderPool.addJob ([this, asset, file, id, model, fromUser]
    {
        auto stale = [&] { return requestId[static_cast<size_t> (asset)].load() != id; };
        if (model)
        {
            auto load = vfxa::MorphEngine::loadModel (file.getFullPathName().toStdString());
            if (stale()) return;
            if (load.model == nullptr)
            {
                // Keep whatever was playing; report the error on the slot.
                const auto err = juce::String (load.error);
                setStatus (asset, [&] (AssetStatus& s) { s.loading = false; s.error = err; if (! s.loaded) s.path = {}; });
                return;
            }
            const auto name = juce::String (load.info.name);
            juce::StringArray detail;
            detail.add (load.info.architecture == "SlimmableContainer" ? juce::String ("A2") : juce::String (load.info.architecture));
            if (load.info.sampleRate > 0) detail.add (juce::String (load.info.sampleRate / 1000.0, load.info.sampleRate == 44100.0 ? 1 : 0) + " kHz");
            if (load.info.gearName().size() > 0) detail.add (juce::String (load.info.gearName()));
            const auto detailText = detail.joinIntoString ("  ");
            if (fromUser && asset < vfxa::kNumSlots)
            {
                // NAM gear types: amp, pedal, pedal_amp, preamp (no cab) / amp_cab, amp_pedal_cab, studio (cab included)
                const juce::String gear = juce::String (load.info.gearType).toLowerCase();
                if (gear.contains ("cab") || gear == "studio") pendingSlotIr[static_cast<size_t> (asset)].store (2);
                else if (gear.isNotEmpty()) pendingSlotIr[static_cast<size_t> (asset)].store (1);
            }
            if (isPedalAsset (asset))
            {
                auto& pd = pedals[static_cast<size_t> (asset - pedalAsset (0))];
                pd.engine.setSlot (0, std::move (load));
                pd.hasModel.store (true);
            }
            else
            {
                engine.setSlot (asset, std::move (load));
            }
            setStatus (asset, [&] (AssetStatus& s) { s.loading = false; s.loaded = true; s.name = name; s.detail = detailText; s.path = file.getFullPathName(); });
        }
        else
        {
            juce::AudioBuffer<float> ir;
            double rate = 0;
            const auto err = vfxa::IrChannel::readIrFile (file, ir, rate);
            if (stale()) return;
            if (err.isNotEmpty())
            {
                setStatus (asset, [&] (AssetStatus& s) { s.loading = false; s.error = err; if (! s.loaded) s.path = {}; });
                return;
            }
            const auto detailText = juce::String (ir.getNumChannels() > 1 ? "stereo  " : "mono  ") + juce::String (rate / 1000.0, 1) + " kHz  "
                                  + juce::String (1000.0 * ir.getNumSamples() / rate, 0) + " ms";
            irs.channel (asset - vfxa::kNumSlots).load (std::move (ir), rate, file.getFullPathName());
            setStatus (asset, [&] (AssetStatus& s) { s.loading = false; s.loaded = true; s.name = file.getFileNameWithoutExtension(); s.detail = detailText; s.path = file.getFullPathName(); });
        }
    });
}

void VFXAampsProcessor::clearAsset (int asset)
{
    if (asset < 0 || asset >= kNumAssets) return;
    ++requestId[static_cast<size_t> (asset)]; // cancels a load in flight
    if (isPedalAsset (asset))
    {
        auto& pd = pedals[static_cast<size_t> (asset - pedalAsset (0))];
        pd.hasModel.store (false); // fades back to dry first; the engine then crossfades the model out on its own
        pd.engine.clearSlot (0);
    }
    else if (isModelAsset (asset)) engine.clearSlot (asset);
    else irs.channel (asset - vfxa::kNumSlots).clear();
    setStatus (asset, [] (AssetStatus& s) { s = AssetStatus(); });
}

juce::Array<int> VFXAampsProcessor::getMissingAssets() const
{
    juce::Array<int> out;
    const juce::ScopedLock sl (statusLock);
    for (int a = 0; a < kNumAssets; ++a) if (status[static_cast<size_t> (a)].missing) out.add (a);
    return out;
}

int VFXAampsProcessor::relinkFromFolder (const juce::File& folder)
{
    int found = 0;
    for (int a : getMissingAssets())
    {
        const auto name = juce::File (getAssetStatus (a).path).getFileName();
        if (name.isEmpty()) continue;
        const auto hits = folder.findChildFiles (juce::File::findFiles, true, name, juce::File::FollowSymlinks::no);
        if (! hits.isEmpty()) { loadAsset (a, hits.getFirst()); ++found; }
    }
    // Remember the folder for future sessions.
    auto list = juce::StringArray::fromLines (getSettings().getValue ("searchFolders"));
    list.addIfNotAlreadyThere (folder.getFullPathName());
    list.removeEmptyStrings();
    getSettings().setValue ("searchFolders", list.joinIntoString ("\n"));
    getSettings().saveIfNeeded();
    return found;
}

//==============================================================================
// State: parameters + file paths (absolute, relative to the home folder, and relative to a preset file).
juce::Array<juce::File> VFXAampsProcessor::searchFolders() const
{
    juce::Array<juce::File> dirs;
    auto& s = const_cast<VFXAampsProcessor*> (this)->getSettings();
    for (auto& p : juce::StringArray::fromLines (s.getValue ("searchFolders")))
        if (p.isNotEmpty()) dirs.addIfNotAlreadyThere (juce::File (p));
    for (auto* key : { "lastModelDir", "lastIrDir" })
        if (auto p = s.getValue (key); p.isNotEmpty()) dirs.addIfNotAlreadyThere (juce::File (p));
    dirs.addIfNotAlreadyThere (userFolder());
    dirs.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("VFXAamps"));
    return dirs;
}

juce::File VFXAampsProcessor::resolve (const juce::String& absPath, const juce::String& relHome, const juce::String& relBase,
                                       const juce::File* base, bool) const
{
    if (absPath.isNotEmpty() && juce::File::isAbsolutePath (absPath) && juce::File (absPath).existsAsFile()) return juce::File (absPath);
    if (base != nullptr && relBase.isNotEmpty())
        if (auto f = base->getChildFile (relBase); f.existsAsFile()) return f;
    if (relHome.isNotEmpty())
        if (auto f = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (relHome); f.existsAsFile()) return f;
    // Last resort: same file name in the folders the user loads from.
    const auto name = juce::File (absPath.isNotEmpty() ? absPath : relHome).getFileName();
    if (name.isNotEmpty())
        for (auto& dir : searchFolders())
            if (dir.isDirectory())
            {
                const auto hits = dir.findChildFiles (juce::File::findFiles, true, name, juce::File::FollowSymlinks::no);
                if (! hits.isEmpty()) return hits.getFirst();
            }
    return {};
}

juce::ValueTree VFXAampsProcessor::saveAssetPaths (const juce::File* relativeTo) const
{
    juce::ValueTree assets (assetsId);
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    for (int a = 0; a < kNumAssets; ++a)
    {
        const auto s = getAssetStatus (a);
        if (s.path.isEmpty()) continue;
        const juce::File f (s.path);
        juce::ValueTree t (assetId);
        t.setProperty (indexId, a, nullptr);
        t.setProperty (pathId, f.getFullPathName(), nullptr);
        if (f.isAChildOf (home)) t.setProperty (relHomeId, f.getRelativePathFrom (home).replaceCharacter ('\\', '/'), nullptr);
        if (relativeTo != nullptr) t.setProperty (relBaseId, f.getRelativePathFrom (*relativeTo).replaceCharacter ('\\', '/'), nullptr);
        assets.appendChild (t, nullptr);
    }
    return assets;
}

void VFXAampsProcessor::restoreAssetPaths (const juce::ValueTree& assets, const juce::File* relativeTo)
{
    std::array<juce::ValueTree, kNumAssets> wanted;
    for (const auto& t : assets)
    {
        const int a = t.getProperty (indexId, -1);
        if (a >= 0 && a < kNumAssets) wanted[static_cast<size_t> (a)] = t;
    }
    for (int a = 0; a < kNumAssets; ++a)
    {
        const auto& t = wanted[static_cast<size_t> (a)];
        if (! t.isValid()) { if (getAssetStatus (a).path.isNotEmpty()) clearAsset (a); continue; }
        const juce::String abs = t.getProperty (pathId).toString();
        const auto f = resolve (abs, t.getProperty (relHomeId).toString(), t.getProperty (relBaseId).toString(), relativeTo, ! isModelAsset (a));
        if (f.existsAsFile())
        {
            if (getAssetStatus (a).path != f.getFullPathName() || ! getAssetStatus (a).loaded) loadAsset (a, f);
        }
        else
        {
            clearAsset (a);
            setStatus (a, [&] (AssetStatus& s) { s.path = abs; s.name = juce::File (abs).getFileNameWithoutExtension(); s.missing = true; });
        }
    }
}

void VFXAampsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (assetsId), nullptr);
    state.appendChild (saveAssetPaths (nullptr), nullptr);
    state.setProperty ("preset", presetName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void VFXAampsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (apvts.state.getType())) return;
    const auto assets = tree.getChildWithName (assetsId).createCopy();
    tree.removeChild (tree.getChildWithName (assetsId), nullptr);
    presetName = tree.getProperty ("preset").toString();
    apvts.replaceState (tree);
    restoreAssetPaths (assets, nullptr);
}

//==============================================================================
juce::File VFXAampsProcessor::userFolder()
{
    // Documents/Nam KNoB. The app was called VFXAamps before: on first use, move that folder (presets, captures, IRs)
    // over; copy it if it can't be moved. Files saved with paths into the old folder are found again by name.
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto dir = docs.getChildFile ("Nam KNoB"), old = docs.getChildFile ("VFXAamps");
    if (! dir.exists() && old.isDirectory() && ! old.moveFileTo (dir))
        old.copyDirectoryTo (dir);
    return dir;
}

juce::File VFXAampsProcessor::getPresetFolder()
{
    return userFolder().getChildFile ("Presets");
}

bool VFXAampsProcessor::savePreset (const juce::File& file)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (assetsId), nullptr);
    const auto dir = file.getParentDirectory();
    state.appendChild (saveAssetPaths (&dir), nullptr);
    juce::XmlElement root ("VFXAampsPreset");
    root.setAttribute ("version", 1);
    if (auto xml = state.createXml()) root.addChildElement (xml.release());
    dir.createDirectory();
    if (! root.writeTo (file)) return false;
    presetName = file.getFileNameWithoutExtension();
    sendChangeMessage();
    return true;
}

bool VFXAampsProcessor::loadPreset (const juce::File& file)
{
    auto root = juce::XmlDocument::parse (file);
    if (root == nullptr || ! root->hasTagName ("VFXAampsPreset") || root->getFirstChildElement() == nullptr) return false;
    auto tree = juce::ValueTree::fromXml (*root->getFirstChildElement());
    if (! tree.hasType (apvts.state.getType())) return false;
    const auto assets = tree.getChildWithName (assetsId).createCopy();
    tree.removeChild (tree.getChildWithName (assetsId), nullptr);
    apvts.replaceState (tree);
    const auto dir = file.getParentDirectory();
    restoreAssetPaths (assets, &dir);
    presetName = file.getFileNameWithoutExtension();
    sendChangeMessage();
    return true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VFXAampsProcessor(); }
