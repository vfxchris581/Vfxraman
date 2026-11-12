// VFXAamps — amp-head look and feel.

#include "AmpLookAndFeel.h"

#include <cmath>
#include <random>

namespace vfxa::ui
{
using namespace juce;

AmpLookAndFeel::AmpLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId, Colour (0xff1d1f24));
    setColour (PopupMenu::textColourId, Colour (0xffe8e2d4));
    setColour (PopupMenu::highlightedBackgroundColourId, colours::amber.withAlpha (0.85f));
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
    setColour (ComboBox::textColourId, colours::lcdText);
    setColour (Label::textColourId, colours::lcdText);
    setColour (TooltipWindow::backgroundColourId, Colour (0xf0202226));
    setColour (TooltipWindow::textColourId, Colour (0xffe8e2d4));
    setColour (Slider::textBoxTextColourId, colours::lcdText);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colour (0xe0101114));
}

Font AmpLookAndFeel::labelFont (float height, bool bold)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain)).withExtraKerningFactor (0.08f);
}

void AmpLookAndFeel::drawEngraved (Graphics& g, const String& text, Rectangle<float> r, float height, Justification j, bool bold)
{
    g.setFont (labelFont (height, bold));
    g.setColour (colours::engraveHi);
    g.drawText (text, r.translated (0.0f, 1.0f), j, false);
    g.setColour (colours::engraving);
    g.drawText (text, r, j, false);
}

void AmpLookAndFeel::drawScrew (Graphics& g, Point<float> c, float rad, float angle)
{
    auto r = Rectangle<float> (rad * 2, rad * 2).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xffeeeeee), r.getTopLeft(), Colour (0xff6d6f73), r.getBottomRight(), false));
    g.fillEllipse (r);
    g.setColour (Colour (0x80000000));
    g.drawEllipse (r, 0.8f);
    Path slot;
    slot.addRectangle (-rad * 0.8f, -rad * 0.13f, rad * 1.6f, rad * 0.26f);
    g.setColour (Colour (0xff3a3b3e));
    g.fillPath (slot, AffineTransform::rotation (angle).translated (c));
}

//==============================================================================
void AmpLookAndFeel::drawChickenHead (Graphics& g, Rectangle<float> r, float angle, bool hover)
{
    const auto c = r.getCentre();
    const float rad = jmin (r.getWidth(), r.getHeight()) * 0.5f;

    // drop shadow on the faceplate
    g.setColour (Colour (0x55000000));
    g.fillEllipse (Rectangle<float> (rad * 1.30f, rad * 1.30f).withCentre (c.translated (rad * 0.05f, rad * 0.10f)));

    // round bakelite skirt
    auto skirt = Rectangle<float> (rad * 1.22f, rad * 1.22f).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xff3b3d42), skirt.getTopLeft(), Colour (0xff050506), skirt.getBottomRight(), false));
    g.fillEllipse (skirt);
    g.setColour (Colour (0x30ffffff));
    g.drawEllipse (skirt.reduced (0.6f), 1.0f);

    // chicken-head pointer: long tapered nose, rounded tail
    Path p;
    const float nose = rad * 0.98f, tail = rad * 0.46f, wide = rad * 0.34f;
    p.startNewSubPath (0.0f, -nose);
    p.cubicTo (wide * 0.55f, -nose * 0.55f, wide, -rad * 0.05f, wide * 0.92f, tail * 0.55f);
    p.cubicTo (wide * 0.8f, tail * 1.15f, -wide * 0.8f, tail * 1.15f, -wide * 0.92f, tail * 0.55f);
    p.cubicTo (-wide, -rad * 0.05f, -wide * 0.55f, -nose * 0.55f, 0.0f, -nose);
    p.closeSubPath();
    const auto t = AffineTransform::rotation (angle).translated (c);

    g.setColour (Colour (0x66000000));
    g.fillPath (p, t.translated (rad * 0.05f, rad * 0.10f));
    g.setGradientFill (ColourGradient (hover ? Colour (0xff4a4c52) : Colour (0xff35373c), c.translated (-rad * 0.4f, -rad * 0.6f),
                                       Colour (0xff060607), c.translated (rad * 0.5f, rad * 0.7f), false));
    g.fillPath (p, t);
    // glossy highlight down the spine
    Path spine;
    spine.addRoundedRectangle (-wide * 0.22f, -nose * 0.80f, wide * 0.44f, nose * 0.95f, wide * 0.2f);
    g.setColour (Colour (0x1cffffff));
    g.fillPath (spine, t);
    // white indicator line
    Path line;
    line.addRoundedRectangle (-rad * 0.035f, -nose * 0.93f, rad * 0.07f, nose * 0.62f, rad * 0.03f);
    g.setColour (Colour (0xfff3eee2));
    g.fillPath (line, t);
    // centre cap
    auto cap = Rectangle<float> (rad * 0.34f, rad * 0.34f).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xff45474d), cap.getTopLeft(), Colour (0xff0b0b0c), cap.getBottomRight(), false));
    g.fillEllipse (cap);
}

void AmpLookAndFeel::drawKnurled (Graphics& g, Rectangle<float> r, float angle, bool hover, bool big)
{
    const auto c = r.getCentre();
    const float rad = jmin (r.getWidth(), r.getHeight()) * 0.5f * 0.93f; // room for the drop shadow

    g.setColour (Colour (0x60000000));
    g.fillEllipse (Rectangle<float> (rad * 2.04f, rad * 2.04f).withCentre (c.translated (rad * 0.03f, rad * 0.05f)));

    // knurled outer ring: fine alternating ridges, lit from the top-left
    auto outer = Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xffd8dadd), outer.getTopLeft(), Colour (0xff4a4c50), outer.getBottomRight(), false));
    g.fillEllipse (outer);
    const int ridges = big ? 120 : 48;
    for (int i = 0; i < ridges; ++i)
    {
        const float a = angle + MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (ridges);
        const float light = 0.5f + 0.5f * std::cos (a - MathConstants<float>::pi * 1.75f); // light from top-left
        g.setColour ((i % 2 == 0 ? Colour (0xff1c1d20) : Colours::white).withAlpha (i % 2 == 0 ? 0.55f - 0.3f * light : 0.25f + 0.35f * light));
        const float r0 = rad * 0.80f, r1 = rad * 0.995f;
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), big ? 1.6f : 1.2f);
    }
    g.setColour (Colour (0x90000000));
    g.drawEllipse (outer.reduced (0.5f), 1.0f);

    // brushed cap with conical sheen
    auto cap = Rectangle<float> (rad * 1.58f, rad * 1.58f).withCentre (c);
    g.setGradientFill (ColourGradient (hover ? Colour (0xfff4f5f6) : Colour (0xffe6e7e9), cap.getTopLeft(), Colour (0xff8e9196), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    {
        Graphics::ScopedSaveState s (g);
        Path clip;
        clip.addEllipse (cap);
        g.reduceClipRegion (clip);
        const int spokes = big ? 180 : 72;
        for (int i = 0; i < spokes; ++i)
        {
            const float a = MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (spokes);
            const float sheen = std::pow (std::abs (std::cos (2.0f * (a - 0.6f))), 6.0f);
            g.setColour (Colours::white.withAlpha (0.10f * sheen + 0.02f));
            g.drawLine (c.x, c.y, c.x + rad * std::sin (a), c.y - rad * std::cos (a), 1.0f);
        }
        for (float k = 0.15f; k < 0.8f; k += big ? 0.035f : 0.08f)
        {
            g.setColour (Colours::black.withAlpha (0.035f));
            g.drawEllipse (Rectangle<float> (rad * 2 * k, rad * 2 * k).withCentre (c), 0.7f);
        }
    }
    g.setColour (Colour (0x70000000));
    g.drawEllipse (cap, 1.0f);

    // pointer: engraved notch filled with cream paint
    Path notch;
    notch.addRoundedRectangle (-rad * 0.035f, -rad * 0.78f, rad * 0.07f, rad * 0.36f, rad * 0.03f);
    g.setColour (Colour (0xff1a1a1c));
    g.fillPath (notch, AffineTransform::rotation (angle).translated (c));
    Path dot;
    dot.addEllipse (-rad * 0.05f, -rad * 0.93f, rad * 0.10f, rad * 0.10f);
    g.setColour (Colour (0xfff7f1e1));
    g.fillPath (dot, AffineTransform::rotation (angle).translated (c));
}

void AmpLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto style = static_cast<KnobStyle> (static_cast<int> (s.getProperties().getWithDefault ("style", 0)));
    const float angle = start + pos * (end - start);
    auto r = Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w), static_cast<float> (h));
    r = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight()));
    const bool hover = s.isMouseOverOrDragging();

    if (style == KnobStyle::Rack)
    {
        const auto accent = Colour (static_cast<uint32> (static_cast<int64> (s.getProperties().getWithDefault ("accent", (int64) 0xff23ded9))));
        drawRackKnob (g, r, start, end, angle, s.getProperties().getWithDefault ("bipolar", false), accent, hover, s.isEnabled());
        return;
    }
    if (style == KnobStyle::ChickenHead)
    {
        // tick dots around the knob, numbered 0..10 at the ends and middle
        const auto c = r.getCentre();
        const float rad = r.getWidth() * 0.5f;
        for (int i = 0; i <= 10; ++i)
        {
            const float a = start + (end - start) * static_cast<float> (i) / 10.0f;
            const float tr = rad * 1.02f;
            const auto p = c.getPointOnCircumference (tr, a);
            g.setColour (colours::engraveHi);
            g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (p.translated (0.0f, 0.8f)));
            g.setColour (colours::engraving);
            g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (p));
        }
        drawChickenHead (g, r.reduced (rad * 0.12f), angle, hover);
    }
    else
    {
        const bool big = style == KnobStyle::Knurled;
        if (! big)
        {
            // small knobs: a thin amber arc shows the setting
            const auto c = r.getCentre();
            const float rad = r.getWidth() * 0.5f;
            Path track, value;
            track.addCentredArc (c.x, c.y, rad * 0.98f, rad * 0.98f, 0.0f, start, end, true);
            g.setColour (Colour (0x40000000));
            g.strokePath (track, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
            const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
            const float from = bipolar ? (start + end) * 0.5f : start;
            value.addCentredArc (c.x, c.y, rad * 0.98f, rad * 0.98f, 0.0f, from, angle, true);
            g.setColour (colours::amber.withAlpha (0.9f));
            g.strokePath (value, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
            drawKnurled (g, r.reduced (rad * 0.16f), angle, hover, false);
        }
        else
        {
            drawKnurled (g, r, angle, hover, true);
        }
    }
}

//==============================================================================
void AmpLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    // Vintage bat-handle toggle switch (style "switch") or a jewel push button (default).
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    if (b.getProperties().getWithDefault ("switch", false))
    {
        auto plate = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight() * 0.62f), jmin (r.getWidth(), r.getHeight() * 0.62f));
        g.setGradientFill (ColourGradient (Colour (0xffe9eaec), plate.getTopLeft(), Colour (0xff7a7d82), plate.getBottomRight(), false));
        g.fillEllipse (plate);
        g.setColour (Colour (0x90000000));
        g.drawEllipse (plate, 1.0f);
        const auto c = plate.getCentre();
        const float len = r.getHeight() * 0.48f * (on ? -1.0f : 1.0f);
        Path bat;
        bat.startNewSubPath (c.x - 2.2f, c.y);
        bat.lineTo (c.x - 3.6f, c.y + len);
        bat.lineTo (c.x + 3.6f, c.y + len);
        bat.lineTo (c.x + 2.2f, c.y);
        bat.closeSubPath();
        g.setColour (Colour (0x50000000));
        g.fillPath (bat, AffineTransform::translation (1.5f, 2.0f));
        g.setGradientFill (ColourGradient (Colour (0xfff8f8f8), c.translated (-4, 0), Colour (0xff8d9095), c.translated (4, 0), false));
        g.fillPath (bat);
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ c.x, c.y + len }));
        if (highlighted) { g.setColour (colours::amber.withAlpha (0.25f)); g.drawEllipse (plate.expanded (2.0f), 1.5f); }
        return;
    }

    auto lamp = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (2.0f);
    const auto col = b.findColour (ToggleButton::tickColourId) == Colour() ? colours::amber : b.findColour (ToggleButton::tickColourId);
    g.setColour (Colour (0xff0b0c0e));
    g.fillEllipse (lamp.expanded (1.5f));
    if (on)
    {
        g.setGradientFill (ColourGradient (col.brighter (0.6f), lamp.getCentre().translated (0, -lamp.getHeight() * 0.2f), col.darker (0.6f), lamp.getBottomRight(), true));
        g.fillEllipse (lamp);
        g.setColour (col.withAlpha (0.25f));
        g.fillEllipse (lamp.expanded (4.0f));
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xff3a3027), lamp.getTopLeft(), Colour (0xff120f0c), lamp.getBottomRight(), false));
        g.fillEllipse (lamp);
    }
    g.setColour (Colours::white.withAlpha (highlighted ? 0.45f : 0.28f));
    g.fillEllipse (lamp.reduced (lamp.getWidth() * 0.3f).translated (-lamp.getWidth() * 0.12f, -lamp.getHeight() * 0.16f).withHeight (lamp.getHeight() * 0.18f));
}

void AmpLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    if (b.getProperties().getWithDefault ("rack", false)) { drawRackButton (g, b, highlighted, down); return; }
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = jmin (5.0f, r.getHeight() * 0.3f);
    g.setGradientFill (ColourGradient (down ? Colour (0xff17181b) : Colour (0xff34363c), r.getTopLeft(),
                                       down ? Colour (0xff2a2c30) : Colour (0xff141518), r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (highlighted ? colours::amber.withAlpha (0.7f) : Colour (0x40ffffff));
    g.drawRoundedRectangle (r, corner, 1.0f);
}

void AmpLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool highlighted, bool)
{
    if (b.getProperties().getWithDefault ("rack", false))
    {
        // label sits right of the LED
        auto r = b.getLocalBounds().toFloat();
        r.removeFromLeft (jmin (22.0f, r.getHeight() * 0.9f));
        g.setFont (labelFont (jmin (11.0f, b.getHeight() * 0.48f)));
        g.setColour (b.isEnabled() ? Colour (highlighted ? 0xffffffff : 0xffd8dce4) : Colour (0x60d8dce4));
        g.drawText (b.getButtonText(), r.withTrimmedRight (4.0f), Justification::centred, false);
        return;
    }
    g.setFont (labelFont (jmin (13.0f, b.getHeight() * 0.55f)));
    g.setColour (highlighted ? colours::amberHot : Colour (0xffe8e2d4));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
}

void AmpLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    g.setColour (Colour (0xff0d0e10));
    g.fillRoundedRectangle (r, 4.0f);
    g.setGradientFill (ColourGradient (Colour (0x30ffa63a), r.getTopLeft(), Colour (0x00000000), r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.isMouseOver() ? colours::amber.withAlpha (0.6f) : Colour (0x50ffffff));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    Path arrow;
    const float ax = static_cast<float> (w) - 14.0f, ay = static_cast<float> (h) * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (colours::lcdText);
    g.fillPath (arrow);
}

Font AmpLookAndFeel::getComboBoxFont (ComboBox& b) { return labelFont (jmin (14.0f, b.getHeight() * 0.55f), false); }
void AmpLookAndFeel::positionComboBoxText (ComboBox& b, Label& l) { l.setBounds (6, 1, b.getWidth() - 24, b.getHeight() - 2); l.setFont (getComboBoxFont (b)); }
void AmpLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (findColour (PopupMenu::backgroundColourId));
    g.setColour (Colour (0x40ffffff));
    g.drawRect (0, 0, w, h);
}
Font AmpLookAndFeel::getPopupMenuFont() { return labelFont (15.0f, false); }

Label* AmpLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (labelFont (12.0f, false));
    l->setJustificationType (Justification::centred);
    return l;
}

//==============================================================================
Image makeTolex (int w, int h, float scale)
{
    const int pw = jmax (1, roundToInt (static_cast<float> (w) * scale)), ph = jmax (1, roundToInt (static_cast<float> (h) * scale));
    Image img (Image::RGB, pw, ph, false);
    std::mt19937 rng (1234);
    std::uniform_int_distribution<int> d (-9, 9);
    // grain: per-pixel noise smoothed with its neighbours gives the pebbled tolex feel
    std::vector<int> noise (static_cast<size_t> (pw * ph));
    for (auto& n : noise) n = d (rng);
    Image::BitmapData bd (img, Image::BitmapData::writeOnly);
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
        {
            auto at = [&] (int xx, int yy) { return noise[static_cast<size_t> (jlimit (0, ph - 1, yy) * pw + jlimit (0, pw - 1, xx))]; };
            const int bump = (2 * at (x, y) + at (x - 1, y) + at (x, y - 1) - at (x + 1, y + 1)) / 3;
            const float vignette = 1.0f - 0.25f * std::pow (std::abs (2.0f * static_cast<float> (y) / static_cast<float> (ph) - 1.0f), 3.0f);
            const int v = jlimit (0, 255, roundToInt ((24.0f + static_cast<float> (bump)) * vignette));
            bd.setPixelColour (x, y, Colour::fromRGB (static_cast<uint8> (v), static_cast<uint8> (v + 1), static_cast<uint8> (v + 3)));
        }
    return img;
}

Image makeBrushedMetal (int w, int h, float scale)
{
    const int pw = jmax (1, roundToInt (static_cast<float> (w) * scale)), ph = jmax (1, roundToInt (static_cast<float> (h) * scale));
    Image img (Image::RGB, pw, ph, false);
    std::mt19937 rng (99);
    std::normal_distribution<float> rowNoise (0.0f, 3.2f), fine (0.0f, 1.6f);
    std::vector<float> row (static_cast<size_t> (ph));
    float drift = 0.0f;
    for (auto& r : row) { drift = 0.85f * drift + rowNoise (rng); r = drift; }
    Image::BitmapData bd (img, Image::BitmapData::writeOnly);
    for (int y = 0; y < ph; ++y)
    {
        const float t = static_cast<float> (y) / static_cast<float> (ph);
        const float base = 196.0f - 34.0f * t + 10.0f * std::sin (t * 3.0f);
        float streak = 0.0f;
        for (int x = 0; x < pw; ++x)
        {
            streak = 0.97f * streak + 0.03f * fine (rng) * 6.0f;
            const float sheen = 14.0f * std::exp (-std::pow ((static_cast<float> (x) / static_cast<float> (pw) - 0.32f - 0.2f * t) * 3.2f, 2.0f));
            const int v = jlimit (0, 255, roundToInt (base + row[static_cast<size_t> (y)] + streak + sheen));
            bd.setPixelColour (x, y, Colour::fromRGB (static_cast<uint8> (v), static_cast<uint8> (v), static_cast<uint8> (jmin (255, v + 4))));
        }
    }
    return img;
}


//==============================================================================
// Rack units
void AmpLookAndFeel::drawRackKnob (Graphics& g, Rectangle<float> r, float start, float end, float angle, bool bipolar,
                                   Colour accent, bool hover, bool enabled)
{
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;
    if (! enabled) accent = accent.withSaturation (0.0f).withAlpha (0.35f);

    // value ring (LED-style): dim track, lit value
    Path track, value;
    track.addCentredArc (c.x, c.y, rad * 0.94f, rad * 0.94f, 0.0f, start, end, true);
    g.setColour (Colour (0xff050608));
    g.strokePath (track, PathStrokeType (rad * 0.13f, PathStrokeType::curved, PathStrokeType::butt));
    const float from = bipolar ? (start + end) * 0.5f : start;
    value.addCentredArc (c.x, c.y, rad * 0.94f, rad * 0.94f, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (value, PathStrokeType (rad * 0.24f, PathStrokeType::curved, PathStrokeType::butt));
    g.setColour (accent);
    g.strokePath (value, PathStrokeType (rad * 0.09f, PathStrokeType::curved, PathStrokeType::butt));

    // body: black anodised with a knurled skirt
    auto body = r.reduced (rad * 0.2f);
    const float br = body.getWidth() * 0.5f;
    g.setColour (Colour (0x90000000));
    g.fillEllipse (body.translated (0.0f, br * 0.10f).expanded (br * 0.04f));
    g.setGradientFill (ColourGradient (Colour (0xff3a3d44), body.getTopLeft(), Colour (0xff0b0c0e), body.getBottomRight(), false));
    g.fillEllipse (body);
    g.setColour (Colour (0xff000000));
    for (int i = 0; i < 36; ++i)
    {
        const float a = angle + MathConstants<float>::twoPi * static_cast<float> (i) / 36.0f;
        g.drawLine (Line<float> (c.getPointOnCircumference (br * 0.80f, a), c.getPointOnCircumference (br * 0.98f, a)), 0.9f);
    }
    // silver cap
    auto cap = body.reduced (br * 0.24f);
    g.setGradientFill (ColourGradient (Colour (0xfff2f3f5), cap.getTopLeft(), Colour (0xff70747c), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    g.setGradientFill (ColourGradient (Colour (0x00ffffff), cap.getCentre(), Colour (0x30000000), cap.getTopLeft(), true));
    g.fillEllipse (cap);
    g.setColour (Colour (0xff2a2c30));
    g.drawEllipse (cap, 0.8f);
    // pointer
    g.setColour (Colour (0xff111214));
    g.drawLine (Line<float> (c.getPointOnCircumference (br * 0.15f, angle), c.getPointOnCircumference (br * 0.70f, angle)), jmax (1.6f, br * 0.10f));
    if (hover && enabled)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.drawEllipse (body.expanded (1.0f), 1.2f);
    }
}

void AmpLookAndFeel::drawRackButton (Graphics& g, Button& b, bool highlighted, bool down)
{
    const auto accent = Colour (static_cast<uint32> (static_cast<int64> (b.getProperties().getWithDefault ("accent", (int64) 0xff23ded9))));
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 3.0f;
    // recess
    g.setColour (Colour (0xff050506));
    g.fillRoundedRectangle (r, corner);
    // rubber cap
    auto cap = r.reduced (1.5f).translated (0.0f, down ? 0.8f : 0.0f);
    g.setGradientFill (ColourGradient (Colour (down ? 0xff23252a : 0xff3b3e45), cap.getTopLeft(), Colour (0xff1a1b1f), cap.getBottomLeft(), false));
    g.fillRoundedRectangle (cap, corner);
    g.setColour (Colour (highlighted ? 0x50ffffff : 0x22ffffff));
    g.drawRoundedRectangle (cap, corner, 0.8f);
    // LED
    const float d = jmin (8.0f, cap.getHeight() * 0.36f);
    const auto led = Rectangle<float> (d, d).withCentre ({ cap.getX() + jmin (11.0f, cap.getHeight() * 0.45f), cap.getCentreY() });
    const bool on = b.getToggleState();
    if (on)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.fillEllipse (led.expanded (d * 0.6f));
    }
    g.setGradientFill (ColourGradient (on ? accent.brighter (0.6f) : Colour (0xff2b2d31), led.getTopLeft(),
                                       on ? accent : Colour (0xff121315), led.getBottomRight(), false));
    g.fillEllipse (led);
    g.setColour (Colour (0x80000000));
    g.drawEllipse (led, 0.7f);
}

void AmpLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                       Slider::SliderStyle style, Slider& s)
{
    if (! s.getProperties().getWithDefault ("fader", false) || style != Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    const auto accent = Colour (static_cast<uint32> (static_cast<int64> (s.getProperties().getWithDefault ("accent", (int64) 0xffffa63a))));
    const auto area = Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w), static_cast<float> (h));
    const float cx = area.getCentreX();
    // slot
    auto slot = Rectangle<float> (4.0f, area.getHeight() + 8.0f).withCentre ({ cx, area.getCentreY() });
    g.setColour (Colour (0xff050506));
    g.fillRoundedRectangle (slot, 2.0f);
    g.setColour (Colour (0x30ffffff));
    g.drawLine (slot.getRight() + 0.5f, slot.getY() + 2.0f, slot.getRight() + 0.5f, slot.getBottom() - 2.0f, 0.6f);
    // lit segment from 0 dB to the cap
    const float zeroY = area.getCentreY();
    const float top = jmin (zeroY, pos), bottom = jmax (zeroY, pos);
    if (bottom - top > 0.5f)
    {
        g.setColour (accent.withAlpha (s.isEnabled() ? 0.85f : 0.25f));
        g.fillRect (Rectangle<float> (2.0f, bottom - top).withCentre ({ cx, (top + bottom) * 0.5f }));
    }
    // fader cap
    const float capW = jmin (static_cast<float> (w) * 0.82f, 30.0f), capH = 16.0f;
    auto cap = Rectangle<float> (capW, capH).withCentre ({ cx, pos });
    g.setColour (Colour (0x90000000));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 2.5f);
    g.setGradientFill (ColourGradient (Colour (0xff4a4d55), cap.getTopLeft(), Colour (0xff15161a), cap.getBottomLeft(), false));
    g.fillRoundedRectangle (cap, 2.5f);
    for (int i = -1; i <= 1; ++i)
    {
        g.setColour (Colour (0x55000000));
        g.drawLine (cap.getX() + 3.0f, cap.getCentreY() + i * 4.0f + 0.6f, cap.getRight() - 3.0f, cap.getCentreY() + i * 4.0f + 0.6f, 0.8f);
        g.setColour (Colour (0x30ffffff));
        g.drawLine (cap.getX() + 3.0f, cap.getCentreY() + i * 4.0f, cap.getRight() - 3.0f, cap.getCentreY() + i * 4.0f, 0.8f);
    }
    g.setColour (Colour (0xfff2f3f5));
    g.drawLine (cap.getX() + 2.0f, cap.getCentreY(), cap.getRight() - 2.0f, cap.getCentreY(), 1.4f);
    if (s.isMouseOverOrDragging())
    {
        g.setColour (accent.withAlpha (0.5f));
        g.drawRoundedRectangle (cap.expanded (1.0f), 3.0f, 1.0f);
    }
}

} // namespace vfxa::ui
