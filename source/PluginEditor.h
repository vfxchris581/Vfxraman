// VFXAamps — amp-head editor (resizable, vector-drawn).
#pragma once

#include "PluginProcessor.h"
#include "ui/AmpComponents.h"
#include "ui/AmpLookAndFeel.h"

namespace vfxa::ui
{

/** The whole amp head, laid out at a fixed base size and scaled by the editor. */
class AmpFace : public juce::Component,
                public juce::FileDragAndDropTarget
{
public:
    static constexpr int kWidth = 1240, kHeight = 760;

    explicit AmpFace (VFXAampsProcessor&);
    ~AmpFace() override;

    void tick();            // ~30 Hz from the editor
    void refreshStatus();   // assets / presets changed

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void paintBackground (juce::Graphics&);
    void paintGainScale (juce::Graphics&);
    void rebuildPresetList();
    void showPresetMenu();
    void updateLicenseUi();
    void showLicensePanel();

    VFXAampsProcessor& proc;
    Knob inTrim, gateThresh, gateRel, morph, bass, mid, treble, presence, output, blend;
    juce::ToggleButton gateOn, limiter, irOn, normalize;
    std::unique_ptr<ButtonAttachment> gateOnAtt, limiterAtt, irOnAtt, normalizeAtt;
    juce::ComboBox osBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAtt;
    juce::TextButton presetButton, saveButton { "SAVE" };
    std::array<std::unique_ptr<SlotCard>, vfxa::kNumSlots> slots;
    std::array<std::unique_ptr<IrColumn>, 2> irColumns;

    struct RelinkBanner : juce::Component
    {
        juce::TextButton findButton { "FIND IN FOLDER" }, dismissButton { "DISMISS" };
        juce::String text;
        RelinkBanner();
        void paint (juce::Graphics&) override;
        void resized() override;
    } banner;
    bool bannerDismissed = false;

    /** Header button: "TRIAL · N DAYS LEFT" / "TRIAL ENDED", hidden once licensed. */
    juce::TextButton licenseButton;
    /** Key entry, drawn over the amp (no separate window, so it can't end up behind the host). */
    struct LicensePanel : juce::Component
    {
        juce::TextEditor keyField;
        juce::TextButton activateButton { "ACTIVATE" }, closeButton { "NOT NOW" };
        juce::String title, message, result;
        bool resultIsError = false;
        LicensePanel();
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::Rectangle<int> card() const;
    } licensePanel;
    int shownLicenseState = -1, shownDaysLeft = -1;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image tolex, metal;
    juce::Image logoTree, logoWord;  // brand artwork (resources/logo-*.png)
    float textureScale = 0.0f;
    std::array<float, vfxa::kNumSlots> levels {};
    float cpu = 0.0f, inPeak = 0.0f;
    int models = 0;
};

} // namespace vfxa::ui

class VFXAampsEditor : public juce::AudioProcessorEditor,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit VFXAampsEditor (VFXAampsProcessor&);
    ~VFXAampsEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;
    void timerCallback() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    VFXAampsProcessor& proc;
    vfxa::ui::AmpLookAndFeel lnf;
    vfxa::ui::AmpFace face;
    juce::TooltipWindow tooltips { this, 700 };
};
