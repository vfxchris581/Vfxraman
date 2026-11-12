// VFXAamps — the effects rack under the amp head (graphic EQ, delay, reverb) and the header tuner.
#pragma once

#include "AmpComponents.h"

namespace vfxa::ui
{

/** Header tuner: note, cents needle, frequency. Click to mute the output while tuning. */
class TunerDisplay : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    explicit TunerDisplay (VFXAampsProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    VFXAampsProcessor& proc;
    bool hasNote = false, muted = false;
    int note = 0;
    double cents = 0.0, hz = 0.0;
    juce::uint32 lastValid = 0;
};

/** Base for one 19-inch rack unit: faceplate, ears, screws, maker's block with model name and power switch. */
class RackUnit : public juce::Component
{
public:
    RackUnit (VFXAampsProcessor&, juce::String model, juce::String title, juce::Colour face, juce::Colour accent);
    void paint (juce::Graphics&) override;
    virtual void tick() {}

protected:
    virtual void paintPanel (juce::Graphics&) {}
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& caption, bool bipolar = false);
    void setupButton (juce::TextButton&, std::unique_ptr<ButtonAttachment>&, const juce::String& paramId);
    void drawCaptions (juce::Graphics&, std::initializer_list<Knob*>) const;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>, const juce::String& big, const juce::String& unit,
                      const juce::String& small, bool lit) const;
    void layoutPowerButton();
    bool isOn() const;
    /** Radio buttons for a choice parameter (mode / program), lit for the current value. */
    void setupChoiceButtons (juce::TextButton* buttons, int count, const juce::String& paramId, const juce::StringArray& labels);
    void updateChoiceButtons (juce::TextButton* buttons, int count, const juce::String& paramId);
    void layoutGrid (std::initializer_list<juce::Component*> row1, std::initializer_list<juce::Component*> row2);

    VFXAampsProcessor& proc;
    juce::String model, title;
    juce::Colour faceColour, accent;
    juce::TextButton power { "ON" };
    std::unique_ptr<ButtonAttachment> powerAtt;
    static constexpr int kEar = 44;
};

class EqUnit : public RackUnit
{
public:
    explicit EqUnit (VFXAampsProcessor&);
    void resized() override;
    void tick() override;

private:
    void paintPanel (juce::Graphics&) override;
    juce::Rectangle<float> screen() const;
    std::array<juce::Slider, 10> faders;
    std::array<std::unique_ptr<SliderAttachment>, 10> faderAtt;
    juce::TextButton flat { "FLAT" };
    std::array<juce::TextButton, 2> typeButtons;
    std::array<float, 10> shown {};
    bool shownOn = false;
};

class DelayUnit : public RackUnit
{
public:
    explicit DelayUnit (VFXAampsProcessor&);
    void resized() override;
    void tick() override;

private:
    void paintPanel (juce::Graphics&) override;
    juce::Rectangle<float> screen() const;
    Knob time, division, feedback, diffusion, mix, lowCut, highCut, modDepth, modRate, duck;
    juce::TextButton sync { "SYNC" }, ping { "PING-PONG" };
    std::unique_ptr<ButtonAttachment> syncAtt, pingAtt;
    std::array<juce::TextButton, 6> modes;
    juce::String shownText;
};

class ReverbUnit : public RackUnit
{
public:
    explicit ReverbUnit (VFXAampsProcessor&);
    void resized() override;
    void tick() override;

private:
    void paintPanel (juce::Graphics&) override;
    juce::Rectangle<float> screen() const;
    Knob decay, preDelay, size, diffusion, mod, mix, lowRT, highRT, early, lowCut, highCut, width;
    std::array<juce::TextButton, 6> programs;
    juce::String shownText;
};

/** VX-31: dual 31-band graphic EQ / limiter (shown in the EQ slot when EQ TYPE is 31-BAND). The faders show one channel
    at a time (A / B); with LINK on, B follows A. */
class Eq31Unit : public RackUnit
{
public:
    explicit Eq31Unit (VFXAampsProcessor&);
    void resized() override;
    void tick() override;

private:
    void paintPanel (juce::Graphics&) override;
    void attachChannel (int ch);
    juce::String faderText (double position) const;
    std::array<juce::Slider, 31> faders;
    std::array<std::unique_ptr<SliderAttachment>, 31> faderAtt;
    Knob input, threshold;
    juce::TextButton chanA { "A" }, chanB { "B" }, link { "LINK" }, range { "+/-15" }, lowCut { "LOW CUT" }, nr { "NR" }, limit { "LIMIT" },
                     flat { "FLAT" };
    std::unique_ptr<ButtonAttachment> linkAtt, rangeAtt, lowCutAtt, nrAtt, limitAtt;
    std::array<juce::TextButton, 2> typeButtons;
    int channel = 0, attached = -1;
    std::array<float, 4> leds {};   // shown meter values: out dBFS, limiter GR, NR dB, (unused)
};

/** Tube opto leveling amp: VU meter (gain reduction / output), GAIN, PEAK REDUCTION, COMPRESS / LIMIT,
    HF emphasis, mix and the chain position (pre amp / post cab / end). */
class CompUnit : public RackUnit
{
public:
    explicit CompUnit (VFXAampsProcessor&);
    void resized() override;
    void tick() override;

private:
    void paintPanel (juce::Graphics&) override;
    juce::Rectangle<float> meterArea() const { return { 268.0f, 12.0f, 236.0f, static_cast<float> (getHeight()) - 24.0f }; }
    Knob gain, peak, emphasis, mix;
    std::array<juce::TextButton, 2> modeButtons;
    std::array<juce::TextButton, 3> positionButtons;
    juce::TextButton meterButton { "OUTPUT" };
    std::unique_ptr<ButtonAttachment> meterAtt;
    float needle = 0.0f;     // VU, -20 .. +3
    bool shownOutput = false;
};

/** The rack: case with rails, holding the three units. Laid out at a fixed base size and scaled by the editor. */
class Rack : public juce::Component
{
public:
    static constexpr int kWidth = 1240, kHeight = 660;
    explicit Rack (VFXAampsProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    VFXAampsProcessor& proc;
    CompUnit comp;
    EqUnit eq;
    Eq31Unit eq31;
    DelayUnit delay;
    ReverbUnit reverb;
};

} // namespace vfxa::ui
