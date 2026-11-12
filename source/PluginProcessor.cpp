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

bool isModelAsset (int asset) noexcept { return asset < vfxa::kNumSlots; }

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
    for (int k = 0; k < vfxa::kNumSlots; ++k) pSlotTrim[static_cast<size_t> (k)] = param (apvts, P::slotTrim (k));
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
    limiterL.setCeilingDb (-0.3);

    inTrim.prepare (sampleRate, 0.020);   inTrim.snapDb (pInTrim->load());
    outGain.prepare (sampleRate, 0.020);  outGain.snapDb (pOut->load());
    gateEnable.prepare (sampleRate, 0.010); gateEnable.snap (pGateOn->load() > 0.5f ? 1.0 : 0.0);
    limitEnable.prepare (sampleRate, 0.020); limitEnable.snap (pLimiter->load() > 0.5f ? 1.0 : 0.0);
    switchFade.prepare (sampleRate, 0.005); switchFade.snap (1.0);
    licenseGate.prepare (sampleRate, 0.250); licenseGate.snap (license->isAudioAllowed() ? 1.0 : 0.0);
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

int VFXAampsProcessor::computeLatency() const noexcept
{
    int l = engine.latencySamples();
    if (activeOs > 0)
        l += juce::roundToInt (osTone[static_cast<size_t> (activeOs)]->getLatencyInSamples())
           + juce::roundToInt (osLim[static_cast<size_t> (activeOs)]->getLatencyInSamples());
    return l;
}

void VFXAampsProcessor::timerCallback()
{
    engine.collectGarbage();
    if (++licenseRefreshTicks >= 20 * 30) // every 30 s: trial countdown, or a key entered in another instance
    {
        licenseRefreshTicks = 0;
        license->refresh();
    }
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
    irs.process (x, l, r, n, pIrOn->load() > 0.5f, pIrBlend->load(), ip[0], ip[1]);

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

    // --- trial ended and no license: fade to silence -------------------------------------
    licenseGate.setTarget (license->isAudioAllowed() ? 1.0 : 0.0);
    if (licenseGate.isSmoothing() || licenseGate.getCurrent() < 1.0)
    {
        for (int i = 0; i < n; ++i)
        {
            const double g = licenseGate.next();
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

void VFXAampsProcessor::loadAsset (int asset, const juce::File& file)
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

    loaderPool.addJob ([this, asset, file, id, model]
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
            engine.setSlot (asset, std::move (load));
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
    if (isModelAsset (asset)) engine.clearSlot (asset);
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
juce::File VFXAampsProcessor::getPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("VFXAamps").getChildFile ("Presets");
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
