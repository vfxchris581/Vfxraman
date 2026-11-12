// VFXAamps — effects rack and header tuner.

#include "Rack.h"

namespace vfxa::ui
{
using namespace juce;
namespace P = vfxa::params;

namespace
{
const Colour kText { 0xffd3d8e2 };
const Colour kTextDim { 0xff8d95a6 };

Font monoFont (float h) { return Font (FontOptions (Font::getDefaultMonospacedFontName(), h, Font::bold)); }

float paramValue (VFXAampsProcessor& p, const String& id)
{
    auto* v = p.apvts.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

} // namespace

//==============================================================================
// Header tuner
TunerDisplay::TunerDisplay (VFXAampsProcessor& p) : proc (p)
{
    setTooltip ("Tuner (listens to your guitar input). Click to mute the output while you tune.");
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void TunerDisplay::tick()
{
    const auto r = proc.analyseTuner();
    const auto now = Time::getMillisecondCounter();
    bool changed = false;
    if (r.valid)
    {
        if (! hasNote || r.midiNote != note) { note = r.midiNote; cents = r.cents; changed = true; }
        else
        {
            const double c = cents + 0.35 * (r.cents - cents);
            changed |= std::abs (c - cents) > 0.05;
            cents = c;
        }
        changed |= std::abs (hz - r.hz) > 0.05;
        hz = r.hz;
        hasNote = true;
        lastValid = now;
    }
    else if (hasNote && now - lastValid > 700)
    {
        hasNote = false;
        changed = true;
    }
    const bool m = proc.isTunerMuted();
    if (m != muted) { muted = m; changed = true; }
    if (changed) repaint();
}

void TunerDisplay::mouseUp (const MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    proc.setTunerMute (! proc.isTunerMuted());
    tick();
}

void TunerDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // bezel + screen
    g.setColour (Colour (0xff050607));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (Colour (0x70ffffff));
    g.drawLine (r.getX() + 4.0f, r.getBottom() - 0.5f, r.getRight() - 4.0f, r.getBottom() - 0.5f, 0.8f); // lower lip catches light
    auto s = r.reduced (3.0f);
    g.setGradientFill (ColourGradient (Colour (0xff0c1a1d), s.getTopLeft(), Colour (0xff060c0e), s.getBottomLeft(), false));
    g.fillRoundedRectangle (s, 3.5f);

    const bool inTune = hasNote && std::abs (cents) < 3.0;
    const Colour green (0xff3ef08f), amber (0xffffb347), dim (0x40a0e8e0);
    const Colour lit = ! hasNote ? dim : (inTune ? green : amber);

    // note name
    auto noteBox = s.removeFromLeft (54.0f);
    if (hasNote)
    {
        const String name (TunerReading::noteName (note));
        g.setColour (lit.withAlpha (0.25f));
        g.setFont (monoFont (27.0f));
        g.drawText (name.substring (0, 1), noteBox.withTrimmedRight (14.0f).translated (0.0f, 0.5f).expanded (1.0f), Justification::centredRight);
        g.setColour (lit);
        g.drawText (name.substring (0, 1), noteBox.withTrimmedRight (14.0f), Justification::centredRight);
        g.setFont (monoFont (11.0f));
        if (name.length() > 1) g.drawText ("#", noteBox.withTrimmedLeft (40.0f).withHeight (18.0f).translated (0.0f, 4.0f), Justification::centredLeft);
        g.setColour (lit.withAlpha (0.75f));
        g.drawText (String (note / 12 - 1), noteBox.withTrimmedLeft (40.0f).withTrimmedTop (18.0f).withTrimmedBottom (4.0f), Justification::centredLeft);
    }
    else
    {
        g.setColour (dim);
        g.setFont (monoFont (22.0f));
        g.drawText (String::fromUTF8 ("\xe2\x80\x93"), noteBox.withTrimmedRight (10.0f), Justification::centred);
    }

    // frequency / status on the right
    auto info = s.removeFromRight (62.0f);
    g.setFont (monoFont (10.0f));
    if (muted)
    {
        g.setColour (Colour (0xffff5a4a));
        g.drawText ("MUTE", info.removeFromTop (info.getHeight() * 0.5f), Justification::centred);
    }
    else
    {
        g.setColour (dim.withAlpha (0.55f));
        g.drawText ("TUNER", info.removeFromTop (info.getHeight() * 0.5f), Justification::centred);
    }
    g.setColour (hasNote ? lit.withAlpha (0.8f) : dim.withAlpha (0.35f));
    g.drawText (hasNote ? (hz < 100.0 ? String (hz, 1) : String (roundToInt (hz))) + " Hz" : String ("A 440"), info, Justification::centred);

    // cents scale: -50 .. +50 with a needle
    auto scale = s.reduced (6.0f, 7.0f);
    const float cx = scale.getCentreX();
    auto xAt = [&] (double c) { return cx + static_cast<float> (jlimit (-50.0, 50.0, c) / 50.0) * scale.getWidth() * 0.5f; };
    g.setColour (Colour (0x183ef08f));
    g.fillRect (Rectangle<float> (xAt (-3.0), scale.getY(), xAt (3.0) - xAt (-3.0), scale.getHeight()));
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = xAt (c), len = c == 0 ? scale.getHeight() : (c % 50 == 0 ? scale.getHeight() * 0.6f : scale.getHeight() * 0.35f);
        g.setColour (c == 0 ? Colour (0x803ef08f) : Colour (0x40a0e8e0));
        g.drawLine (x, scale.getBottom() - len, x, scale.getBottom(), c == 0 ? 1.2f : 0.8f);
    }
    if (hasNote)
    {
        const float x = xAt (cents);
        g.setColour (lit.withAlpha (0.3f));
        g.fillRect (Rectangle<float> (7.0f, scale.getHeight() + 4.0f).withCentre ({ x, scale.getCentreY() }));
        g.setColour (lit);
        g.fillRect (Rectangle<float> (2.6f, scale.getHeight() + 4.0f).withCentre ({ x, scale.getCentreY() }));
        // flat / sharp arrows
        g.setFont (monoFont (9.0f));
        g.setColour (cents < -3.0 ? amber : dim.withAlpha (0.3f));
        g.drawText (String::fromUTF8 ("\xe2\x97\x80"), Rectangle<float> (12.0f, 12.0f).withCentre ({ scale.getX() - 1.0f, scale.getY() + 4.0f }), Justification::centred);
        g.setColour (cents > 3.0 ? amber : dim.withAlpha (0.3f));
        g.drawText (String::fromUTF8 ("\xe2\x96\xb6"), Rectangle<float> (12.0f, 12.0f).withCentre ({ scale.getRight() + 1.0f, scale.getY() + 4.0f }), Justification::centred);
    }
}

//==============================================================================
// Rack units
RackUnit::RackUnit (VFXAampsProcessor& p, String m, String t, Colour face, Colour acc)
    : proc (p), model (std::move (m)), title (std::move (t)), faceColour (face), accent (acc)
{
    power.getProperties().set ("rack", true);
    power.getProperties().set ("accent", (int64) accent.getARGB());
    power.setClickingTogglesState (true);
    addAndMakeVisible (power);
}

bool RackUnit::isOn() const { return power.getToggleState(); }

void RackUnit::layoutPowerButton() { power.setBounds (kEar + 16, getHeight() - 38, 64, 24); }

void RackUnit::setupKnob (Knob& k, const String& paramId, const String& caption, bool bipolar)
{
    k.setup (*this, proc.apvts, paramId, caption, KnobStyle::Rack, bipolar);
    k.slider.getProperties().set ("accent", (int64) accent.getARGB());
    k.slider.setMouseDragSensitivity (260);
}

void RackUnit::setupButton (TextButton& b, std::unique_ptr<ButtonAttachment>& att, const String& paramId)
{
    b.getProperties().set ("rack", true);
    b.getProperties().set ("accent", (int64) accent.getARGB());
    b.setClickingTogglesState (true);
    if (auto* prm = proc.apvts.getParameter (paramId)) b.setTooltip (prm->getName (64));
    addAndMakeVisible (b);
    att = std::make_unique<ButtonAttachment> (proc.apvts, paramId, b);
}

void RackUnit::setupChoiceButtons (TextButton* buttons, int count, const String& paramId, const StringArray& labels)
{
    for (int i = 0; i < count; ++i)
    {
        auto& b = buttons[i];
        b.setButtonText (labels[i]);
        b.getProperties().set ("rack", true);
        b.getProperties().set ("radio", true);
        b.getProperties().set ("accent", (int64) accent.getARGB());
        b.onClick = [this, paramId, i]
        {
            if (auto* prm = proc.apvts.getParameter (paramId)) prm->setValueNotifyingHost (prm->convertTo0to1 (static_cast<float> (i)));
        };
        addAndMakeVisible (b);
    }
    updateChoiceButtons (buttons, count, paramId);
}

void RackUnit::updateChoiceButtons (TextButton* buttons, int count, const String& paramId)
{
    const int v = roundToInt (paramValue (proc, paramId));
    for (int i = 0; i < count; ++i) buttons[i].setToggleState (i == v, dontSendNotification);
}

void RackUnit::layoutGrid (std::initializer_list<Component*> row1, std::initializer_list<Component*> row2)
{
    // six columns from x 506; knobs 54 px, rows centred at y 46 and 118
    auto place = [] (std::initializer_list<Component*> row, int cy)
    {
        int c = 0;
        for (auto* comp : row)
        {
            const Point<int> centre { 559 + c * 107, cy };
            if (dynamic_cast<Slider*> (comp) != nullptr) comp->setBounds (Rectangle<int> (54, 54).withCentre (centre));
            else comp->setBounds (Rectangle<int> (100, 26).withCentre (centre));
            ++c;
        }
    };
    place (row1, 46);
    place (row2, 118);
}

void RackUnit::drawCaptions (Graphics& g, std::initializer_list<Knob*> knobs) const
{
    g.setFont (AmpLookAndFeel::labelFont (10.0f));
    for (auto* k : knobs)
    {
        const auto b = k->slider.getBounds().toFloat();
        g.setColour (k->slider.isEnabled() ? kText : kTextDim.withAlpha (0.5f));
        g.drawText (k->caption, Rectangle<float> (b.getCentreX() - 50.0f, b.getBottom() + 1.0f, 100.0f, 14.0f), Justification::centred);
    }
}

void RackUnit::drawDisplay (Graphics& g, Rectangle<float> r, const String& big, const String& unit, const String& small, bool lit) const
{
    g.setColour (Colour (0xff030304));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colour (0x30ffffff));
    g.drawLine (r.getX() + 4.0f, r.getBottom() - 0.5f, r.getRight() - 4.0f, r.getBottom() - 0.5f, 0.8f);
    auto s = r.reduced (3.0f);
    g.setGradientFill (ColourGradient (accent.withMultipliedBrightness (0.10f).withSaturation (0.6f), s.getTopLeft(),
                                       Colour (0xff040506), s.getBottomLeft(), false));
    g.fillRoundedRectangle (s, 3.0f);

    const auto col = lit ? accent : accent.withAlpha (0.28f);
    auto textArea = s.reduced (10.0f, 4.0f);
    auto bigArea = textArea.removeFromTop (textArea.getHeight() * 0.66f);
    const float bigH = jmin (34.0f, bigArea.getHeight());
    // unlit segments behind, like a real LED display
    g.setFont (monoFont (bigH));
    g.setColour (accent.withAlpha (0.06f));
    g.drawText ("8888", bigArea.withTrimmedRight (30.0f), Justification::centredRight);
    auto drawGlow = [&] (const String& t, Rectangle<float> a, Justification j)
    {
        if (lit)
        {
            g.setColour (col.withAlpha (0.22f));
            for (auto d : { Point<float> (-0.8f, 0.0f), Point<float> (0.8f, 0.0f), Point<float> (0.0f, 0.8f), Point<float> (0.0f, -0.8f) })
                g.drawText (t, a.translated (d.x, d.y), j);
        }
        g.setColour (col);
        g.drawText (t, a, j);
    };
    drawGlow (big, bigArea.withTrimmedRight (30.0f), Justification::centredRight);
    g.setFont (monoFont (13.0f));
    drawGlow (unit, bigArea.removeFromRight (28.0f).withTrimmedBottom (4.0f), Justification::bottomLeft);
    g.setFont (monoFont (11.0f));
    drawGlow (small, textArea, Justification::centredRight);
}

void RackUnit::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // faceplate (ears are part of the same plate)
    g.setGradientFill (ColourGradient (faceColour.brighter (0.18f), r.getTopLeft(), faceColour.darker (0.25f), r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, 2.5f);
    // brushed grain
    Random rng (static_cast<int64> (model.hashCode()));
    for (int i = 0; i < 140; ++i)
    {
        const float y = rng.nextFloat() * r.getHeight();
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.025f + rng.nextFloat() * 0.03f));
        g.drawHorizontalLine (static_cast<int> (y), r.getX() + 2.0f, r.getRight() - 2.0f);
    }
    // bevel
    g.setColour (Colour (0x40ffffff));
    g.drawLine (r.getX() + 2.0f, 0.6f, r.getRight() - 2.0f, 0.6f, 1.0f);
    g.setColour (Colour (0x90000000));
    g.drawLine (r.getX() + 2.0f, r.getBottom() - 0.6f, r.getRight() - 2.0f, r.getBottom() - 0.6f, 1.2f);
    g.setColour (Colour (0x60000000));
    g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);

    // ears: a pressed seam, an oval slot and a screw top and bottom
    for (float seam : { static_cast<float> (kEar), r.getRight() - kEar })
    {
        g.setColour (Colour (0x70000000));
        g.drawLine (seam, 3.0f, seam, r.getBottom() - 3.0f, 1.2f);
        g.setColour (Colour (0x22ffffff));
        g.drawLine (seam + 1.2f, 3.0f, seam + 1.2f, r.getBottom() - 3.0f, 0.8f);
    }
    for (float x : { kEar * 0.5f, r.getRight() - kEar * 0.5f })
        for (float y : { 18.0f, r.getBottom() - 18.0f })
        {
            g.setColour (Colour (0xff050506));
            g.fillRoundedRectangle (Rectangle<float> (22.0f, 13.0f).withCentre ({ x, y }), 6.5f);
            AmpLookAndFeel::drawScrew (g, { x, y }, 5.0f, x * 0.11f + y * 0.07f);
        }

    // maker's block
    const float bx = kEar + 16.0f;
    g.setFont (Font (FontOptions (11.0f, Font::bold)).withExtraKerningFactor (0.25f));
    g.setColour (accent);
    g.drawText ("VFXA", Rectangle<float> (bx, 12.0f, 120.0f, 14.0f), Justification::centredLeft);
    g.setFont (Font (FontOptions (23.0f, Font::bold | Font::italic)).withExtraKerningFactor (0.02f));
    g.setColour (Colour (0xfff2f4f8));
    g.drawText (model, Rectangle<float> (bx, 25.0f, 190.0f, 26.0f), Justification::centredLeft);
    g.setFont (AmpLookAndFeel::labelFont (10.0f));
    g.setColour (kTextDim);
    g.drawText (title, Rectangle<float> (bx, 51.0f, 190.0f, 14.0f), Justification::centredLeft);
    // groove between the maker's block and the controls
    g.setColour (Colour (0x60000000));
    g.drawLine (kEar + 206.0f, 10.0f, kEar + 206.0f, r.getBottom() - 10.0f, 1.0f);
    g.setColour (Colour (0x18ffffff));
    g.drawLine (kEar + 207.2f, 10.0f, kEar + 207.2f, r.getBottom() - 10.0f, 0.8f);

    paintPanel (g);
}

//==============================================================================
EqUnit::EqUnit (VFXAampsProcessor& p) : RackUnit (p, "VX-10", "GRAPHIC EQUALIZER", Colour (0xff2b2d33), Colour (0xffffa63a))
{
    powerAtt = std::make_unique<ButtonAttachment> (proc.apvts, P::eqOn, power);
    power.setTooltip ("EQ on/off");
    for (int b = 0; b < 10; ++b)
    {
        auto& f = faders[static_cast<size_t> (b)];
        f.setSliderStyle (Slider::LinearVertical);
        f.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        f.setPopupDisplayEnabled (true, true, nullptr, 1200);
        f.getProperties().set ("fader", true);
        f.getProperties().set ("accent", (int64) accent.getARGB());
        f.setDoubleClickReturnValue (true, 0.0);
        f.setTooltip (String (P::eqBandLabel (b)) + (b < 5 ? " Hz" : "Hz") + "  (double-click: 0 dB)");
        addAndMakeVisible (f);
        faderAtt[static_cast<size_t> (b)] = std::make_unique<SliderAttachment> (proc.apvts, P::eqBand (b), f);
    }
    flat.getProperties().set ("rack", true);
    flat.getProperties().set ("accent", (int64) accent.getARGB());
    flat.setTooltip ("Set every band to 0 dB");
    flat.onClick = [this]
    {
        for (int b = 0; b < 10; ++b)
            if (auto* prm = proc.apvts.getParameter (P::eqBand (b)))
                prm->setValueNotifyingHost (prm->convertTo0to1 (0.0f));
    };
    addAndMakeVisible (flat);
}

Rectangle<float> EqUnit::screen() const { return { 916.0f, 16.0f, static_cast<float> (getWidth() - kEar - 16) - 916.0f, static_cast<float> (getHeight()) - 32.0f }; }

void EqUnit::resized()
{
    layoutPowerButton();
    flat.setBounds (kEar + 88, getHeight() - 38, 64, 24);
    for (int b = 0; b < 10; ++b) faders[static_cast<size_t> (b)].setBounds (290 + b * 60, 14, 40, getHeight() - 46);
}

void EqUnit::tick()
{
    bool changed = isOn() != shownOn;
    for (int b = 0; b < 10; ++b)
    {
        const float v = paramValue (proc, P::eqBand (b));
        changed |= std::abs (v - shown[static_cast<size_t> (b)]) > 0.01f;
        shown[static_cast<size_t> (b)] = v;
    }
    shownOn = isOn();
    for (auto& f : faders) f.setAlpha (shownOn ? 1.0f : 0.65f);
    if (changed) repaint (screen().toNearestInt().expanded (2));
}

void EqUnit::paintPanel (Graphics& g)
{
    // scale lines through the fader travel (sliders keep a 12 px margin at both ends)
    const auto& f0 = faders[0];
    const float top = static_cast<float> (f0.getY() + 12), bottom = static_cast<float> (f0.getBottom() - 12), mid = (top + bottom) * 0.5f;
    const float x0 = static_cast<float> (f0.getX()) - 26.0f, x1 = static_cast<float> (faders[9].getRight()) + 6.0f;
    g.setFont (AmpLookAndFeel::labelFont (9.0f));
    for (auto [y, label] : { std::pair<float, const char*> { top, "+12" }, { mid, "0" }, { bottom, "-12" },
                             { (top + mid) * 0.5f, "+6" }, { (mid + bottom) * 0.5f, "-6" } })
    {
        g.setColour (Colour (label[1] == '6' ? 0x14ffffff : 0x28ffffff));
        g.drawHorizontalLine (static_cast<int> (y), x0 + 24.0f, x1);
        g.setColour (kTextDim);
        g.drawText (label, Rectangle<float> (x0 - 4.0f, y - 7.0f, 26.0f, 14.0f), Justification::centredRight);
    }
    g.setFont (AmpLookAndFeel::labelFont (10.0f));
    g.setColour (kText);
    for (int b = 0; b < 10; ++b)
    {
        const auto fb = faders[static_cast<size_t> (b)].getBounds().toFloat();
        g.drawText (P::eqBandLabel (b), Rectangle<float> (fb.getCentreX() - 30.0f, fb.getBottom() + 2.0f, 60.0f, 14.0f), Justification::centred);
    }

    // response screen
    const auto r = screen();
    g.setColour (Colour (0xff030304));
    g.fillRoundedRectangle (r, 4.0f);
    auto s = r.reduced (3.0f);
    g.setGradientFill (ColourGradient (accent.withMultipliedBrightness (0.10f).withSaturation (0.6f), s.getTopLeft(), Colour (0xff040506), s.getBottomLeft(), false));
    g.fillRoundedRectangle (s, 3.0f);
    auto plot = s.reduced (8.0f, 10.0f);
    g.setColour (accent.withAlpha (0.10f));
    for (int b = 0; b < 10; ++b)
    {
        const float x = plot.getX() + plot.getWidth() * (static_cast<float> (b) + 0.5f) / 10.0f;
        g.drawVerticalLine (static_cast<int> (x), plot.getY(), plot.getBottom());
    }
    g.drawHorizontalLine (static_cast<int> (plot.getCentreY()), plot.getX(), plot.getRight());
    Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const double oct = -0.5 + 10.0 * i / 120.0; // octaves from 31 Hz
        double db = 0.0;
        for (int b = 0; b < 10; ++b)
        {
            const double d = oct - b;
            db += shown[static_cast<size_t> (b)] * std::exp (-d * d / (2.0 * 0.42 * 0.42));
        }
        if (! shownOn) db = 0.0;
        const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / 120.0f;
        const float y = plot.getCentreY() - static_cast<float> (jlimit (-15.0, 15.0, db) / 15.0) * plot.getHeight() * 0.5f;
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    const auto col = shownOn ? accent : accent.withAlpha (0.3f);
    g.setColour (col.withAlpha (0.25f));
    g.strokePath (curve, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (curve, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setFont (monoFont (9.0f));
    g.setColour (col.withAlpha (0.6f));
    g.drawText (shownOn ? "EQ" : "BYPASS", s.reduced (6.0f, 3.0f), Justification::topLeft);
}

//==============================================================================
DelayUnit::DelayUnit (VFXAampsProcessor& p) : RackUnit (p, "VX-D8", "STUDIO MULTI-DELAY", Colour (0xff16171b), Colour (0xff23ded9))
{
    powerAtt = std::make_unique<ButtonAttachment> (proc.apvts, P::dlyOn, power);
    power.setTooltip ("Delay on/off (repeats ring out when switched off)");
    setupKnob (time, P::dlyTime, "TIME");
    setupKnob (division, P::dlyDiv, "NOTE");
    setupKnob (feedback, P::dlyFeedback, "FEEDBACK");
    setupKnob (diffusion, P::dlyDiff, "DIFFUSION");
    setupKnob (mix, P::dlyMix, "MIX");
    setupKnob (lowCut, P::dlyLowCut, "LOW CUT");
    setupKnob (highCut, P::dlyTone, "HIGH CUT");
    setupKnob (modDepth, P::dlyModDepth, "MOD");
    setupKnob (modRate, P::dlyModRate, "RATE");
    setupKnob (duck, P::dlyDuck, "DUCK");
    setupButton (sync, syncAtt, P::dlySync);
    setupButton (ping, pingAtt, P::dlyPing);
    sync.setTooltip ("Sync the delay time to the host tempo (set the note value with NOTE)");
    duck.slider.setTooltip ("Ducking: the repeats get quieter while you play and swell back when you stop");
    diffusion.slider.setTooltip ("Diffusion: smears each repeat into an ambient wash");
    setupChoiceButtons (modes.data(), 6, P::dlyMode, { "DIGITAL", "TAPE", "ANALOG", "MOD", "REVERSE", "DUAL" });
    modes[0].setTooltip ("Digital: pristine repeats");
    modes[1].setTooltip ("Tape: wow and flutter, saturation, repeats darken as they fade");
    modes[2].setTooltip ("Analog: warm, dark bucket-brigade style repeats");
    modes[3].setTooltip ("Mod: chorused repeats (MOD / RATE)");
    modes[4].setTooltip ("Reverse: each stretch of playing comes back backwards");
    modes[5].setTooltip ("Dual: left at the set time, right at 1.5x for a rhythmic spread");
}

Rectangle<float> DelayUnit::screen() const { return { 270.0f, 14.0f, 214.0f, 78.0f }; }

void DelayUnit::resized()
{
    layoutPowerButton();
    for (int i = 0; i < 6; ++i)
        modes[static_cast<size_t> (i)].setBounds (270 + (i % 3) * 73, 102 + (i / 3) * 28, 68, 23);
    layoutGrid ({ &time.slider, &division.slider, &feedback.slider, &diffusion.slider, &mix.slider, &sync },
                { &lowCut.slider, &highCut.slider, &modDepth.slider, &modRate.slider, &duck.slider, &ping });
}

void DelayUnit::tick()
{
    updateChoiceButtons (modes.data(), 6, P::dlyMode);
    const bool synced = sync.getToggleState();
    time.slider.setEnabled (! synced);
    division.slider.setEnabled (synced);
    const float ms = proc.getDelayTimeMs();
    String text;
    text << (synced ? "S" : "M") << String (ms, 1) << "|" << roundToInt (paramValue (proc, P::dlyDiv)) << "|"
         << roundToInt (paramValue (proc, P::dlyFeedback) * 100.0f) << "|" << roundToInt (paramValue (proc, P::dlyMode))
         << (isOn() ? 1 : 0) << (ping.getToggleState() ? 1 : 0);
    if (text != shownText) { shownText = text; repaint(); }
}

void DelayUnit::paintPanel (Graphics& g)
{
    const bool synced = sync.getToggleState();
    const float ms = proc.getDelayTimeMs();
    const String mode = vfxa::StudioDelay::modeName (roundToInt (paramValue (proc, P::dlyMode)));
    const String fb = "FB " + String (roundToInt (paramValue (proc, P::dlyFeedback) * 100.0f)) + "%";
    if (synced)
        drawDisplay (g, screen(), P::delayDivisionNames()[roundToInt (paramValue (proc, P::dlyDiv))], "",
                     mode + "  SYNC " + String (roundToInt (ms)) + " ms", isOn());
    else
        drawDisplay (g, screen(), String (ms, ms < 100.0f ? 1 : 0), "ms", mode + "  " + (ping.getToggleState() ? String ("P-P ") : String()) + fb, isOn());
    drawCaptions (g, { &time, &division, &feedback, &diffusion, &mix, &lowCut, &highCut, &modDepth, &modRate, &duck });
}

//==============================================================================
ReverbUnit::ReverbUnit (VFXAampsProcessor& p) : RackUnit (p, "VX-R16", "STUDIO REVERB", Colour (0xff15214a), Colour (0xff7d8dff))
{
    powerAtt = std::make_unique<ButtonAttachment> (proc.apvts, P::rvbOn, power);
    power.setTooltip ("Reverb on/off (the tail rings out when switched off)");
    setupKnob (decay, P::rvbDecay, "DECAY");
    setupKnob (preDelay, P::rvbPre, "PRE-DELAY");
    setupKnob (size, P::rvbSize, "SIZE");
    setupKnob (diffusion, P::rvbDiff, "DIFFUSION");
    setupKnob (mod, P::rvbMod, "MOD");
    setupKnob (mix, P::rvbMix, "MIX");
    setupKnob (lowRT, P::rvbLowRT, "LOW DECAY");
    setupKnob (highRT, P::rvbHighRT, "HIGH DECAY");
    setupKnob (early, P::rvbEarly, "EARLY");
    setupKnob (lowCut, P::rvbLowCut, "LOW CUT");
    setupKnob (highCut, P::rvbTone, "HIGH CUT");
    setupKnob (width, P::rvbWidth, "WIDTH");
    lowRT.slider.setTooltip ("Low decay: how long the lows ring, as a multiple of DECAY");
    highRT.slider.setTooltip ("High decay: how long the highs ring, as a multiple of DECAY (lower = darker tail)");
    early.slider.setTooltip ("Early reflections level (the room's first bounces)");
    setupChoiceButtons (programs.data(), 6, P::rvbProgram, { "AMBIENCE", "ROOM", "CHAMBER", "HALL", "PLATE", "CATHEDRAL" });
}

Rectangle<float> ReverbUnit::screen() const { return { 270.0f, 14.0f, 214.0f, 78.0f }; }

void ReverbUnit::resized()
{
    layoutPowerButton();
    for (int i = 0; i < 6; ++i)
        programs[static_cast<size_t> (i)].setBounds (270 + (i % 3) * 73, 102 + (i / 3) * 28, 68, 23);
    layoutGrid ({ &decay.slider, &preDelay.slider, &size.slider, &diffusion.slider, &mod.slider, &mix.slider },
                { &lowRT.slider, &highRT.slider, &early.slider, &lowCut.slider, &highCut.slider, &width.slider });
}

void ReverbUnit::tick()
{
    updateChoiceButtons (programs.data(), 6, P::rvbProgram);
    String text;
    text << String (paramValue (proc, P::rvbDecay), 1) << "|" << roundToInt (paramValue (proc, P::rvbProgram)) << "|"
         << roundToInt (paramValue (proc, P::rvbPre)) << "|" << roundToInt (paramValue (proc, P::rvbSize) * 100.0f) << "|" << (isOn() ? 1 : 0);
    if (text != shownText) { shownText = text; repaint (screen().toNearestInt().expanded (2)); }
}

void ReverbUnit::paintPanel (Graphics& g)
{
    const float dec = paramValue (proc, P::rvbDecay);
    drawDisplay (g, screen(), String (dec, dec < 10.0f ? 1 : 0), "s",
                 String (vfxa::StudioReverb::programName (roundToInt (paramValue (proc, P::rvbProgram))))
                     + "  PRE " + String (roundToInt (paramValue (proc, P::rvbPre))) + " ms", isOn());
    drawCaptions (g, { &decay, &preDelay, &size, &diffusion, &mod, &mix, &lowRT, &highRT, &early, &lowCut, &highCut, &width });
}

//==============================================================================
Rack::Rack (VFXAampsProcessor& p) : eq (p), delay (p), reverb (p)
{
    addAndMakeVisible (eq);
    addAndMakeVisible (delay);
    addAndMakeVisible (reverb);
    setSize (kWidth, kHeight);
}

void Rack::resized()
{
    eq.setBounds (14, 10, kWidth - 28, 150);
    delay.setBounds (14, 164, kWidth - 28, 164);
    reverb.setBounds (14, 332, kWidth - 28, 164);
}

void Rack::tick()
{
    eq.tick();
    delay.tick();
    reverb.tick();
}

void Rack::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // case interior
    g.setGradientFill (ColourGradient (Colour (0xff0b0b0d), r.getTopLeft(), Colour (0xff141418), r.getBottomLeft(), false));
    g.fillRect (r);
    // rails with square holes (behind the ears)
    for (float x : { 6.0f, r.getRight() - 30.0f })
    {
        auto rail = Rectangle<float> (x, 0.0f, 24.0f, r.getHeight());
        g.setGradientFill (ColourGradient (Colour (0xff8d9097), rail.getTopLeft(), Colour (0xff4a4c52), rail.getTopRight(), false));
        g.fillRect (rail);
        g.setColour (Colour (0xff0d0d10));
        for (float y = 6.0f; y < r.getHeight() - 6.0f; y += 14.6f)
            g.fillRect (Rectangle<float> (6.0f, 6.0f).withCentre ({ rail.getCentreX(), y }));
    }
    // shadows the units cast on the case
    g.setColour (Colour (0x80000000));
    for (auto* u : { static_cast<Component*> (&eq), static_cast<Component*> (&delay), static_cast<Component*> (&reverb) })
        g.fillRect (u->getBounds().toFloat().translated (0.0f, 3.0f).reduced (6.0f, 0.0f));
}

} // namespace vfxa::ui
