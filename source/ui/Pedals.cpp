// VFXAamps — pedalboard implementation.

#include "Pedals.h"

namespace vfxa::ui
{
using namespace juce;

void Footswitch::paintButton (Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto c = r.getCentre();
    const float rad = jmin (r.getWidth(), r.getHeight()) * 0.5f;
    // hex nut
    Path nut;
    for (int i = 0; i < 6; ++i)
    {
        const auto p = c.getPointOnCircumference (rad, MathConstants<float>::pi / 6.0f + i * MathConstants<float>::pi / 3.0f);
        if (i == 0) nut.startNewSubPath (p); else nut.lineTo (p);
    }
    nut.closeSubPath();
    g.setColour (Colour (0x90000000));
    g.fillPath (nut, AffineTransform::translation (0.0f, 1.5f));
    g.setGradientFill (ColourGradient (Colour (0xffe6e8ec), r.getTopLeft(), Colour (0xff5d6168), r.getBottomRight(), false));
    g.fillPath (nut);
    g.setColour (Colour (0x80000000));
    g.strokePath (nut, PathStrokeType (0.8f));
    // dome
    auto dome = Rectangle<float> (rad * 1.25f, rad * 1.25f).withCentre (c.translated (0.0f, down ? 0.8f : 0.0f));
    g.setGradientFill (ColourGradient (Colour (0xffffffff), dome.getTopLeft().translated (dome.getWidth() * 0.25f, dome.getHeight() * 0.2f),
                                       Colour (0xff6a6e75), dome.getBottomRight(), true));
    g.fillEllipse (dome);
    g.setColour (Colour (0xff3a3c40));
    g.drawEllipse (dome, 0.9f);
    g.setColour (Colour (0x90ffffff));
    g.fillEllipse (Rectangle<float> (dome.getWidth() * 0.28f, dome.getHeight() * 0.18f)
                       .withCentre (dome.getCentre().translated (-dome.getWidth() * 0.14f, -dome.getHeight() * 0.22f)));
    if (highlighted)
    {
        g.setColour (Colour (0x30ffffff));
        g.fillEllipse (dome);
    }
}

//==============================================================================
PedalUnit::PedalUnit (VFXAampsProcessor& p, int which, Colour paint)
    : proc (p), pedal (which), asset (VFXAampsProcessor::pedalAsset (which)), paintColour (paint)
{
    namespace P = vfxa::params;
    level.setup (*this, proc.apvts, P::pedalLevel (pedal), "LEVEL", KnobStyle::Small, true);
    addAndMakeVisible (footswitch);
    footAtt = std::make_unique<ButtonAttachment> (proc.apvts, P::pedalOn (pedal), footswitch);
    footswitch.setTooltip ("Pedal " + String (pedal + 1) + " on / off");
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    loadButton.setTooltip ("Load a NAM pedal capture (.nam) into pedal " + String (pedal + 1) + " (or drop one here)");
    clearButton.setTooltip ("Remove the capture from pedal " + String (pedal + 1));
    loadButton.onClick = [this] { chooseAndLoad (proc, asset, chooser); };
    clearButton.onClick = [this] { proc.clearAsset (asset); };
    setTooltip ("Pedal " + String (pedal + 1) + ": a NAM capture in front of the amp. Drop a .nam file here.");
    refresh();
}

void PedalUnit::refresh()
{
    status = proc.getAssetStatus (asset);
    clearButton.setEnabled (status.loaded || status.missing || status.path.isNotEmpty());
    repaint();
}

void PedalUnit::tick()
{
    const float l = status.loaded ? proc.getPedalLevel (pedal) : 0.0f;
    if (std::abs (l - led) > 0.01f) { led = l; repaint (Rectangle<int> (40, 26).withCentre ({ getWidth() / 2, 15 })); }
}

void PedalUnit::resized()
{
    loadButton.setBounds (14, 82, 62, 22);
    clearButton.setBounds (80, 82, 26, 22);
    level.slider.setBounds (Rectangle<int> (38, 38).withCentre ({ getWidth() - 40, 88 }));
    footswitch.setBounds (Rectangle<int> (40, 40).withCentre ({ getWidth() / 2, getHeight() - 30 }));
}

void PedalUnit::mouseUp (const MouseEvent& e)
{
    // clicking the name display loads (or relinks a missing capture)
    if (e.mouseWasClicked() && Rectangle<int> (14, 40, getWidth() - 28, 34).contains (e.getPosition()))
        chooseAndLoad (proc, asset, chooser);
}

bool PedalUnit::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (isModelFile (f)) return true;
    return false;
}

void PedalUnit::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (isModelFile (f)) { proc.loadAsset (asset, File (f), true); break; }
    repaint();
}

void PedalUnit::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    // shadow on the board
    g.setColour (Colour (0x90000000));
    g.fillRoundedRectangle (r.translated (2.0f, 5.0f), 12.0f);
    // die-cast enclosure, painted
    g.setGradientFill (ColourGradient (paintColour.brighter (0.35f), r.getTopLeft(), paintColour.darker (0.55f), r.getBottomRight(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setGradientFill (ColourGradient (Colour (0x38ffffff), r.getTopLeft(), Colour (0x00ffffff), r.getTopLeft().translated (0.0f, r.getHeight() * 0.45f), false));
    g.fillRoundedRectangle (r.reduced (2.0f), 10.0f);
    g.setColour (paintColour.darker (0.9f));
    g.drawRoundedRectangle (r, 12.0f, 1.4f);
    g.setColour (Colour (0x40ffffff));
    g.drawRoundedRectangle (r.reduced (1.5f), 10.5f, 0.8f);
    if (dragOver)
    {
        g.setColour (colours::amberHot.withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.5f);
    }

    // jacks on the sides (input left, output right)
    for (float x : { r.getX() - 2.0f, r.getRight() - 6.0f })
    {
        auto j = Rectangle<float> (8.0f, 16.0f).withCentre ({ x + 4.0f, 58.0f });
        g.setGradientFill (ColourGradient (Colour (0xffd0d3d8), j.getTopLeft(), Colour (0xff55585e), j.getBottomRight(), false));
        g.fillRoundedRectangle (j, 2.0f);
    }

    // LED
    const auto ledC = Point<float> (r.getCentreX(), 15.0f);
    const bool on = led > 0.02f;
    if (on)
    {
        g.setColour (Colour (0xffff2a1a).withAlpha (0.35f * led));
        g.fillEllipse (Rectangle<float> (22.0f, 22.0f).withCentre (ledC));
    }
    auto ledR = Rectangle<float> (9.0f, 9.0f).withCentre (ledC);
    g.setGradientFill (ColourGradient (on ? Colour (0xffffb4a8) : Colour (0xff5a1410), ledR.getTopLeft(),
                                       on ? Colour (0xffe8170a) : Colour (0xff250605), ledR.getBottomRight(), false));
    g.fillEllipse (ledR);
    g.setColour (Colour (0xa0000000));
    g.drawEllipse (ledR, 0.8f);

    // title
    g.setFont (AmpLookAndFeel::labelFont (11.0f));
    g.setColour (Colour (0xfff4f1ea));
    g.drawText ("PEDAL " + String (pedal + 1), Rectangle<float> (r.getX() + 12.0f, 6.0f, 70.0f, 18.0f), Justification::centredLeft);
    g.setFont (AmpLookAndFeel::labelFont (9.0f, false));
    g.setColour (Colour (0xccf4f1ea));
    g.drawText ("NAM", Rectangle<float> (r.getRight() - 52.0f, 6.0f, 40.0f, 18.0f), Justification::centredRight);

    // name display
    auto nameArea = Rectangle<float> (14.0f, 40.0f, r.getWidth() - 24.0f, 34.0f);
    g.setColour (Colour (0xff070809));
    g.fillRoundedRectangle (nameArea, 4.0f);
    g.setColour (Colour (0x30ffffff));
    g.drawLine (nameArea.getX() + 3.0f, nameArea.getBottom() + 0.5f, nameArea.getRight() - 3.0f, nameArea.getBottom() + 0.5f, 0.7f);
    String text;
    Colour col = colours::lcdText;
    if (status.loading) { text = String::fromUTF8 ("loading\xe2\x80\xa6"); col = colours::lcdText.withAlpha (0.6f); }
    else if (status.missing) { text = status.name + "\nmissing - click to find"; col = colours::warn; }
    else if (status.error.isNotEmpty() && ! status.loaded) { text = "error: " + status.error; col = colours::warn; }
    else if (status.loaded) text = status.name;
    else { text = "empty - drop a .nam"; col = Colour (0xff6c6458); }
    g.setColour (col);
    g.setFont (AmpLookAndFeel::labelFont (11.5f, false));
    g.drawFittedText (text, nameArea.reduced (5.0f, 2.0f).toNearestInt(), Justification::centred, 2, 0.85f);

    // level caption
    const auto kb = level.slider.getBounds().toFloat();
    g.setFont (AmpLookAndFeel::labelFont (9.0f));
    g.setColour (Colour (0xfff4f1ea));
    g.drawText ("LEVEL", Rectangle<float> (kb.getCentreX() - 30.0f, kb.getBottom(), 60.0f, 12.0f), Justification::centred);
}

//==============================================================================
Pedalboard::Pedalboard (VFXAampsProcessor& p)
    : p1 (p, 0, Colour (0xff168a84)), p2 (p, 1, Colour (0xffc4561a))
{
    addAndMakeVisible (p1);
    addAndMakeVisible (p2);
    setSize (kWidth, kHeight);
}

void Pedalboard::resized()
{
    p1.setBounds (430, 14, 204, 160);
    p2.setBounds (700, 14, 204, 160);
}

void Pedalboard::tick() { p1.tick(); p2.tick(); }
void Pedalboard::refresh() { p1.refresh(); p2.refresh(); }

void Pedalboard::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // board: dark carpet-like surface in an aluminium frame
    g.setColour (Colour (0xff0d0d0f));
    g.fillRect (r);
    auto board = r.reduced (14.0f, 6.0f);
    g.setGradientFill (ColourGradient (Colour (0xff9a9da3), board.getTopLeft(), Colour (0xff4a4d53), board.getBottomLeft(), false));
    g.fillRoundedRectangle (board, 6.0f);
    auto felt = board.reduced (6.0f);
    g.setGradientFill (ColourGradient (Colour (0xff1d1e22), felt.getTopLeft(), Colour (0xff111215), felt.getBottomLeft(), false));
    g.fillRoundedRectangle (felt, 3.0f);
    Random rng (17);
    for (int i = 0; i < 900; ++i)
    {
        g.setColour (Colour (0x0affffff));
        g.fillRect (felt.getX() + rng.nextFloat() * felt.getWidth(), felt.getY() + rng.nextFloat() * felt.getHeight(), 1.0f, 1.0f);
    }

    // labels
    g.setFont (Font (FontOptions (20.0f, Font::bold | Font::italic)).withExtraKerningFactor (0.03f));
    g.setColour (Colour (0xffe9ecf2));
    g.drawText ("PEDALBOARD", Rectangle<float> (48.0f, 40.0f, 300.0f, 26.0f), Justification::centredLeft);
    g.setFont (AmpLookAndFeel::labelFont (10.0f, false));
    g.setColour (Colour (0xff8d95a6));
    g.drawText ("Two NAM captures in front of the amp", Rectangle<float> (48.0f, 66.0f, 320.0f, 16.0f), Justification::centredLeft);
    g.drawText ("Footswitch = on / off.  Off or empty = straight through.", Rectangle<float> (48.0f, 82.0f, 340.0f, 16.0f), Justification::centredLeft);

    // jack plates: guitar in, to the amp
    auto plate = [&] (Point<float> c, const char* label)
    {
        auto pr = Rectangle<float> (58.0f, 44.0f).withCentre (c);
        g.setGradientFill (ColourGradient (Colour (0xffc9ccd2), pr.getTopLeft(), Colour (0xff6c6f76), pr.getBottomRight(), false));
        g.fillRoundedRectangle (pr, 4.0f);
        g.setColour (Colour (0xff111214));
        g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (c.translated (0.0f, 4.0f)));
        g.setFont (AmpLookAndFeel::labelFont (8.0f));
        g.setColour (Colour (0xff1b1c20));
        g.drawText (label, pr.withHeight (14.0f).translated (0.0f, 2.0f), Justification::centred);
    };
    const Point<float> guitar { 360.0f, 130.0f }, amp { 1000.0f, 130.0f };
    plate (guitar, "GUITAR");
    plate (amp, "TO AMP");

    // patch cables (signal flows left to right)
    auto cable = [&] (Point<float> a, Point<float> b)
    {
        Path p;
        p.startNewSubPath (a);
        const float sag = 26.0f + std::abs (b.x - a.x) * 0.08f;
        p.cubicTo (a.translated (24.0f, sag), b.translated (-24.0f, sag), b);
        g.setColour (Colour (0xff050506));
        g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0x30ffffff));
        g.strokePath (p, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (0.0f, -1.5f));
    };
    const float jackY = 14.0f + 58.0f;
    cable (guitar.translated (0.0f, 4.0f), { 430.0f, jackY });
    cable ({ 634.0f, jackY }, { 700.0f, jackY });
    cable ({ 904.0f, jackY }, amp.translated (0.0f, 4.0f));
}

} // namespace vfxa::ui
