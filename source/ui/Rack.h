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
    Knob time, division, feedback, tone, mix;
    juce::TextButton sync { "SYNC" }, ping { "PING-PONG" };
    std::unique_ptr<ButtonAttachment> syncAtt, pingAtt;
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
    Knob size, decay, preDelay, tone, mix;
    juce::String shownText;
};

/** The rack: case with rails, holding the three units. Laid out at a fixed base size and scaled by the editor. */
class Rack : public juce::Component
{
public:
    static constexpr int kWidth = 1240, kHeight = 434;
    explicit Rack (VFXAampsProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    EqUnit eq;
    DelayUnit delay;
    ReverbUnit reverb;
};

} // namespace vfxa::ui
