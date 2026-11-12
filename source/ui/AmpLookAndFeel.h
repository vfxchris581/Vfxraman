// VFXAamps — vector-drawn amp-head look: tolex, brushed metal, chicken-head and knurled knobs.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vfxa::ui
{

namespace colours
{
    inline const juce::Colour tolex      { 0xff17181a };
    inline const juce::Colour piping     { 0xffd9cfb8 };
    inline const juce::Colour engraving  { 0xff23252a };
    inline const juce::Colour engraveHi  { 0x66ffffff };
    inline const juce::Colour amber      { 0xffffa63a };
    inline const juce::Colour amberHot   { 0xffffd27a };
    inline const juce::Colour pilotRed   { 0xffff3b2f };
    inline const juce::Colour panelDark  { 0xff15171b };
    inline const juce::Colour lcdText    { 0xffffc46b };
    inline const juce::Colour warn       { 0xffff6b5a };
}

/** Knob styles, chosen with slider.getProperties().set ("style", ...). */
enum class KnobStyle { ChickenHead, Knurled, Small, Rack };

class AmpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmpLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    static void drawChickenHead (juce::Graphics&, juce::Rectangle<float>, float angle, bool hover);
    static void drawKnurled (juce::Graphics&, juce::Rectangle<float>, float angle, bool hover, bool big);
    /** Engraved (debossed) label on brushed metal. */
    static void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float height,
                              juce::Justification = juce::Justification::centred, bool bold = true);
    static void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);
    /** Rack-unit knob: black anodised body, silver cap, white pointer, accent-coloured value ring. */
    static void drawRackKnob (juce::Graphics&, juce::Rectangle<float>, float start, float end, float angle, bool bipolar,
                              juce::Colour accent, bool hover, bool enabled);
    /** Rack button: rubber cap with an LED that lights in the accent colour when on. */
    static void drawRackButton (juce::Graphics&, juce::Button&, bool highlighted, bool down);
    static juce::Font labelFont (float height, bool bold = true);
};

/** Procedurally generated textures, cached per pixel size. */
juce::Image makeTolex (int w, int h, float scale);
juce::Image makeBrushedMetal (int w, int h, float scale);

} // namespace vfxa::ui
