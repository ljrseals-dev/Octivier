#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class OctivierAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit OctivierAudioProcessorEditor (OctivierAudioProcessor&);
    ~OctivierAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    OctivierAudioProcessor& processor;

    juce::Label title, subtitle, pitchReadout, oddReadout, evenReadout;

    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save State" };

    juce::Slider strength, focus, oddCut, evenBoost, tracking, gate;
    juce::Slider delay, width, modDepth, formant, air, charAmount, mix, output, manualF0;

    juce::ToggleButton freeze { "Freeze / Manual F0" }, autoGain { "RMS Compensation" };
    juce::ComboBox mode, character;

    std::vector<std::unique_ptr<juce::Component>>    labels;
    std::vector<std::unique_ptr<SliderAttachment>>   sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>>   buttonAttachments;
    std::vector<std::unique_ptr<ComboAttachment>>    comboAttachments;

    int pitchHoldFrames = 0;

    void configureSlider (juce::Slider&, const juce::String&);
    void addSlider (juce::Slider&, const juce::String&, const char*);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctivierAudioProcessorEditor)
};
