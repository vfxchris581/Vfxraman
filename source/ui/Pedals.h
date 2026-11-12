// VFXAamps — pedalboard: two NAM pedal loaders in front of the amp.
#pragma once

#include "AmpComponents.h"

namespace vfxa::ui
{

/** Chrome stomp switch (top view). */
class Footswitch : public juce::Button
{
public:
    Footswitch() : juce::Button ("footswitch") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** One stompbox: LED, name display, load / clear, level, footswitch. Drop a .nam on it to load. */
class PedalUnit : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::SettableTooltipClient
{
public:
    PedalUnit (VFXAampsProcessor&, int pedal, juce::Colour paint);
    void refresh();
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    VFXAampsProcessor& proc;
    const int pedal;
    const int asset;
    juce::Colour paintColour;
    Knob level;
    Footswitch footswitch;
    std::unique_ptr<ButtonAttachment> footAtt;
    juce::TextButton loadButton { "LOAD" }, clearButton { juce::String::fromUTF8 ("\xc3\x97") };
    VFXAampsProcessor::AssetStatus status;
    float led = 0.0f;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

/** The board the two pedals sit on, with patch cables guitar -> pedal 1 -> pedal 2 -> amp.
    Laid out at a fixed base size and scaled by the editor. */
class Pedalboard : public juce::Component
{
public:
    static constexpr int kWidth = 1240, kHeight = 186;          // horizontal strip (pedals only)
    static constexpr int kColumnWidth = 236, kColumnHeight = 534; // vertical column beside the rack (rack height x 1004 / 1240)
    explicit Pedalboard (VFXAampsProcessor&);
    void setVertical (bool v);
    bool isVertical() const noexcept { return vertical; }
    void tick();
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void paintVertical (juce::Graphics&);
    PedalUnit p1, p2;
    bool vertical = false;
};

} // namespace vfxa::ui
