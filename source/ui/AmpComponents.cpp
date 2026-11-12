// VFXAamps — slot cards and IR columns.

#include "AmpComponents.h"

namespace vfxa::ui
{
using namespace juce;

void Knob::setup (Component& parent, AudioProcessorValueTreeState& state, const String& paramId,
                  const String& captionText, KnobStyle style, bool bipolar)
{
    caption = captionText;
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr, 1200);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set ("style", static_cast<int> (style));
    slider.getProperties().set ("bipolar", bipolar);
    slider.setMouseDragSensitivity (style == KnobStyle::Knurled ? 420 : 220);
    slider.setVelocityBasedMode (false);
    if (auto* p = state.getParameter (paramId)) slider.setTooltip (p->getName (64));
    parent.addAndMakeVisible (slider);
    attachment = std::make_unique<SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, static_cast<double> (state.getParameter (paramId)->convertFrom0to1 (state.getParameter (paramId)->getDefaultValue())));
}

void drawKnobCaptions (Graphics& g, std::initializer_list<const Knob*> knobs, float height)
{
    for (auto* k : knobs)
    {
        const auto b = k->slider.getBounds().toFloat();
        const auto r = Rectangle<float> (b.getX() - 20.0f, b.getBottom() - 2.0f, b.getWidth() + 40.0f, height + 4.0f);
        g.setFont (AmpLookAndFeel::labelFont (height));
        g.setColour (Colour (0xffcfc8b8));
        g.drawText (k->caption, r, Justification::centredTop, false);
    }
}

bool isModelFile (const String& path) { return path.endsWithIgnoreCase (".nam"); }
bool isIrFile (const String& path)
{
    for (auto* ext : { ".wav", ".aif", ".aiff", ".flac" }) if (path.endsWithIgnoreCase (ext)) return true;
    return false;
}

void chooseAndLoad (VFXAampsProcessor& proc, int asset, std::unique_ptr<FileChooser>& holder)
{
    const bool model = asset < vfxa::kNumSlots;
    const auto current = proc.getAssetStatus (asset).path;
    File start (proc.getSettings().getValue (model ? "lastModelDir" : "lastIrDir"));
    if (current.isNotEmpty() && File (current).getParentDirectory().isDirectory()) start = File (current).getParentDirectory();
    if (! start.isDirectory()) start = File::getSpecialLocation (File::userDocumentsDirectory);
    holder = std::make_unique<FileChooser> (model ? "Load NAM model into slot " + String (asset + 1)
                                                  : String ("Load impulse response ") + (asset == VFXAampsProcessor::irAsset (0) ? "A" : "B"),
                                            start, model ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac");
    holder->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                         [&proc, asset] (const FileChooser& fc)
                         {
                             const auto f = fc.getResult();
                             if (f.existsAsFile()) proc.loadAsset (asset, f, true);
                         });
}

//==============================================================================
SlotCard::SlotCard (VFXAampsProcessor& p, int s) : proc (p), slot (s)
{
    trim.setup (*this, proc.apvts, vfxa::params::slotTrim (slot), "LEVEL", KnobStyle::Small, true);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    loadButton.setTooltip ("Load a .nam model into slot " + String (slot + 1) + " (or drop files here)");
    clearButton.setTooltip ("Clear slot " + String (slot + 1));
    loadButton.onClick = [this] { chooseAndLoad (proc, slot, chooser); };
    clearButton.onClick = [this] { proc.clearAsset (slot); };
    setTooltip ("Slot " + String (slot + 1) + ": drop .nam files here (several fill the following slots)");
    irButton.getProperties().set ("rack", true);
    irButton.getProperties().set ("accent", (int64) colours::amber.getARGB());
    irButton.setClickingTogglesState (true);
    irButton.setTooltip ("Slot " + String (slot + 1) + ": use the cab IR while this slot plays. Turn it off for captures that "
                         "already include a cab (full rig). Set automatically when you load a capture that says which it is.");
    addAndMakeVisible (irButton);
    irAtt = std::make_unique<ButtonAttachment> (proc.apvts, vfxa::params::slotIr (slot), irButton);
    refresh();
}

SlotCard::~SlotCard() = default;

void SlotCard::refresh()
{
    status = proc.getAssetStatus (slot);
    clearButton.setEnabled (status.loaded || status.missing || status.loading);
    repaint();
}

void SlotCard::setLevel (float l)
{
    if (std::abs (l - level) < 0.004f) return;
    level = l;
    repaint();
}

void SlotCard::resized()
{
    auto r = getLocalBounds().reduced (6);
    irButton.setBounds (Rectangle<int> (42, 19).withCentre ({ getWidth() / 2 + 1, 18 }));
    auto bottom = r.removeFromBottom (24);
    loadButton.setBounds (bottom.removeFromLeft (bottom.getWidth() - 30).reduced (0, 1));
    clearButton.setBounds (bottom.removeFromRight (26).reduced (0, 1));
    r.removeFromBottom (14);
    trim.slider.setBounds (r.removeFromBottom (40).withSizeKeepingCentre (40, 40));
}

void SlotCard::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    // recessed dark panel
    g.setGradientFill (ColourGradient (Colour (0xff0c0d0f), r.getTopLeft(), Colour (0xff1c1e22), r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (Colour (0x70000000));
    g.drawRoundedRectangle (r, 7.0f, 1.4f);
    g.setColour (Colour (0x22ffffff));
    g.drawRoundedRectangle (r.reduced (1.5f).translated (0, 1.0f), 6.0f, 0.8f);

    // active glow around the card
    if (level > 0.01f)
    {
        g.setColour (colours::amber.withAlpha (0.55f * level));
        g.drawRoundedRectangle (r.reduced (1.0f), 7.0f, 2.0f);
        g.setColour (colours::amber.withAlpha (0.07f * level));
        g.fillRoundedRectangle (r.reduced (2.0f), 6.0f);
    }
    if (dragOver)
    {
        g.setColour (colours::amberHot.withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (1.0f), 7.0f, 2.5f);
    }

    // slot number + lamp
    auto top = r.reduced (8.0f, 6.0f).removeFromTop (22.0f);
    g.setFont (AmpLookAndFeel::labelFont (15.0f));
    g.setColour (Colour (0xffe9e2d0));
    g.drawText (String (slot + 1), top.removeFromLeft (26.0f), Justification::centredLeft, false);
    auto lamp = top.removeFromRight (16.0f).withSizeKeepingCentre (13.0f, 13.0f);
    if (status.loaded)
    {
        const float b = 0.18f + 0.82f * level;
        g.setColour (colours::amber.withAlpha (0.35f * level));
        g.fillEllipse (lamp.expanded (6.0f * level + 1.0f));
        g.setGradientFill (ColourGradient (colours::amberHot.withMultipliedBrightness (b), lamp.getCentre().translated (-1, -2),
                                           colours::amber.darker (1.4f - b), lamp.getBottomRight(), true));
        g.fillEllipse (lamp);
    }
    else
    {
        g.setColour (Colour (0xff221d18));
        g.fillEllipse (lamp);
    }
    g.setColour (Colour (0x80000000));
    g.drawEllipse (lamp, 1.0f);

    // model name / state, like a little backlit label
    auto nameArea = r.reduced (8.0f, 0.0f).withTrimmedTop (32.0f).withHeight (44.0f);
    g.setColour (Colour (0xff070809));
    g.fillRoundedRectangle (nameArea, 4.0f);
    String text;
    Colour col = colours::lcdText;
    if (status.loading) { text = String::fromUTF8 ("loading\xe2\x80\xa6"); col = colours::lcdText.withAlpha (0.6f); }
    else if (status.missing) { text = status.name + "\nmissing - click to find"; col = colours::warn; }
    else if (status.error.isNotEmpty() && ! status.loaded) { text = "error: " + status.error; col = colours::warn; }
    else if (status.loaded) { text = status.name; col = colours::lcdText.withMultipliedBrightness (0.75f + 0.25f * level); }
    else { text = "empty"; col = Colour (0xff6c6458); }
    g.setColour (col);
    g.setFont (AmpLookAndFeel::labelFont (12.0f, false));
    g.drawFittedText (text, nameArea.reduced (4.0f, 2.0f).toNearestInt(), Justification::centred, 3, 0.85f);
    if (status.loaded && status.detail.isNotEmpty())
    {
        g.setColour (Colour (0xff8f8676));
        g.setFont (AmpLookAndFeel::labelFont (9.5f, false));
        g.drawFittedText (status.detail, nameArea.withY (nameArea.getBottom() + 3.0f).withHeight (24.0f).toNearestInt(), Justification::centredTop, 2, 0.8f);
    }

    drawKnobCaptions (g, { &trim }, 9.5f);
}

void SlotCard::mouseUp (const MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown()) return;
    chooseAndLoad (proc, slot, chooser); // also how a missing file is relinked
}

bool SlotCard::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (isModelFile (f)) return true;
    return false;
}

void SlotCard::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    int target = slot;
    for (auto& f : files)
        if (isModelFile (f) && target < vfxa::kNumSlots) proc.loadAsset (target++, File (f), true);
    repaint();
}

//==============================================================================
IrColumn::IrColumn (VFXAampsProcessor& p, int w) : proc (p), which (w)
{
    namespace P = vfxa::params;
    level.setup (*this, proc.apvts, P::ir (which, "level"), "LEVEL", KnobStyle::Small);
    pan.setup (*this, proc.apvts, P::ir (which, "pan"), "PAN", KnobStyle::Small, true);
    lowCut.setup (*this, proc.apvts, P::ir (which, "lowcut"), "LOW CUT", KnobStyle::Small);
    highCut.setup (*this, proc.apvts, P::ir (which, "highcut"), "HIGH CUT", KnobStyle::Small);
    delay.setup (*this, proc.apvts, P::ir (which, "delay"), "ALIGN", KnobStyle::Small);
    delay.slider.setTooltip ("Time alignment (0-2 ms) to line up two mics");

    addAndMakeVisible (onButton);
    onButton.setTooltip (String ("IR ") + (which == 0 ? "A" : "B") + " on/off");
    onAttachment = std::make_unique<ButtonAttachment> (proc.apvts, P::ir (which, "on"), onButton);
    addAndMakeVisible (phaseButton);
    phaseButton.getProperties().set ("switch", true);
    phaseButton.setTooltip ("Phase invert");
    phaseAttachment = std::make_unique<ButtonAttachment> (proc.apvts, P::ir (which, "phase"), phaseButton);

    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    const int asset = VFXAampsProcessor::irAsset (which);
    loadButton.onClick = [this, asset] { chooseAndLoad (proc, asset, chooser); };
    clearButton.onClick = [this, asset] { proc.clearAsset (asset); };
    loadButton.setTooltip ("Load an impulse response (.wav/.aiff/.flac, any sample rate) - or drop it here");
    clearButton.setTooltip ("Clear this IR");
    refresh();
}

IrColumn::~IrColumn() = default;

void IrColumn::refresh()
{
    status = proc.getAssetStatus (VFXAampsProcessor::irAsset (which));
    clearButton.setEnabled (status.loaded || status.missing);
    repaint();
}

void IrColumn::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto head = r.removeFromTop (24);
    onButton.setBounds (head.removeFromRight (24));
    r.removeFromTop (2);
    plate = r.removeFromTop (40);
    r.removeFromTop (5);
    auto buttons = r.removeFromTop (22);
    clearButton.setBounds (buttons.removeFromRight (26));
    buttons.removeFromRight (4);
    loadButton.setBounds (buttons);
    r.removeFromTop (6);
    const int k = 48, rowH = 66;
    auto row = [&] (Knob& a, Component* b)
    {
        auto line = r.removeFromTop (rowH);
        auto left = line.removeFromLeft (line.getWidth() / 2), right = line;
        a.slider.setBounds (left.removeFromTop (k).withSizeKeepingCentre (k, k));
        if (b != nullptr) b->setBounds (right.removeFromTop (k).withSizeKeepingCentre (k, k));
    };
    row (level, &pan.slider);
    row (lowCut, &highCut.slider);
    row (delay, &phaseButton);
}

void IrColumn::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setFont (AmpLookAndFeel::labelFont (15.0f));
    g.setColour (Colour (0xffe9e2d0));
    g.drawText (which == 0 ? "IR  A" : "IR  B", r.reduced (8.0f, 4.0f).withHeight (24.0f), Justification::centredLeft, false);

    auto p = plate.toFloat();
    g.setColour (Colour (0xff070809));
    g.fillRoundedRectangle (p, 4.0f);
    g.setColour (dragOver ? colours::amberHot : Colour (0x30ffffff));
    g.drawRoundedRectangle (p, 4.0f, dragOver ? 2.0f : 1.0f);
    String text;
    Colour col = colours::lcdText;
    if (status.loading) text = String::fromUTF8 ("loading\xe2\x80\xa6");
    else if (status.missing) { text = status.name + " (missing)"; col = colours::warn; }
    else if (status.error.isNotEmpty() && ! status.loaded) { text = status.error; col = colours::warn; }
    else if (status.loaded) text = status.name;
    else { text = "drop a cab IR"; col = Colour (0xff6c6458); }
    g.setColour (col);
    g.setFont (AmpLookAndFeel::labelFont (12.0f, false));
    g.drawFittedText (text, p.reduced (5.0f, 2.0f).toNearestInt(), Justification::centred, 2, 0.85f);

    drawKnobCaptions (g, { &level, &pan, &lowCut, &highCut, &delay }, 9.5f);
    const auto pb = phaseButton.getBounds().toFloat();
    g.setFont (AmpLookAndFeel::labelFont (9.5f));
    g.setColour (Colour (0xffcfc8b8));
    g.drawText (String::fromUTF8 ("\xc3\x98 PHASE"), Rectangle<float> (pb.getX() - 20.0f, pb.getBottom() - 2.0f, pb.getWidth() + 40.0f, 13.0f),
                Justification::centredTop, false);
}

void IrColumn::mouseUp (const MouseEvent& e)
{
    if (plate.contains (e.getPosition()) && ! e.mouseWasDraggedSinceMouseDown())
        chooseAndLoad (proc, VFXAampsProcessor::irAsset (which), chooser);
}

bool IrColumn::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (isIrFile (f)) return true;
    return false;
}

void IrColumn::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (isIrFile (f)) { proc.loadAsset (VFXAampsProcessor::irAsset (which), File (f), true); break; }
    repaint();
}

} // namespace vfxa::ui
