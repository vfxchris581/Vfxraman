// VFXAamps — amp-head building blocks: NAM slot cards and IR columns.
#pragma once

#include "../PluginProcessor.h"
#include "AmpLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vfxa::ui
{

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

/** A labelled knob: the slider plus an engraved caption drawn by the parent. */
struct Knob
{
    juce::Slider slider;
    juce::String caption;
    std::unique_ptr<SliderAttachment> attachment;

    void setup (juce::Component& parent, juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& captionText, KnobStyle style, bool bipolar = false);
};

/** Opens a file chooser for an asset (a .nam model or an IR) and loads the result. */
void chooseAndLoad (VFXAampsProcessor&, int asset, std::unique_ptr<juce::FileChooser>& holder);
bool isModelFile (const juce::String& path);
bool isIrFile (const juce::String& path);

//==============================================================================
/** One of the ten NAM loaders: lamp (glows with the slot's share of the blend), name, load/clear, level trim. */
class SlotCard : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 public juce::SettableTooltipClient
{
public:
    SlotCard (VFXAampsProcessor&, int slot);
    ~SlotCard() override;

    void refresh();                 // status changed
    void setLevel (float level);    // 0..1 share of the blend

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    VFXAampsProcessor& proc;
    const int slot;
    Knob trim;
    juce::TextButton loadButton { "LOAD" }, clearButton { juce::String::fromUTF8 ("\xc3\x97") };
    juce::TextButton irButton { "IR" };            // use the dual IR (cab) while this slot plays
    std::unique_ptr<ButtonAttachment> irAtt;
    VFXAampsProcessor::AssetStatus status;
    float level = 0.0f;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
/** IR A or IR B: name plate, load/clear, on, phase, and level/pan/low-cut/high-cut/delay knobs. */
class IrColumn : public juce::Component,
                 public juce::FileDragAndDropTarget
{
public:
    IrColumn (VFXAampsProcessor&, int which);
    ~IrColumn() override;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    VFXAampsProcessor& proc;
    const int which;
    Knob level, pan, lowCut, highCut, delay;
    juce::ToggleButton onButton, phaseButton;
    std::unique_ptr<ButtonAttachment> onAttachment, phaseAttachment;
    juce::TextButton loadButton { "LOAD" }, clearButton { juce::String::fromUTF8 ("\xc3\x97") };
    VFXAampsProcessor::AssetStatus status;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> plate;
};

/** Draws an engraved caption under each knob of a component (call from the owner's paintOverChildren). */
void drawKnobCaptions (juce::Graphics&, std::initializer_list<const Knob*> knobs, float height = 10.5f);

} // namespace vfxa::ui
