#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

const char* splintDecoName (int i);

struct PadButton : juce::TextButton
{
    std::function<void (bool)> onPress;
    void mouseDown (const juce::MouseEvent& e) override { if (onPress) onPress (true); juce::Component::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (onPress) onPress (false); juce::Component::mouseUp (e); }
};

class WaveView : public juce::Component
{
public:
    explicit WaveView (SplintProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override;
private:
    SplintProcessor& proc;
};

class LaneView : public juce::Component
{
public:
    LaneView (SplintProcessor& p, int laneIndex) : proc (p), lane (laneIndex) {}
    void paint (juce::Graphics& g) override;
private:
    SplintProcessor& proc;
    int lane;
};

class SplintEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit SplintEditor (SplintProcessor&);
    ~SplintEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addSlider (const char* paramId, const juce::String& name);
    void addCombo (const char* paramId, const juce::String& name);
    void addToggle (const char* paramId, const juce::String& name);

    SplintProcessor& proc;
    WaveView wave { proc };
    LaneView laneA { proc, 0 }, laneB { proc, 1 };

    juce::TextButton loadBtn, demoBtn, genBtn;
    juce::TextButton insABtn, insBBtn;
    PadButton fillBtn;
    juce::Label sampleLabel, hint;
    std::array<PadButton, 8> pads;
    std::array<std::unique_ptr<juce::Slider>, 8> padAmt;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> padAmtAtt;

    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> sliderLabels;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label> comboLabels;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAtt;
    juce::HashMap<juce::String, juce::Component*> byId;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component* find (const char* id) { return byId.contains (id) ? byId[id] : nullptr; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplintEditor)
};
