// VFXAamps — amp-head editor.

#include "PluginEditor.h"
#include "VFXAampsAssets.h"

namespace vfxa::ui
{
using namespace juce;

namespace
{
// Base-coordinate layout (1240 x 760).
const Rectangle<float> faceplate { 30.0f, 30.0f, 1180.0f, 700.0f };
const Rectangle<int> inputPanel { 48, 112, 186, 386 };
const Rectangle<int> gainPanel { 244, 112, 422, 386 };
const Rectangle<int> tonePanel { 676, 112, 204, 386 };
const Rectangle<int> irPanel { 890, 112, 302, 386 };
const Rectangle<int> slotStrip { 48, 528, 1144, 186 };
const Point<float> knobCentre { 455.0f, 300.0f };
constexpr float knobSize = 222.0f;
constexpr float startAngle = MathConstants<float>::pi * 1.25f, endAngle = MathConstants<float>::pi * 2.75f;
} // namespace

//==============================================================================
AmpFace::RelinkBanner::RelinkBanner()
{
    addAndMakeVisible (findButton);
    addAndMakeVisible (dismissButton);
}

void AmpFace::RelinkBanner::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xf02a1210));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::warn);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setFont (AmpLookAndFeel::labelFont (13.0f, false));
    g.drawFittedText (text, getLocalBounds().reduced (12, 2).withTrimmedRight (250), Justification::centredLeft, 1, 0.8f);
}

void AmpFace::RelinkBanner::resized()
{
    auto r = getLocalBounds().reduced (6, 5);
    dismissButton.setBounds (r.removeFromRight (82));
    r.removeFromRight (6);
    findButton.setBounds (r.removeFromRight (140));
}

//==============================================================================
AmpFace::AmpFace (VFXAampsProcessor& p) : proc (p), tunerDisplay (p)
{
    logoTree = ImageCache::getFromMemory (VFXAampsAssets::logotree_png, VFXAampsAssets::logotree_pngSize);
    logoWord = ImageCache::getFromMemory (VFXAampsAssets::logowordmark_png, VFXAampsAssets::logowordmark_pngSize);
    namespace P = vfxa::params;
    auto& s = proc.apvts;
    inTrim.setup (*this, s, P::inTrim, "INPUT", KnobStyle::ChickenHead);
    gateThresh.setup (*this, s, P::gateThresh, "THRESH", KnobStyle::Small);
    gateRel.setup (*this, s, P::gateRel, "RELEASE", KnobStyle::Small);
    morph.setup (*this, s, P::morph, "", KnobStyle::Knurled);
    morph.slider.setTooltip ("Gain: 1-10 morphs smoothly through the loaded models (e.g. 1.5 = slot 1 and slot 2 equally)");
    bass.setup (*this, s, P::bass, "BASS", KnobStyle::ChickenHead);
    mid.setup (*this, s, P::mid, "MIDDLE", KnobStyle::ChickenHead);
    treble.setup (*this, s, P::treble, "TREBLE", KnobStyle::ChickenHead);
    presence.setup (*this, s, P::presence, "PRESENCE", KnobStyle::ChickenHead);
    output.setup (*this, s, P::outLevel, "MASTER", KnobStyle::ChickenHead);
    blend.setup (*this, s, P::irBlend, "BLEND", KnobStyle::Small, true);

    for (auto* b : { &gateOn, &limiter, &irOn }) { b->getProperties().set ("switch", true); addAndMakeVisible (*b); }
    addAndMakeVisible (normalize);
    gateOn.setTooltip ("Noise gate on/off");
    limiter.setTooltip ("Output limiter (-0.3 dBFS ceiling)");
    irOn.setTooltip ("Cab IRs on/off (off = amp only)");
    normalize.setTooltip ("Level-match models using their loudness metadata (-18 dBFS)");
    gateOnAtt = std::make_unique<ButtonAttachment> (s, P::gateOn, gateOn);
    limiterAtt = std::make_unique<ButtonAttachment> (s, P::limiter, limiter);
    irOnAtt = std::make_unique<ButtonAttachment> (s, P::irOn, irOn);
    normalizeAtt = std::make_unique<ButtonAttachment> (s, P::normalize, normalize);

    osBox.addItemList ({ "OS off", "OS 2x", "OS 4x" }, 1);
    osBox.setTooltip ("Oversampling for the tone stack and limiter (adds a few samples of latency)");
    addAndMakeVisible (osBox);
    osAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, P::oversample, osBox);

    addAndMakeVisible (presetButton);
    addAndMakeVisible (saveButton);
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.setTooltip ("Save a preset (parameters + model and IR files)");
    saveButton.onClick = [this]
    {
        auto dir = VFXAampsProcessor::getPresetFolder();
        dir.createDirectory();
        const auto name = proc.getCurrentPresetName().isNotEmpty() ? proc.getCurrentPresetName() : String ("My Tone");
        chooser = std::make_unique<FileChooser> ("Save preset", dir.getChildFile (name + ".vfxapreset"), "*.vfxapreset");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                              [this] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File()) return;
                                  proc.savePreset (f.withFileExtension ("vfxapreset"));
                              });
    };

    for (int k = 0; k < vfxa::kNumSlots; ++k) { slots[static_cast<size_t> (k)] = std::make_unique<SlotCard> (proc, k); addAndMakeVisible (*slots[static_cast<size_t> (k)]); }
    for (int w = 0; w < 2; ++w) { irColumns[static_cast<size_t> (w)] = std::make_unique<IrColumn> (proc, w); addAndMakeVisible (*irColumns[static_cast<size_t> (w)]); }

    addChildComponent (banner);
    banner.dismissButton.onClick = [this] { bannerDismissed = true; banner.setVisible (false); };
    banner.findButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose a folder to search for the missing files",
                                                 File::getSpecialLocation (File::userHomeDirectory));
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (dir.isDirectory()) proc.relinkFromFolder (dir);
                              });
    };

    addAndMakeVisible (tunerDisplay);
    rackButton.getProperties().set ("rack", true);
    rackButton.getProperties().set ("accent", (int64) 0xff23ded9);
    rackButton.setClickingTogglesState (true);
    rackButton.setTooltip ("Show or hide the effects rack (graphic EQ, delay, reverb)");
    addAndMakeVisible (rackButton);

    setSize (kWidth, kHeight);
    refreshStatus();
}

AmpFace::~AmpFace() = default;

void AmpFace::resized()
{
    auto place = [] (Knob& k, Point<float> c, float size) { k.slider.setBounds (Rectangle<float> (size, size).withCentre (c).toNearestInt()); };

    // header
    presetButton.setBounds (560, 50, 230, 30);
    saveButton.setBounds (796, 50, 60, 30);
    osBox.setBounds (866, 50, 92, 30);
    tunerDisplay.setBounds (256, 44, 292, 44);
    rackButton.setBounds (1110, slotStrip.getY() - 26, 82, 22);

    // input
    place (inTrim, { 141.0f, 205.0f }, 92.0f);
    gateOn.setBounds (60, 290, 40, 50);
    place (gateThresh, { 145.0f, 315.0f }, 50.0f);
    place (gateRel, { 200.0f, 315.0f }, 50.0f);
    normalize.setBounds (66, 412, 26, 26);

    // gain
    place (morph, knobCentre, knobSize);

    // tone + master
    place (bass, { 728.0f, 192.0f }, 78.0f);
    place (mid, { 830.0f, 192.0f }, 78.0f);
    place (treble, { 728.0f, 312.0f }, 78.0f);
    place (presence, { 830.0f, 312.0f }, 78.0f);
    place (output, { 728.0f, 430.0f }, 78.0f);
    limiter.setBounds (810, 405, 40, 50);

    // dual IR
    irOn.setBounds (irPanel.getRight() - 46, irPanel.getY() + 2, 40, 34);
    irColumns[0]->setBounds (irPanel.getX() + 2, irPanel.getY() + 34, 148, 300);
    irColumns[1]->setBounds (irPanel.getX() + 152, irPanel.getY() + 34, 148, 300);
    place (blend, { static_cast<float> (irPanel.getCentreX()), static_cast<float> (irPanel.getBottom() - 34) }, 44.0f);

    // slots
    const int gap = 6, w = (slotStrip.getWidth() - gap * (vfxa::kNumSlots - 1)) / vfxa::kNumSlots;
    for (int k = 0; k < vfxa::kNumSlots; ++k)
        slots[static_cast<size_t> (k)]->setBounds (slotStrip.getX() + k * (w + gap), slotStrip.getY(), w, slotStrip.getHeight());

    banner.setBounds (240, 92, 760, 34);
}

void AmpFace::rebuildPresetList()
{
    const auto name = proc.getCurrentPresetName();
    presetButton.setButtonText (name.isNotEmpty() ? name + String::fromUTF8 ("  \xe2\x96\xbe") : String::fromUTF8 ("PRESETS  \xe2\x96\xbe"));
}

void AmpFace::showPresetMenu()
{
    PopupMenu m;
    const auto dir = VFXAampsProcessor::getPresetFolder();
    auto files = dir.findChildFiles (File::findFiles, true, "*.vfxapreset");
    files.sort();
    int id = 1;
    for (auto& f : files) m.addItem (id++, f.getFileNameWithoutExtension(), true, f.getFileNameWithoutExtension() == proc.getCurrentPresetName());
    if (files.isEmpty()) m.addItem (-1, "(no presets yet - use SAVE)", false);
    m.addSeparator();
    m.addItem (1000, String::fromUTF8 ("Load preset file\xe2\x80\xa6"));
    m.addItem (1001, "Show presets folder");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (presetButton), [this, files] (int r)
    {
        if (r >= 1 && r <= files.size()) proc.loadPreset (files[r - 1]);
        else if (r == 1000)
        {
            chooser = std::make_unique<FileChooser> ("Load preset", VFXAampsProcessor::getPresetFolder(), "*.vfxapreset");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadPreset (fc.getResult()); });
        }
        else if (r == 1001)
        {
            VFXAampsProcessor::getPresetFolder().createDirectory();
            VFXAampsProcessor::getPresetFolder().startAsProcess();
        }
    });
}

void AmpFace::refreshStatus()
{
    for (auto& s : slots) s->refresh();
    for (auto& c : irColumns) c->refresh();
    rebuildPresetList();

    const auto missing = proc.getMissingAssets();
    if (missing.isEmpty()) bannerDismissed = false;
    StringArray names;
    for (int a : missing) names.add (File (proc.getAssetStatus (a).path).getFileName());
    banner.text = String (missing.size()) + (missing.size() == 1 ? " file" : " files") + " not found: " + names.joinIntoString (", ")
                + "  - click a slot to locate it, or search a folder";
    banner.setVisible (! missing.isEmpty() && ! bannerDismissed);
    banner.repaint();
    repaint();
}

void AmpFace::tick()
{
    bool changed = false;
    for (int k = 0; k < vfxa::kNumSlots; ++k)
    {
        const float l = proc.getSlotLevel (k);
        if (std::abs (l - levels[static_cast<size_t> (k)]) > 0.003f) { levels[static_cast<size_t> (k)] = l; changed = true; }
        slots[static_cast<size_t> (k)]->setLevel (l);
    }
    const float c = proc.getCpuLoad();
    const int m = proc.getRunningModels();
    const float ip = proc.getInputPeak();
    if (std::abs (c - cpu) > 0.004f || m != models || std::abs (ip - inPeak) > 0.01f) { cpu = c; models = m; inPeak = ip; changed = true; }
    if (changed) repaint (0, 0, kWidth, slotStrip.getY());
    tunerDisplay.tick();
}

//==============================================================================
void AmpFace::paintBackground (Graphics& g)
{
    const float scale = jlimit (0.5f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::abs (scale - textureScale) > 0.01f || tolex.isNull())
    {
        textureScale = scale;
        tolex = makeTolex (kWidth, kHeight, scale);
        metal = makeBrushedMetal (static_cast<int> (faceplate.getWidth()), static_cast<int> (faceplate.getHeight()), scale);
    }
    const auto all = getLocalBounds().toFloat();

    // tolex cabinet with rounded corners and cream piping
    {
        Graphics::ScopedSaveState s (g);
        Path body;
        body.addRoundedRectangle (all, 18.0f);
        g.reduceClipRegion (body);
        g.drawImage (tolex, all);
    }
    g.setColour (colours::piping.withAlpha (0.85f));
    g.drawRoundedRectangle (all.reduced (14.0f), 12.0f, 2.2f);
    g.setColour (Colour (0x80000000));
    g.drawRoundedRectangle (all.reduced (14.0f).translated (0, 1.5f), 12.0f, 1.0f);

    // chrome corner protectors
    auto corner = [&g] (Point<float> c, float rot)
    {
        Path p;
        p.startNewSubPath (0, 0);
        p.lineTo (46, 0);
        p.quadraticTo (46, 10, 34, 12);
        p.lineTo (12, 12);
        p.lineTo (12, 34);
        p.quadraticTo (10, 46, 0, 46);
        p.closeSubPath();
        const auto t = AffineTransform::rotation (rot).translated (c);
        g.setColour (Colour (0x70000000));
        g.fillPath (p, t.translated (1.5f, 2.5f));
        g.setGradientFill (ColourGradient (Colour (0xfff2f3f5), c, Colour (0xff6d7075), c.translated (40, 40).transformedBy (AffineTransform::rotation (rot, c.x, c.y)), false));
        g.fillPath (p, t);
        AmpLookAndFeel::drawScrew (g, Point<float> (7, 7).transformedBy (t), 3.2f, 0.6f + rot);
    };
    corner ({ 4, 4 }, 0.0f);
    corner ({ all.getRight() - 4, 4 }, MathConstants<float>::halfPi);
    corner ({ all.getRight() - 4, all.getBottom() - 4 }, MathConstants<float>::pi);
    corner ({ 4, all.getBottom() - 4 }, -MathConstants<float>::halfPi);

    // brushed faceplate with a bevel
    g.setColour (Colour (0xa0000000));
    g.fillRoundedRectangle (faceplate.expanded (3.0f).translated (0, 2.0f), 10.0f);
    {
        Graphics::ScopedSaveState s (g);
        Path plate;
        plate.addRoundedRectangle (faceplate, 8.0f);
        g.reduceClipRegion (plate);
        g.drawImage (metal, faceplate);
        g.setGradientFill (ColourGradient (Colour (0x30ffffff), faceplate.getTopLeft(), Colour (0x00000000), faceplate.getTopLeft().translated (0, 60), false));
        g.fillRect (faceplate);
    }
    g.setColour (Colour (0x90ffffff));
    g.drawRoundedRectangle (faceplate.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (Colour (0x70000000));
    g.drawRoundedRectangle (faceplate.expanded (0.5f), 8.5f, 1.0f);
    for (auto p : { Point<float> { 44, 44 }, Point<float> { faceplate.getRight() - 14, 44 },
                    Point<float> { 44, faceplate.getBottom() - 14 }, Point<float> { faceplate.getRight() - 14, faceplate.getBottom() - 14 } })
        AmpLookAndFeel::drawScrew (g, p, 5.0f, p.x * 0.07f);

    // header: logo, pilot light, CPU meter
    {
        // Nameplate badge: navy plate with the tree and the VFXAamps lettering.
        const auto badge = Rectangle<float> (56.0f, 42.0f, 188.0f, 46.0f);
        g.setColour (Colour (0x60000000));
        g.fillRoundedRectangle (badge.translated (0.0f, 2.0f).expanded (1.0f), 7.0f);
        g.setGradientFill (ColourGradient (Colour (0xff1a2a52), badge.getTopLeft(), Colour (0xff0b1430), badge.getBottomLeft(), false));
        g.fillRoundedRectangle (badge, 6.0f);
        g.setGradientFill (ColourGradient (Colour (0xffe8eaee), badge.getTopLeft(), Colour (0xff6b7079), badge.getBottomLeft(), false));
        g.drawRoundedRectangle (badge.reduced (0.6f), 6.0f, 1.2f);
        g.setColour (Colour (0x26ffffff));
        g.drawLine (badge.getX() + 8.0f, badge.getY() + 2.0f, badge.getRight() - 8.0f, badge.getY() + 2.0f, 0.8f);
        const auto treeBox = Rectangle<float> (badge.getX() + 7.0f, badge.getY() + 4.0f, 38.0f, 38.0f);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.setOpacity (1.0f); // drawImage uses the current colour's alpha; the highlight line above left it at ~15%
        if (logoTree.isValid())
            g.drawImage (logoTree, treeBox, RectanglePlacement::centred);
        if (logoWord.isValid())
        {
            const float wh = 26.0f, ww = wh * static_cast<float> (logoWord.getWidth()) / static_cast<float> (logoWord.getHeight());
            const auto wordBox = Rectangle<float> (treeBox.getRight() + 7.0f, badge.getCentreY() - wh * 0.5f, ww, wh);
            g.drawImage (logoWord, wordBox, RectanglePlacement::centred);
        }
    }
    {
        const Point<float> pc { faceplate.getRight() - 52.0f, 65.0f };
        const float glow = 0.85f + 0.15f * jlimit (0.0f, 1.0f, inPeak * 4.0f);
        // chrome bezel
        auto bez = Rectangle<float> (34, 34).withCentre (pc);
        g.setGradientFill (ColourGradient (Colour (0xfff4f4f4), bez.getTopLeft(), Colour (0xff55585d), bez.getBottomRight(), false));
        g.fillEllipse (bez);
        g.setColour (colours::pilotRed.withAlpha (0.35f * glow));
        g.fillEllipse (Rectangle<float> (52, 52).withCentre (pc));
        auto jewel = Rectangle<float> (24, 24).withCentre (pc);
        g.setGradientFill (ColourGradient (Colour (0xffffb2a0).withMultipliedBrightness (glow), pc.translated (-3, -4),
                                           Colour (0xff8a0d07), jewel.getBottomRight(), true));
        g.fillEllipse (jewel);
        // jewel facets
        for (int i = 0; i < 8; ++i)
        {
            const float a = MathConstants<float>::twoPi * static_cast<float> (i) / 8.0f;
            g.setColour (Colours::white.withAlpha (i % 2 ? 0.10f : 0.22f));
            g.drawLine (pc.x, pc.y, pc.x + 11.0f * std::cos (a), pc.y + 11.0f * std::sin (a), 0.7f);
        }
        g.setColour (Colours::white.withAlpha (0.6f));
        g.fillEllipse (Rectangle<float> (6, 4).withCentre (pc.translated (-4, -5)));
        AmpLookAndFeel::drawEngraved (g, "POWER", { pc.x - 30, pc.y + 18, 60, 14 }, 9.5f);

        // CPU / running models
        auto meter = Rectangle<float> (972.0f, 50.0f, 132.0f, 30.0f);
        g.setColour (Colour (0xff0b0c0e));
        g.fillRoundedRectangle (meter, 4.0f);
        g.setColour (Colour (0x40ffffff));
        g.drawRoundedRectangle (meter, 4.0f, 1.0f);
        auto bar = meter.reduced (6.0f, 0.0f).removeFromBottom (6.0f).translated (0, -3.0f);
        g.setColour (Colour (0xff24262b));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (cpu > 0.7f ? colours::warn : colours::amber);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, cpu)), 2.0f);
        g.setFont (AmpLookAndFeel::labelFont (11.5f, false));
        g.setColour (colours::lcdText);
        g.drawText ("CPU " + String (roundToInt (cpu * 100.0f)) + "%   " + String (models) + (models == 1 ? " model" : " models"),
                    meter.withTrimmedBottom (9.0f).translated (0, 2.0f), Justification::centred, false);
    }

    // section titles and dividers
    auto title = [&g] (const String& t, Rectangle<int> panel)
    {
        AmpLookAndFeel::drawEngraved (g, t, panel.toFloat().removeFromTop (22.0f), 13.0f);
    };
    title ("INPUT", inputPanel);
    title ("GAIN", gainPanel);
    title ("TONE", tonePanel);
    for (int x : { inputPanel.getRight() + 5, gainPanel.getRight() + 5 })
    {
        g.setColour (Colour (0x30000000));
        g.drawLine (static_cast<float> (x), 122.0f, static_cast<float> (x), 490.0f, 1.0f);
        g.setColour (Colour (0x50ffffff));
        g.drawLine (static_cast<float> (x) + 1.0f, 122.0f, static_cast<float> (x) + 1.0f, 490.0f, 1.0f);
    }
    AmpLookAndFeel::drawEngraved (g, "GATE", { 52.0f, 342.0f, 56.0f, 14.0f }, 10.0f);
    AmpLookAndFeel::drawEngraved (g, "LIMIT", { 802.0f, 457.0f, 56.0f, 14.0f }, 10.0f);
    AmpLookAndFeel::drawEngraved (g, "LEVEL MATCH", { 98.0f, 418.0f, 110.0f, 14.0f }, 10.0f, Justification::centredLeft);
    AmpLookAndFeel::drawEngraved (g, "MODELS", { 98.0f, 431.0f, 110.0f, 12.0f }, 8.5f, Justification::centredLeft, false);

    // input signal LED (green = signal, red = clipping)
    {
        auto led = Rectangle<float> (10, 10).withCentre ({ 196.0f, 160.0f });
        const auto col = inPeak > 0.89f ? colours::warn : Colour (0xff6dff7a);
        const float a = jlimit (0.0f, 1.0f, inPeak * 6.0f);
        g.setColour (Colour (0xff101112));
        g.fillEllipse (led.expanded (1.5f));
        g.setColour (col.withAlpha (0.15f + 0.85f * a));
        g.fillEllipse (led);
        AmpLookAndFeel::drawEngraved (g, "SIG", { 180.0f, 167.0f, 32.0f, 12.0f }, 8.5f);
    }

    // recessed IR panel
    {
        auto p = irPanel.toFloat();
        g.setColour (Colour (0x70ffffff));
        g.fillRoundedRectangle (p.translated (0, 1.0f), 8.0f);
        g.setGradientFill (ColourGradient (Colour (0xff121316), p.getTopLeft(), Colour (0xff1d1f24), p.getBottomLeft(), false));
        g.fillRoundedRectangle (p, 8.0f);
        g.setColour (Colour (0xa0000000));
        g.drawRoundedRectangle (p, 8.0f, 1.2f);
        g.setFont (AmpLookAndFeel::labelFont (13.0f));
        g.setColour (Colour (0xffe9e2d0));
        g.drawText ("DUAL  CAB  IR", p.reduced (12.0f, 0.0f).removeFromTop (34.0f), Justification::centredLeft, false);
        g.setColour (Colour (0x25ffffff));
        g.drawLine (p.getCentreX(), p.getY() + 42.0f, p.getCentreX(), p.getBottom() - 70.0f, 1.0f);
        g.setFont (AmpLookAndFeel::labelFont (11.0f));
        g.setColour (Colour (0xffcfc8b8));
        const auto bc = blend.slider.getBounds().toFloat().getCentre();
        g.drawText ("A", Rectangle<float> (20, 14).withCentre (bc.translated (-38, 2)), Justification::centred, false);
        g.drawText ("B", Rectangle<float> (20, 14).withCentre (bc.translated (38, 2)), Justification::centred, false);
    }

    AmpLookAndFeel::drawEngraved (g, "NAM MODEL SLOTS", { static_cast<float> (slotStrip.getX()), static_cast<float> (slotStrip.getY()) - 22.0f, 300.0f, 18.0f },
                                  12.0f, Justification::centredLeft);
    AmpLookAndFeel::drawEngraved (g, "drop .nam files on a slot  /  turn GAIN to morph between neighbouring slots",
                                  { 260.0f, static_cast<float> (slotStrip.getY()) - 22.0f, 836.0f, 18.0f }, 10.0f, Justification::centredRight, false);
}

void AmpFace::paintGainScale (Graphics& g)
{
    const auto c = knobCentre;
    const float numR = knobSize * 0.5f + 30.0f;
    const double pos = proc.apvts.getRawParameterValue (vfxa::params::morph)->load();

    // engraved tick ring
    for (int i = 0; i <= 90; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * static_cast<float> (i) / 90.0f;
        const bool major = i % 10 == 0;
        const float r0 = knobSize * 0.5f + 6.0f, r1 = r0 + (major ? 11.0f : 5.0f);
        const auto p0 = c.getPointOnCircumference (r0, a), p1 = c.getPointOnCircumference (r1, a);
        g.setColour (colours::engraveHi);
        g.drawLine (p0.x, p0.y + 1.0f, p1.x, p1.y + 1.0f, major ? 2.0f : 1.0f);
        g.setColour (colours::engraving.withAlpha (major ? 1.0f : 0.6f));
        g.drawLine (p0.x, p0.y, p1.x, p1.y, major ? 2.0f : 1.0f);
    }

    // glowing arc between the two blending slots
    float glowFrom = -1.0f, glowTo = -1.0f;
    for (int k = 0; k < vfxa::kNumSlots; ++k)
        if (levels[static_cast<size_t> (k)] > 0.01f) { if (glowFrom < 0) glowFrom = static_cast<float> (k); glowTo = static_cast<float> (k); }
    if (glowFrom >= 0.0f)
    {
        auto angleOf = [] (double v) { return startAngle + (endAngle - startAngle) * static_cast<float> ((v - 1.0) / 9.0); };
        const float r = knobSize * 0.5f + 12.0f;
        Path arc;
        const float a0 = angleOf (glowFrom + 1.0), a1 = angleOf (glowTo + 1.0);
        if (a1 - a0 > 0.01f) arc.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        for (auto [w, alpha] : { std::pair<float, float> { 14.0f, 0.10f }, { 7.0f, 0.22f }, { 3.0f, 0.85f } })
        {
            g.setColour (colours::amber.withAlpha (alpha));
            g.strokePath (arc, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
        }
        const auto pp = c.getPointOnCircumference (r, angleOf (pos));
        g.setColour (colours::amberHot.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (18, 18).withCentre (pp));
        g.setColour (colours::amberHot);
        g.fillEllipse (Rectangle<float> (7, 7).withCentre (pp));
    }

    // numerals 1..10: each glows with its slot's share of the blend
    for (int k = 0; k < vfxa::kNumSlots; ++k)
    {
        const float a = startAngle + (endAngle - startAngle) * static_cast<float> (k) / 9.0f;
        const auto p = c.getPointOnCircumference (numR, a);
        auto box = Rectangle<float> (34, 24).withCentre (p);
        const float lv = levels[static_cast<size_t> (k)];
        const bool loaded = proc.getAssetStatus (k).loaded;
        if (lv > 0.01f)
        {
            g.setColour (colours::amber.withAlpha (0.28f * lv));
            g.fillEllipse (box.withSizeKeepingCentre (40, 40));
            g.setFont (AmpLookAndFeel::labelFont (21.0f));
            g.setColour (colours::amberHot.interpolatedWith (colours::amber, 1.0f - lv).withAlpha (0.6f + 0.4f * lv));
            g.drawText (String (k + 1), box, Justification::centred, false);
        }
        else
        {
            AmpLookAndFeel::drawEngraved (g, String (k + 1), box, 19.0f);
            if (! loaded)
            {
                g.setColour (Colour (0x55b9b4a8));
                g.drawText (String (k + 1), box, Justification::centred, false);
            }
        }
    }

    // blend readout under the knob
    String text;
    const auto mp = vfxa::morphAt (pos, [this] { vfxa::SlotMask m = 0; for (int k = 0; k < vfxa::kNumSlots; ++k) if (proc.getAssetStatus (k).loaded) m |= 1u << k; return m; }());
    if (mp.lo < 0) text = "load .nam models into the slots below";
    else if (mp.lo == mp.hi || mp.gainHi < 0.005) text = String (mp.lo + 1) + "  " + proc.getAssetStatus (mp.lo).name;
    else
        text = String (mp.lo + 1) + "  " + String (roundToInt (100.0 * mp.gainLo * mp.gainLo)) + "%   " + String::fromUTF8 ("\xe2\x9f\xb7") + "   "
             + String (roundToInt (100.0 * mp.gainHi * mp.gainHi)) + "%  " + String (mp.hi + 1);
    auto lcd = Rectangle<float> (300.0f, 462.0f, 310.0f, 26.0f);
    g.setColour (Colour (0xff0b0c0e));
    g.fillRoundedRectangle (lcd, 5.0f);
    g.setColour (Colour (0x40ffffff));
    g.drawRoundedRectangle (lcd, 5.0f, 1.0f);
    g.setFont (AmpLookAndFeel::labelFont (13.0f, false));
    g.setColour (colours::lcdText);
    g.drawFittedText (text, lcd.reduced (8.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
}

void AmpFace::paint (Graphics& g)
{
    paintBackground (g);
    paintGainScale (g);
}

void AmpFace::paintOverChildren (Graphics& g)
{
    for (auto* k : { &inTrim, &bass, &mid, &treble, &presence, &output })
    {
        const auto b = k->slider.getBounds().toFloat();
        AmpLookAndFeel::drawEngraved (g, k->caption, { b.getX() - 30.0f, b.getBottom() + 1.0f, b.getWidth() + 60.0f, 14.0f }, 11.0f);
    }
    for (auto* k : { &gateThresh, &gateRel })
    {
        const auto b = k->slider.getBounds().toFloat();
        AmpLookAndFeel::drawEngraved (g, k->caption, { b.getX() - 20.0f, b.getBottom() - 1.0f, b.getWidth() + 40.0f, 12.0f }, 9.0f);
    }
    const auto bb = blend.slider.getBounds().toFloat();
    g.setFont (AmpLookAndFeel::labelFont (9.5f));
    g.setColour (Colour (0xffcfc8b8));
    g.drawText ("BLEND", Rectangle<float> (bb.getX() - 20.0f, bb.getBottom() - 2.0f, bb.getWidth() + 40.0f, 13.0f), Justification::centredTop, false);
}

bool AmpFace::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (isModelFile (f) || isIrFile (f)) return true;
    return false;
}

void AmpFace::filesDropped (const StringArray& files, int, int)
{
    // Dropped on the faceplate: models fill the empty slots in order, IRs fill A then B.
    int nextSlot = 0;
    for (auto& f : files)
    {
        if (isModelFile (f))
        {
            while (nextSlot < vfxa::kNumSlots && (proc.getAssetStatus (nextSlot).loaded || proc.getAssetStatus (nextSlot).loading)) ++nextSlot;
            if (nextSlot < vfxa::kNumSlots) proc.loadAsset (nextSlot++, File (f), true);
        }
        else if (isIrFile (f))
        {
            const int a = proc.getAssetStatus (VFXAampsProcessor::irAsset (0)).loaded ? VFXAampsProcessor::irAsset (1) : VFXAampsProcessor::irAsset (0);
            proc.loadAsset (a, File (f), true);
        }
    }
}

} // namespace vfxa::ui

//==============================================================================
VFXAampsEditor::VFXAampsEditor (VFXAampsProcessor& p)
    : AudioProcessorEditor (p), proc (p), face (p), rack (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (face);
    addAndMakeVisible (rack);
    face.rackButton.onClick = [this] { setRackShown (face.rackButton.getToggleState()); };
    setResizable (true, true);

    rackShown = proc.getSettings().getBoolValue ("rackShown", true);
    face.rackButton.setToggleState (rackShown, juce::dontSendNotification);
    rack.setVisible (rackShown);

    using F = vfxa::ui::AmpFace;
    double scale = juce::jlimit (0.55, 1.7, proc.getSettings().getDoubleValue ("uiScale", 0.9));
    // first open with the rack: keep the whole rig on screen
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        scale = juce::jmin (scale, 0.92 * d->userBounds.getHeight() / baseHeight());
    scale = juce::jmax (0.55, scale);
    setRackShown (rackShown);
    setSize (juce::roundToInt (F::kWidth * scale), juce::roundToInt (baseHeight() * scale));
    proc.addChangeListener (this);
    startTimerHz (30);
}

VFXAampsEditor::~VFXAampsEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

int VFXAampsEditor::baseHeight() const noexcept
{
    return vfxa::ui::AmpFace::kHeight + (rackShown ? vfxa::ui::Rack::kHeight : 0);
}

void VFXAampsEditor::setRackShown (bool shown)
{
    using F = vfxa::ui::AmpFace;
    rackShown = shown;
    rack.setVisible (shown);
    face.rackButton.setToggleState (shown, juce::dontSendNotification);
    proc.getSettings().setValue ("rackShown", shown);
    const int h = baseHeight();
    setResizeLimits (F::kWidth * 55 / 100, h * 55 / 100, F::kWidth * 170 / 100, h * 170 / 100);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (static_cast<double> (F::kWidth) / h);
    if (getWidth() > 0) setSize (getWidth(), juce::roundToInt (getWidth() * static_cast<double> (h) / F::kWidth));
}

void VFXAampsEditor::resized()
{
    using F = vfxa::ui::AmpFace;
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (F::kWidth);
    face.setTransform (juce::AffineTransform::scale (scale));
    face.setBounds (0, 0, F::kWidth, F::kHeight);
    rack.setTransform (juce::AffineTransform::scale (scale));
    rack.setBounds (0, F::kHeight, vfxa::ui::Rack::kWidth, vfxa::ui::Rack::kHeight);
    proc.getSettings().setValue ("uiScale", scale);
}

void VFXAampsEditor::timerCallback()
{
    face.tick();
    if (rackShown) rack.tick();
}
void VFXAampsEditor::changeListenerCallback (juce::ChangeBroadcaster*) { face.refreshStatus(); }
