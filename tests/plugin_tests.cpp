// VFXAamps — headless plugin tests: full signal chain, latency, dual IR (stage 3),
// state/presets with missing-file relinking, and an editor snapshot (stage 4).

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <random>

#ifndef VFXA_EXAMPLE_MODELS
 #define VFXA_EXAMPLE_MODELS "."
#endif

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using juce::File;
static const double pi = juce::MathConstants<double>::pi;
static File modelFile (const char* n) { return File (VFXA_EXAMPLE_MODELS).getChildFile (n); }

static void setParam (VFXAampsProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

static File tempDir()
{
    static File d = [] { auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("vfxa_plugin_tests"); f.deleteRecursively(); f.createDirectory(); return f; }();
    return d;
}

static File writeWav (const File& f, const std::vector<float>& data, double rate, int channels = 1)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    auto* w = wav.createWriterFor (os.get(), rate, static_cast<unsigned> (channels), 24, {}, 0);
    if (w == nullptr) return {};
    os.release();
    std::unique_ptr<juce::AudioFormatWriter> writer (w);
    juce::AudioBuffer<float> b (channels, static_cast<int> (data.size()));
    for (int c = 0; c < channels; ++c) b.copyFrom (c, 0, data.data(), static_cast<int> (data.size()));
    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

/** Second-difference spike relative to the local 10 ms RMS of the second difference (a click scores 10+). */
static double clickScore (const std::vector<float>& y, double fs, int skip, int* where = nullptr)
{
    const int n = static_cast<int> (y.size());
    std::vector<double> d (static_cast<size_t> (n), 0.0), pre (static_cast<size_t> (n + 1), 0.0);
    for (int i = 2; i < n; ++i) d[static_cast<size_t> (i)] = std::fabs (y[static_cast<size_t> (i)] - 2.0 * y[static_cast<size_t> (i - 1)] + y[static_cast<size_t> (i - 2)]);
    for (int i = 0; i < n; ++i) pre[static_cast<size_t> (i + 1)] = pre[static_cast<size_t> (i)] + d[static_cast<size_t> (i)] * d[static_cast<size_t> (i)];
    const int w = static_cast<int> (0.01 * fs);
    double worst = 0;
    for (int i = std::max (skip, 2); i < n - w; ++i)
    {
        const int a = std::max (0, i - w), b = i + w;
        const double rms = std::sqrt ((pre[static_cast<size_t> (b)] - pre[static_cast<size_t> (a)]) / (b - a));
        const double s = d[static_cast<size_t> (i)] / (rms + 1e-7);
        if (s > worst) { worst = s; if (where) *where = i; }
    }
    return worst;
}

struct Out { std::vector<float> l, r; };

static void waitForIrs (VFXAampsProcessor& p, std::initializer_list<int> which, double fs, int block);

/** Runs the processor on a mono input (duplicated to both inputs); perBlock(blockIndex, samplePos) may change params. */
template <typename Fn>
static Out run (VFXAampsProcessor& p, const std::vector<float>& x, int block, Fn&& perBlock)
{
    Out o;
    o.l.resize (x.size());
    o.r.resize (x.size());
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0, b = 0; pos < static_cast<int> (x.size()); pos += block, ++b)
    {
        const int n = std::min (block, static_cast<int> (x.size()) - pos);
        perBlock (b, pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, x.data() + pos, n);
        p.processBlock (buf, midi);
        std::copy_n (buf.getReadPointer (0), n, o.l.begin() + pos);
        std::copy_n (buf.getReadPointer (1), n, o.r.begin() + pos);
        p.getEngine().collectGarbage();
    }
    return o;
}
static Out run (VFXAampsProcessor& p, const std::vector<float>& x, int block) { return run (p, x, block, [] (int, int) {}); }

static std::vector<float> sine (double fs, double seconds, double f, double amp = 0.25)
{
    std::vector<float> x (static_cast<size_t> (fs * seconds));
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float> (amp * std::sin (2 * pi * f * static_cast<double> (i) / fs));
    return x;
}

static double rms (const std::vector<float>& y, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; ++i) s += y[i] * y[i];
    return std::sqrt (s / std::max<size_t> (1, to - from));
}

static int peakIndex (const std::vector<float>& y)
{
    int k = 0;
    for (int i = 0; i < static_cast<int> (y.size()); ++i) if (std::fabs (y[static_cast<size_t> (i)]) > std::fabs (y[static_cast<size_t> (k)])) k = i;
    return k;
}

/** Waits for the queued loads, then runs silence until JUCE's background thread has switched the IRs in. */
static void waitForIrs (VFXAampsProcessor& p, std::initializer_list<int> which, double fs, int block)
{
    p.waitForLoads (10000);
    const std::vector<float> silence (static_cast<size_t> (block), 0.0f);
    for (int i = 0; i < 2000; ++i)
    {
        bool ready = true;
        for (int w : which) ready &= p.isIrReady (w);
        if (ready) break;
        run (p, silence, block);
        juce::Thread::sleep (2);
    }
    run (p, std::vector<float> (static_cast<size_t> (fs * 0.1), 0.0f), block); // let the 30 ms fade-ins finish
}

/** A neutral processor: no models (dry), no IR, limiter off, flat tone. */
static void makeNeutral (VFXAampsProcessor& p)
{
    namespace P = vfxa::params;
    setParam (p, P::limiter, 0.0f);
    setParam (p, P::irOn, 0.0f);
}

//==============================================================================
static void testLatency()
{
    std::printf ("Latency reported == measured (dry path)\n");
    for (double fs : { 48000.0, 44100.0, 96000.0 })
        for (int os : { 0, 1, 2 })
        {
            VFXAampsProcessor p;
            makeNeutral (p);
            setParam (p, vfxa::params::oversample, static_cast<float> (os));
            p.prepareToPlay (fs, 256);
            std::vector<float> x (static_cast<size_t> (fs * 0.5), 0.0f);
            x[10000] = 0.5f;
            const auto o = run (p, x, 256);
            const int measured = peakIndex (o.l) - 10000;
            const int reported = p.getLatencySamples();
            CHECK (std::abs (measured - reported) <= 1, "fs %.0f os %d: measured %d, reported %d", fs, os, measured, reported);
            std::printf ("  %6.0f Hz, oversampling %dx: latency %d samples (%.2f ms)\n", fs, 1 << os, reported, 1000.0 * reported / fs);
        }
}

static void testFullChain()
{
    std::printf ("Full chain with NAM + IR, float and double precision\n");
    const double fs = 48000;
    const auto ir = writeWav (tempDir().getChildFile ("cab.wav"), [] {
        std::vector<float> h (2048);
        for (size_t i = 0; i < h.size(); ++i) h[i] = static_cast<float> (std::exp (-static_cast<double> (i) / 200.0) * std::cos (0.3 * static_cast<double> (i)));
        return h; }(), 44100.0);

    VFXAampsProcessor p;
    p.prepareToPlay (fs, 512);
    p.loadAsset (0, modelFile ("wavenet.nam"));
    p.loadAsset (1, modelFile ("lstm.nam"));
    p.loadAsset (VFXAampsProcessor::irAsset (0), ir);
    CHECK (p.waitForLoads (20000), "loads did not finish");
    CHECK (p.getAssetStatus (0).loaded && p.getAssetStatus (1).loaded && p.getAssetStatus (VFXAampsProcessor::irAsset (0)).loaded, "assets not loaded");
    setParam (p, vfxa::params::gateOn, 1.0f);
    setParam (p, vfxa::params::morph, 1.5f);

    const auto x = sine (fs, 2.0, 110.0);
    const auto o = run (p, x, 512);
    bool finite = true;
    for (float v : o.l) finite &= std::isfinite (v);
    CHECK (finite && rms (o.l, 48000, o.l.size()) > 1e-3, "float output rms %.5f", rms (o.l, 48000, o.l.size()));

    // double precision path
    juce::AudioBuffer<double> db (2, 512);
    juce::MidiBuffer midi;
    double peak = 0;
    for (int b = 0; b < 50; ++b)
    {
        for (int i = 0; i < 512; ++i) db.setSample (0, i, 0.25 * std::sin (2 * pi * 110.0 * (b * 512 + i) / fs)), db.setSample (1, i, db.getSample (0, i));
        p.processBlock (db, midi);
        for (int i = 0; i < 512; ++i) peak = std::max (peak, std::fabs (db.getSample (0, i)));
    }
    CHECK (std::isfinite (peak) && peak > 1e-3 && peak <= std::pow (10.0, -0.3 / 20) + 1e-6, "double path peak %.4f (limiter ceiling)", peak);
}

//==============================================================================
static void testIr()
{
    std::printf ("Stage 3: dual IR\n");
    const double fs = 48000;
    std::vector<float> delta (256, 0.0f); delta[0] = 1.0f;
    std::vector<float> delta2 (256, 0.0f); delta2[20] = 1.0f;
    std::vector<float> lowpass (512);
    for (size_t i = 0; i < lowpass.size(); ++i) lowpass[i] = static_cast<float> (std::exp (-static_cast<double> (i) / 30.0));
    const auto irDelta = writeWav (tempDir().getChildFile ("delta.wav"), delta, fs);
    const auto irDelta96 = writeWav (tempDir().getChildFile ("delta96.wav"), std::vector<float> (512, 0.0f), 96000.0);
    const auto irLate = writeWav (tempDir().getChildFile ("late.wav"), delta2, fs);
    const auto irDark = writeWav (tempDir().getChildFile ("dark.wav"), lowpass, fs);
    (void) irDelta96;
    namespace P = vfxa::params;

    auto fresh = [&] (VFXAampsProcessor& p)
    {
        setParam (p, P::limiter, 0.0f);
        p.prepareToPlay (fs, 480);
    };

    // Loaded after the app has been running a while (the IR channel has gone idle): must still switch on.
    {
        VFXAampsProcessor p; fresh (p);
        run (p, std::vector<float> (static_cast<size_t> (fs * 2.0), 0.0f), 480); // 2 s with no IR
        p.loadAsset (VFXAampsProcessor::irAsset (1), irLate);
        waitForIrs (p, { 1 }, fs, 480);
        CHECK (p.isIrReady (1), "IR B loaded after 2 s idle becomes active");
        std::vector<float> imp (4800, 0.0f); imp[1000] = 1.0f;
        const auto o = run (p, imp, 480);
        CHECK (peakIndex (o.l) == 1020, "late-loaded IR is applied (peak at %d, want 1020)", peakIndex (o.l));
    }

    // A only (B empty): plays at full level regardless of blend
    {
        VFXAampsProcessor p; fresh (p);
        p.loadAsset (VFXAampsProcessor::irAsset (0), irDelta);
        waitForIrs (p, { 0 }, fs, 480);
        setParam (p, P::irBlend, 1.0f); // fully towards the empty B
        const auto x = sine (fs, 1.0, 440.0);
        const auto o = run (p, x, 480);
        const double ratio = rms (o.l, 24000, 48000) / rms (x, 24000, 48000);
        CHECK (std::fabs (ratio - 1.0) < 0.02, "single IR with blend at B: gain %.3f", ratio);
        double err = 0;
        for (size_t i = 24000; i < 48000; ++i) err = std::max (err, (double) std::fabs (o.l[i] - x[i]));
        CHECK (err < 1e-3, "delta IR is transparent and zero-latency (max err %.5f)", err);
        if (err >= 1e-3 || std::fabs (ratio - 1.0) >= 0.02)
        {
            std::vector<float> imp (4800, 0.0f); imp[1000] = 1.0f;
            const auto ri = run (p, imp, 480);
            std::printf ("  debug: ready A %d B %d, impulse response L around 1000:", p.isIrReady (0), p.isIrReady (1));
            for (int i = 996; i < 1012; ++i) std::printf (" %.4f", ri.l[static_cast<size_t> (i)]);
            double e = 0; for (float v : ri.l) e += v * v;
            std::printf ("  energy %.4f peak@%d\n", e, peakIndex (ri.l));
            std::printf ("  debug: sine out/in at i=24000..24005:");
            for (size_t i = 24000; i < 24006; ++i) std::printf (" %.4f/%.4f", o.l[i], x[i]);
            std::printf ("\n");
        }
    }

    // pan, phase, delay, cuts
    {
        VFXAampsProcessor p; fresh (p);
        p.loadAsset (VFXAampsProcessor::irAsset (0), irDelta);
        waitForIrs (p, { 0 }, fs, 480);
        setParam (p, P::ir (0, "pan"), -1.0f);
        const auto x = sine (fs, 0.6, 440.0);
        auto o = run (p, x, 480);
        CHECK (rms (o.r, 14400, 28800) < 1e-3 && std::fabs (rms (o.l, 14400, 28800) / rms (x, 14400, 28800) - std::sqrt (2.0)) < 0.03,
               "hard left pan: L %.4f R %.4f", rms (o.l, 14400, 28800), rms (o.r, 14400, 28800));
        setParam (p, P::ir (0, "pan"), 0.0f);
        setParam (p, P::ir (0, "phase"), 1.0f);
        o = run (p, x, 480);
        double sum = 0; for (size_t i = 14400; i < 28800; ++i) sum += o.l[i] * x[i];
        CHECK (sum < 0, "phase invert");
        setParam (p, P::ir (0, "phase"), 0.0f);
        setParam (p, P::ir (0, "delay"), 1.0f); // 1 ms = 48 samples
        std::vector<float> imp (static_cast<size_t> (fs * 0.5), 0.0f); imp[20000] = 1.0f;
        run (p, std::vector<float> (9600, 0.0f), 480); // let the delay ramp settle
        o = run (p, imp, 480);
        CHECK (std::abs (peakIndex (o.l) - 20048) <= 1, "1 ms delay: peak at %d (expected 20048)", peakIndex (o.l));
        setParam (p, P::ir (0, "delay"), 0.0f);
        setParam (p, P::ir (0, "lowcut"), 500.0f);
        setParam (p, P::ir (0, "highcut"), 2000.0f);
        run (p, std::vector<float> (9600, 0.0f), 480);
        const auto lo = run (p, sine (fs, 0.5, 60.0), 480), hi = run (p, sine (fs, 0.5, 12000.0), 480), mid = run (p, sine (fs, 0.5, 1000.0), 480);
        const double dbLo = 20 * std::log10 (rms (lo.l, 12000, 24000) / 0.1768), dbHi = 20 * std::log10 (rms (hi.l, 12000, 24000) / 0.1768),
                     dbMid = 20 * std::log10 (rms (mid.l, 12000, 24000) / 0.1768);
        CHECK (dbLo < -24 && dbHi < -24 && dbMid > -4, "cuts: 60 Hz %.1f dB, 1 kHz %.1f dB, 12 kHz %.1f dB", dbLo, dbMid, dbHi);
    }

    // global IR off -> dry; A/B blend; IR swaps and clears while playing are click-free
    {
        VFXAampsProcessor p; fresh (p);
        p.loadAsset (VFXAampsProcessor::irAsset (0), irDark);
        p.loadAsset (VFXAampsProcessor::irAsset (1), irLate);
        waitForIrs (p, { 0, 1 }, fs, 480);
        const auto x = sine (fs, 6.0, 220.0);
        int phase = -1;
        const auto o = run (p, x, 480, [&] (int, int pos)
        {
            const int ev = pos / 24000; // every 0.5 s
            if (ev == phase) return;
            phase = ev;
            switch (ev)
            {
                case 1: setParam (p, P::irBlend, 1.0f); break;                       // A -> B
                case 2: setParam (p, P::irBlend, 0.3f); break;
                case 3: p.loadAsset (VFXAampsProcessor::irAsset (0), irDelta); break; // swap A while audible
                case 4: setParam (p, P::irOn, 0.0f); break;                           // global off
                case 5: setParam (p, P::irOn, 1.0f); break;
                case 6: p.clearAsset (VFXAampsProcessor::irAsset (1)); break;         // clear B while audible
                case 7: setParam (p, P::ir (0, "phase"), 1.0f); break;
                case 8: setParam (p, P::ir (0, "delay"), 2.0f); break;
                case 9: setParam (p, P::ir (0, "lowcut"), 300.0f); setParam (p, P::ir (0, "highcut"), 3000.0f); break;
                case 10: p.loadAsset (VFXAampsProcessor::irAsset (1), irDark); break;
                default: break;
            }
        });
        // global off section is exactly dry
        double err = 0;
        for (size_t i = 24000 * 4 + 4800; i < 24000 * 5; ++i) err = std::max (err, (double) std::fabs (o.l[i] - x[i]));
        CHECK (err < 1e-5, "IR off = dry (err %.6f)", err);
        int at = 0;
        const double c = clickScore (o.l, fs, 4800, &at);
        CHECK (c < 8.0, "IR automation / swaps click score %.1f at %.3f s", c, at / fs);
        std::printf ("  blend, swap, clear, on/off, phase, delay, cuts while playing: click score %.1f (a click is 10+)\n", c);
    }
}

//==============================================================================
static void testMorphThroughProcessor()
{
    std::printf ("Morph knob automation through the processor (sine, 44.1 kHz)\n");
    const double fs = 44100;
    VFXAampsProcessor p;
    setParam (p, vfxa::params::limiter, 0.0f);
    p.prepareToPlay (fs, 333);
    const char* names[] = { "wavenet.nam", "lstm.nam", "A2.nam", "wavenet.nam", "lstm.nam" };
    for (int k = 0; k < 5; ++k) p.loadAsset (k * 2, modelFile (names[k])); // slots 1,3,5,7,9 (gaps skipped)
    p.waitForLoads (20000);
    const auto x = sine (fs, 4.0, 82.4, 0.2);
    const auto o = run (p, x, 333, [&] (int, int pos)
    {
        const double t = pos / fs;
        setParam (p, vfxa::params::morph, static_cast<float> (t < 2.0 ? 1.0 + 4.5 * t : 10.0 - 4.5 * (t - 2.0)));
    });
    bool finite = true;
    for (float v : o.l) finite &= std::isfinite (v);
    CHECK (finite, "finite");
    CHECK (p.getRunningModels() <= 4, "%d models running", p.getRunningModels());
    std::printf ("  sweep 1->10->1 over 4 s: %d models running at the end, CPU load %.0f%%\n", p.getRunningModels(), 100.0 * p.getCpuLoad());
}

//==============================================================================
static void testStateAndPresets()
{
    std::printf ("State, presets, relative paths, missing-file relink\n");
    const auto lib = tempDir().getChildFile ("Library");
    lib.createDirectory();
    modelFile ("wavenet.nam").copyFileTo (lib.getChildFile ("Clean.nam"));
    modelFile ("lstm.nam").copyFileTo (lib.getChildFile ("Lead.nam"));
    std::vector<float> h (64, 0.0f); h[0] = 1.0f;
    writeWav (lib.getChildFile ("Cab.wav"), h, 48000.0);

    juce::MemoryBlock state;
    {
        VFXAampsProcessor p;
        p.prepareToPlay (48000, 256);
        p.loadAsset (0, lib.getChildFile ("Clean.nam"));
        p.loadAsset (4, lib.getChildFile ("Lead.nam"));
        p.loadAsset (VFXAampsProcessor::irAsset (1), lib.getChildFile ("Cab.wav"));
        p.waitForLoads (10000);
        setParam (p, vfxa::params::morph, 3.25f);
        setParam (p, vfxa::params::slotTrim (4), -4.5f);
        setParam (p, vfxa::params::ir (1, "pan"), 0.4f);
        p.getStateInformation (state);
        CHECK (p.savePreset (lib.getChildFile ("Presets").getChildFile ("My Tone.vfxapreset")), "save preset");
    }
    {
        VFXAampsProcessor p;
        p.prepareToPlay (48000, 256);
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        p.waitForLoads (10000);
        CHECK (p.getAssetStatus (0).loaded && p.getAssetStatus (4).loaded && p.getAssetStatus (VFXAampsProcessor::irAsset (1)).loaded, "assets restored");
        CHECK (! p.getAssetStatus (1).loaded && p.getAssetStatus (1).path.isEmpty(), "empty slot stays empty");
        CHECK (std::fabs (p.apvts.getRawParameterValue (vfxa::params::morph)->load() - 3.25f) < 1e-4, "morph restored");
        CHECK (std::fabs (p.apvts.getRawParameterValue (vfxa::params::slotTrim (4))->load() + 4.5f) < 1e-3, "slot trim restored");
    }

    // Move the whole library (preset + files): the preset finds its files relative to itself.
    const auto moved = tempDir().getChildFile ("MovedLibrary");
    CHECK (lib.moveFileTo (moved), "move library");
    {
        VFXAampsProcessor p;
        p.prepareToPlay (48000, 256);
        CHECK (p.loadPreset (moved.getChildFile ("Presets").getChildFile ("My Tone.vfxapreset")), "load preset");
        p.waitForLoads (10000);
        CHECK (p.getAssetStatus (4).loaded && p.getAssetStatus (4).path == moved.getChildFile ("Lead.nam").getFullPathName(),
               "relative path fallback: %s", p.getAssetStatus (4).path.toRawUTF8());
        CHECK (p.getMissingAssets().isEmpty(), "nothing missing");
    }
    // The session state has only absolute/home-relative paths: files are missing until relinked.
    {
        VFXAampsProcessor p;
        p.getSettings().setValue ("searchFolders", juce::String());
        p.getSettings().setValue ("lastModelDir", juce::String());
        p.getSettings().setValue ("lastIrDir", juce::String());
        p.prepareToPlay (48000, 256);
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        p.waitForLoads (10000);
        const auto missing = p.getMissingAssets();
        CHECK (missing.size() == 3, "3 missing files (got %d)", missing.size());
        CHECK (p.getAssetStatus (4).missing && p.getAssetStatus (4).name == "Lead", "missing slot keeps its name for the relink prompt");
        const int found = p.relinkFromFolder (moved);
        p.waitForLoads (10000);
        CHECK (found == 3 && p.getMissingAssets().isEmpty() && p.getAssetStatus (4).loaded, "relink from folder found %d", found);
    }
}

//==============================================================================
static void testEditorSnapshot (const File& outDir)
{
    std::printf ("Editor snapshot\n");
    VFXAampsProcessor p;
    p.prepareToPlay (48000, 256);
    const char* names[] = { "wavenet.nam", "lstm.nam", "A2.nam", "wavenet_a1_standard.nam", nullptr, "my_model.nam", nullptr, "lstm.nam", "wavenet.nam", nullptr };
    for (int k = 0; k < 10; ++k) if (names[k]) p.loadAsset (k, modelFile (names[k]));
    std::vector<float> h (512);
    for (size_t i = 0; i < h.size(); ++i) h[i] = static_cast<float> (std::exp (-static_cast<double> (i) / 50.0));
    p.loadAsset (VFXAampsProcessor::irAsset (0), writeWav (tempDir().getChildFile ("V30 SM57 Cap.wav"), h, 48000.0));
    p.loadAsset (VFXAampsProcessor::irAsset (1), writeWav (tempDir().getChildFile ("Greenback R121.wav"), h, 48000.0));
    p.waitForLoads (20000);
    setParam (p, vfxa::params::morph, 2.4f);
    setParam (p, vfxa::params::gateOn, 1.0f);
    run (p, sine (48000, 0.3, 110.0), 256); // so the slot glow / meters have values

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (1240, 760);
    for (auto [w, name] : { std::pair<int, const char*> { ed->getWidth(), "vfxaamps-ui.png" }, { 1600, "vfxaamps-ui-large.png" } })
    {
        ed->setSize (w, juce::roundToInt (w * static_cast<double> (ed->getHeight()) / ed->getWidth()));
        if (auto* e = dynamic_cast<VFXAampsEditor*> (ed.get())) e->timerCallback();
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        outDir.createDirectory();
        const auto f = outDir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        CHECK (os.openedOk() && png.writeImageToStream (img, os), "write %s", name);
        std::printf ("  wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
}

//==============================================================================
static void testLicense()
{
    std::printf ("License: trial, mute when the trial ends, key checks\n");
    VFXAampsProcessor p;
    makeNeutral (p);
    const double fs = 48000;
    p.prepareToPlay (fs, 480);
    auto& lic = p.getLicense();
    const auto dir = tempDir().getChildFile ("license");
    dir.deleteRecursively();
    juce::int64 t = juce::Time (2026, 0, 10, 12, 0).toMilliseconds();
    lic.setClockForTesting ([&] { return juce::Time (t); });
    lic.setStorageForTesting (dir.getChildFile ("a/license.xml"), dir.getChildFile ("a/.marker"));
    using S = vfxa::LicenseManager::State;
    CHECK (lic.getState() == S::trial && lic.getTrialDaysLeft() == 14, "fresh trial: %d days", lic.getTrialDaysLeft());

    const auto x = sine (fs, 1.0, 220.0);
    auto o = run (p, x, 480);
    CHECK (rms (o.l, 24000, 48000) > 0.05, "trial passes audio (rms %.4f)", rms (o.l, 24000, 48000));

    t += 15LL * 24 * 3600 * 1000;
    lic.refresh();
    CHECK (lic.getState() == S::expired && ! lic.isAudioAllowed(), "expired after 14 days");
    o = run (p, x, 480);
    CHECK (rms (o.l, 19200, 48000) < 1e-6 && rms (o.r, 19200, 48000) < 1e-6, "expired trial is muted (rms %.2g)", rms (o.l, 19200, 48000));
    double jump = 0;
    for (size_t i = 1; i < 12000; ++i) jump = std::max (jump, (double) std::fabs (o.l[i] - o.l[i - 1]));
    CHECK (jump < 0.05, "mute fades out without a click (max step %.4f)", jump);

    CHECK (lic.activate ("hello").isNotEmpty(), "junk text rejected");
    const juce::String fakePayload = juce::Base64::toBase64 ("VFXAamps|1|Nobody|2026-01-01");
    juce::MemoryBlock junk (256);
    juce::Random (7).fillBitsRandomly (junk.getData(), junk.getSize());
    CHECK (lic.activate ("VFXA1." + fakePayload + "." + juce::Base64::toBase64 (junk.getData(), junk.getSize())).isNotEmpty()
               && lic.getState() == S::expired,
           "key with a forged signature rejected");

    // a fresh trial (new storage, real clock) brings the sound back for the remaining tests
    t = juce::Time::getCurrentTime().toMilliseconds();
    lic.setStorageForTesting (dir.getChildFile ("b/license.xml"), dir.getChildFile ("b/.marker"));
    o = run (p, x, 480);
    CHECK (lic.isAudioAllowed() && rms (o.l, 24000, 48000) > 0.05, "audio returns when allowed again");
    lic.setClockForTesting ([] { return juce::Time::getCurrentTime(); }); // the manager outlives this function's clock
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const File shots = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1]) : tempDir().getChildFile ("screenshots");
    testLatency();
    testFullChain();
    testIr();
    testMorphThroughProcessor();
    testStateAndPresets();
    testLicense();
    testEditorSnapshot (shots);
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
