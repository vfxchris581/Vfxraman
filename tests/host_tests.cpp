// Nam KNoB — host compatibility tests: load the built plug-in (VST3, or AU on macOS) through JUCE's plug-in hosting,
// the way a DAW does, and run it through the situations DAWs put plug-ins in.
//
//   VFXAampsHostTest <path to Nam KNoB.vst3 | AU>
//
// Covers: scan / load, sample rates 44.1 - 192 kHz, block sizes 1 - 8192 (fixed and changing every call), mono and
// stereo layouts, float and double processing, every parameter at random values and automated in the block, state
// save / restore (and junk state), release / re-prepare, reset, bypass, offline (non-realtime) rendering, processing
// on one thread while another changes parameters and saves state, two instances side by side, and the tail length.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>

namespace
{
int failures = 0, checks = 0;

#define CHECK(cond, ...)                                                                \
    do {                                                                                \
        ++checks;                                                                       \
        if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } \
        else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } \
    } while (false)

std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, const juce::PluginDescription& d,
                                                 double sr, int bs)
{
    juce::String error;
    auto p = fm.createPluginInstance (d, sr, bs, error);
    if (p == nullptr) std::printf ("  load error: %s\n", error.toRawUTF8());
    return p;
}

template <typename T>
bool finiteAndSane (const juce::AudioBuffer<T>& b, double& peak)
{
    peak = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double v = b.getSample (c, i);
            if (! std::isfinite (v)) return false;
            peak = std::max (peak, std::abs (v));
        }
    return peak < 64.0;
}

/** A guitar-like test signal: plucked decaying tones with a little noise. */
template <typename T>
void fillGuitar (juce::AudioBuffer<T>& b, double sr, int64_t& pos, std::mt19937& rng)
{
    std::normal_distribution<double> n (0.0, 0.002);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = static_cast<double> (pos + i) / sr;
        const double ph = std::fmod (t, 0.5);
        const double v = 0.4 * std::exp (-6.0 * ph) * (std::sin (2 * juce::MathConstants<double>::pi * 82.4 * t)
                                                       + 0.5 * std::sin (2 * juce::MathConstants<double>::pi * 164.8 * t)) + n (rng);
        for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, static_cast<T> (v));
    }
    pos += b.getNumSamples();
}

template <typename T>
bool run (juce::AudioPluginInstance& p, double sr, int maxBlock, double seconds, bool variableBlocks, std::mt19937& rng,
          double& peak, bool automate = false)
{
    const int ch = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<T> buf (ch, maxBlock);
    juce::MidiBuffer midi;
    std::uniform_int_distribution<int> blk (1, maxBlock);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    auto& params = p.getParameters();
    int64_t pos = 0;
    peak = 0.0;
    const auto total = static_cast<int64_t> (seconds * sr);
    bool ok = true;
    while (pos < total)
    {
        const int n = variableBlocks ? blk (rng) : maxBlock;
        juce::AudioBuffer<T> view (buf.getArrayOfWritePointers(), ch, n);
        fillGuitar (view, sr, pos, rng);
        if (automate && ! params.isEmpty())
            for (int k = 0; k < 4; ++k)
            {
                auto* prm = params[static_cast<int> (rng() % static_cast<unsigned> (params.size()))];
                if (! prm->getName (64).containsIgnoreCase ("bypass")) prm->setValue (u (rng));
            }
        p.processBlock (view, midi);
        double pk = 0.0;
        ok = ok && finiteAndSane (view, pk);
        peak = std::max (peak, pk);
    }
    return ok;
}

void setLayout (juce::AudioPluginInstance& p, const juce::AudioChannelSet& in, const juce::AudioChannelSet& out, bool& accepted)
{
    auto layout = p.getBusesLayout();
    if (! layout.inputBuses.isEmpty()) layout.inputBuses.getReference (0) = in;
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = out;
    accepted = p.setBusesLayout (layout);
}

/** What the host shows for each parameter (switches and stepped controls snap, so compare the displayed values). */
std::vector<juce::String> paramValues (juce::AudioPluginInstance& p)
{
    std::vector<juce::String> v;
    for (auto* prm : p.getParameters()) v.push_back (prm->getCurrentValueAsText());
    return v;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) { std::printf ("usage: %s <plug-in path>\n", argv[0]); return 2; }
    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();

    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);

    std::printf ("Scan\n");
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (path)) f->findAllTypesForFile (found, path);
    CHECK (found.size() == 1, "%s: %d plug-in(s) found", path.toRawUTF8(), found.size());
    if (found.isEmpty()) { std::printf ("%d checks, %d failures\n", checks, failures + 1); return 1; }
    const auto desc = *found[0];
    std::printf ("  %s  (%s, %s, %s)\n", desc.name.toRawUTF8(), desc.pluginFormatName.toRawUTF8(), desc.category.toRawUTF8(),
                 desc.manufacturerName.toRawUTF8());
    CHECK (desc.name == "Nam KNoB", "name is Nam KNoB");
    CHECK (! desc.isInstrument, "registered as an effect");

    std::mt19937 rng (1234);
    double peak = 0.0;

    std::printf ("Sample rates and block sizes\n");
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        auto p = load (fm, desc, sr, 512);
        if (p == nullptr) { CHECK (false, "load at %.0f Hz", sr); continue; }
        p->enableAllBuses();
        for (int bs : { 16, 64, 256, 512, 1024, 4096 })
        {
            p->prepareToPlay (sr, bs);
            const bool ok = run<float> (*p, sr, bs, 0.6, false, rng, peak);
            CHECK (ok && peak > 1.0e-4, "%6.0f Hz, block %4d: output finite and present (peak %.3f)", sr, bs, peak);
            p->releaseResources();
        }
    }

    {
        std::printf ("Odd and changing block sizes\n");
        auto p = load (fm, desc, 48000.0, 8192);
        if (p == nullptr) return 1;
        for (int bs : { 1, 3, 31, 333, 999, 8192 })
        {
            p->prepareToPlay (48000.0, bs);
            const bool ok = run<float> (*p, 48000.0, bs, 0.5, false, rng, peak);
            CHECK (ok, "block %d", bs);
        }
        p->prepareToPlay (48000.0, 2048);
        CHECK (run<float> (*p, 48000.0, 2048, 3.0, true, rng, peak), "random block sizes 1 - 2048 every call (peak %.3f)", peak);
        p->releaseResources();
    }

    {
        std::printf ("Channel layouts\n");
        const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
        struct L { juce::AudioChannelSet in, out; const char* name; };
        for (const auto& l : { L { mono, mono, "mono -> mono" }, L { mono, stereo, "mono -> stereo" }, L { stereo, stereo, "stereo -> stereo" } })
        {
            auto p = load (fm, desc, 48000.0, 512);
            if (p == nullptr) continue;
            bool accepted = false;
            setLayout (*p, l.in, l.out, accepted);
            p->prepareToPlay (48000.0, 512);
            const bool ok = run<float> (*p, 48000.0, 512, 0.8, false, rng, peak);
            CHECK (accepted && ok && peak > 1.0e-4, "%s accepted and processing (peak %.3f)", l.name, peak);
            p->releaseResources();
        }
    }

    {
        std::printf ("Double precision\n");
        auto p = load (fm, desc, 48000.0, 512);
        if (p != nullptr)
        {
            const bool dbl = p->supportsDoublePrecisionProcessing();
            if (dbl)
            {
                p->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
                p->prepareToPlay (48000.0, 512);
                CHECK (run<double> (*p, 48000.0, 512, 0.8, false, rng, peak) && peak > 1.0e-4, "64-bit processing (peak %.3f)", peak);
            }
            else
                CHECK (true, "64-bit processing not offered by this format (host uses 32-bit)");
            p->releaseResources();
        }
    }

    {
        std::printf ("Parameters\n");
        auto p = load (fm, desc, 48000.0, 512);
        if (p == nullptr) return 1;
        p->prepareToPlay (48000.0, 512);
        auto& params = p->getParameters();
        CHECK (params.size() > 50, "%d automatable parameters", params.size());
        int badText = 0;
        for (auto* prm : params)
            for (float v : { 0.0f, 0.25f, 0.5f, 1.0f })
            {
                const auto text = prm->getText (v, 64);
                const float back = prm->getValueForText (text);
                if (text.isEmpty() || ! std::isfinite (back)) ++badText;
            }
        CHECK (badText == 0, "every parameter shows a value and reads it back (%d problems)", badText);
        for (auto* prm : params) if (prm->getName (64).isEmpty()) ++badText;
        CHECK (badText == 0, "every parameter has a name");
        CHECK (run<float> (*p, 48000.0, 512, 6.0, true, rng, peak, true), "parameters automated at random inside blocks for 6 s (peak %.2f)", peak);
        for (int k = 0; k < 6; ++k)
        {
            for (auto* prm : params) prm->setValue (static_cast<float> (rng() % 1001) / 1000.0f);
            CHECK (run<float> (*p, 48000.0, 512, 0.5, false, rng, peak), "random settings #%d: output finite (peak %.2f)", k + 1, peak);
        }
        for (auto* prm : params) prm->setValue (prm->getDefaultValue());
        p->releaseResources();
    }

    {
        std::printf ("State (session save / recall, presets)\n");
        auto a = load (fm, desc, 48000.0, 512);
        auto b = load (fm, desc, 48000.0, 512);
        if (a == nullptr || b == nullptr) return 1;
        for (auto* prm : a->getParameters()) prm->setValue (static_cast<float> (rng() % 1001) / 1000.0f);
        juce::MemoryBlock state;
        a->getStateInformation (state);
        a->setStateInformation (state.getData(), static_cast<int> (state.getSize())); // switches / steps snap
        a->getStateInformation (state);
        CHECK (state.getSize() > 100, "state saved (%zu bytes)", state.getSize());
        b->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        const auto va = paramValues (*a), vb = paramValues (*b);
        int diff = 0;
        for (size_t i = 0; i < va.size() && i < vb.size(); ++i)
            if (va[i] != vb[i])
            {
                if (++diff <= 8)
                    std::printf ("    %s: saved %s, recalled %s\n", a->getParameters()[static_cast<int> (i)]->getName (64).toRawUTF8(),
                                 va[i].toRawUTF8(), vb[i].toRawUTF8());
            }
        CHECK (va.size() == vb.size() && diff == 0, "a second instance recalls every parameter (%d differ)", diff);
        juce::MemoryBlock again;
        b->getStateInformation (again);
        b->setStateInformation (again.getData(), static_cast<int> (again.getSize()));
        CHECK (paramValues (*b) == vb, "save -> load -> save is stable");

        // junk and empty state must not crash or break the plug-in
        const char junk[] = "not a valid state \x01\x02\x03";
        b->setStateInformation (junk, sizeof (junk));
        b->setStateInformation (nullptr, 0);
        std::vector<char> random (4096);
        for (auto& c : random) c = static_cast<char> (rng());
        b->setStateInformation (random.data(), static_cast<int> (random.size()));
        b->prepareToPlay (48000.0, 512);
        CHECK (run<float> (*b, 48000.0, 512, 0.5, false, rng, peak), "junk / empty / random state ignored safely");

        // programs (some hosts list and switch them)
        const int progs = b->getNumPrograms();
        CHECK (progs >= 0, "%d program(s) listed", progs);
        if (progs > 0) { b->setCurrentProgram (0); (void) b->getProgramName (0); }
    }

    {
        std::printf ("Lifecycle\n");
        auto p = load (fm, desc, 44100.0, 256);
        if (p == nullptr) return 1;
        bool ok = true;
        for (int k = 0; k < 8; ++k)
        {
            const double sr = (k % 2) ? 96000.0 : 44100.0;
            p->prepareToPlay (sr, 64 << (k % 4));
            ok = ok && run<float> (*p, sr, 64 << (k % 4), 0.2, false, rng, peak);
            if (k % 3 == 0) p->reset();
            p->releaseResources();
        }
        CHECK (ok, "8 x release / re-prepare with rate and block changes, and reset()");
        p->releaseResources();
        p->releaseResources(); // hosts sometimes release twice
        p->prepareToPlay (48000.0, 512);
        p->prepareToPlay (48000.0, 512); // ... or prepare twice
        CHECK (run<float> (*p, 48000.0, 512, 0.3, false, rng, peak), "double release / double prepare");

        // bypass: through the plug-in's bypass parameter if it has one, else the host's own bypass
        if (auto* bp = p->getBypassParameter())
        {
            bp->setValue (1.0f);
            CHECK (run<float> (*p, 48000.0, 512, 0.3, false, rng, peak), "bypass parameter on");
            bp->setValue (0.0f);
        }
        else
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            int64_t pos = 0;
            fillGuitar (buf, 48000.0, pos, rng);
            p->processBlockBypassed (buf, midi);
            CHECK (finiteAndSane (buf, peak), "processBlockBypassed");
        }

        // offline bounce
        p->setNonRealtime (true);
        p->prepareToPlay (48000.0, 4096);
        CHECK (run<float> (*p, 48000.0, 4096, 2.0, false, rng, peak), "offline (non-realtime) render");
        p->setNonRealtime (false);

        const double tail = p->getTailLengthSeconds();
        CHECK (std::isfinite (tail) && tail >= 0.0 && tail <= 120.0, "tail length %.2f s", tail);
        CHECK (p->getLatencySamples() >= 0 && p->getLatencySamples() < 48000, "latency %d samples", p->getLatencySamples());
        p->releaseResources();
    }

    {
        std::printf ("Threads (audio thread vs. UI / host thread)\n");
        auto p = load (fm, desc, 48000.0, 256);
        if (p == nullptr) return 1;
        p->prepareToPlay (48000.0, 256);
        std::atomic<bool> stop { false }, ok { true };
        std::thread audio ([&] {
            std::mt19937 r2 (99);
            double pk = 0.0;
            while (! stop.load())
                if (! run<float> (*p, 48000.0, 256, 0.05, true, r2, pk)) ok = false;
        });
        auto& params = p->getParameters();
        const auto until = juce::Time::getMillisecondCounter() + 4000;
        int saves = 0;
        while (juce::Time::getMillisecondCounter() < until)
        {
            for (int k = 0; k < 20; ++k)
                params[static_cast<int> (rng() % static_cast<unsigned> (params.size()))]->setValueNotifyingHost (static_cast<float> (rng() % 1001) / 1000.0f);
            juce::MemoryBlock st;
            p->getStateInformation (st);
            if ((saves++ % 5) == 0) p->setStateInformation (st.getData(), static_cast<int> (st.getSize()));
            (void) p->getTailLengthSeconds();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        stop = true;
        audio.join();
        CHECK (ok.load(), "4 s of processing while another thread automates, saves and recalls state (%d saves)", saves);
        p->releaseResources();
    }

    {
        std::printf ("Several instances\n");
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> many;
        for (int k = 0; k < 4; ++k) if (auto p = load (fm, desc, 48000.0, 512)) many.push_back (std::move (p));
        bool ok = many.size() == 4;
        for (auto& p : many) { p->prepareToPlay (48000.0, 512); ok = ok && run<float> (*p, 48000.0, 512, 0.3, false, rng, peak); }
        CHECK (ok, "4 instances side by side");
        for (auto& p : many) p->releaseResources();
        many.clear();
    }

    {
        std::printf ("Editor\n");
        auto p = load (fm, desc, 48000.0, 512);
        if (p != nullptr && p->hasEditor())
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorIfNeeded());
            CHECK (ed != nullptr && ed->getWidth() > 200 && ed->getHeight() > 200, "editor opens (%d x %d)",
                   ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
            ed.reset();
        }
        else
            CHECK (false, "plug-in has an editor");
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
