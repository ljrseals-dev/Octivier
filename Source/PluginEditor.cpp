#include "PluginEditor.h"
#include "Presets.h"

OctivierAudioProcessorEditor::OctivierAudioProcessorEditor (OctivierAudioProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setSize (860, 610);
    title.setText ("OCTIVIER", juce::dontSendNotification); title.setFont (juce::Font (28.0f, juce::Font::bold)); title.setColour (juce::Label::textColourId, juce::Colour (0xffe8f2f7)); addAndMakeVisible (title);
    subtitle.setText ("HARMONIC SCULPTOR  /  PITCH-AWARE TIMBRE PROCESSING", juce::dontSendNotification); subtitle.setFont (juce::Font (11.0f)); subtitle.setColour (juce::Label::textColourId, juce::Colour (0xff70cbd1)); addAndMakeVisible (subtitle);
    pitchReadout.setJustificationType (juce::Justification::centredLeft); oddReadout.setJustificationType (juce::Justification::centredLeft); evenReadout.setJustificationType (juce::Justification::centredLeft);
    for (auto* l : { &pitchReadout, &oddReadout, &evenReadout }) { l->setColour (juce::Label::textColourId, juce::Colour (0xffc5d2dc)); l->setFont (juce::Font (12.0f)); addAndMakeVisible (*l); }
    presetBox.addItemList ({ "Manual", "Natural Vocal Double", "Bright Pop Double", "Warm Soul", "Bass Bite", "Sub Power", "Hollow Ghost", "Square Lead", "Wide Choir", "Freeze A3", "Live Tight", "Null / Bypass" }, 1);
    presetBox.onChange = [this]
    {
        const int selected = presetBox.getSelectedItemIndex();
        if (selected <= 0) return; // Manual preserves the current parameter state.
        octivier::presets::apply (selected - 1, processor.parameters);
    };
    addAndMakeVisible (presetBox);
    saveButton.onClick = [this] { auto xml = processor.parameters.copyState().createXml();
        if (xml != nullptr)
        {
            auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Octivier-State.xml");
            f.replaceWithText (xml->toString());
        } };
    addAndMakeVisible (saveButton);

    addSlider (strength, "STRENGTH", "strength"); addSlider (focus, "HARMONIC FOCUS", "focus"); addSlider (oddCut, "ODD CUT (dB)", "oddCut"); addSlider (evenBoost, "EVEN BOOST (dB)", "evenBoost");
    addSlider (tracking, "TRACKING", "tracking"); addSlider (gate, "GATE (dBFS)", "gate"); addSlider (delay, "DELAY (ms)", "delay"); addSlider (width, "WIDTH", "width");
    addSlider (modDepth, "MOD DEPTH (ms)", "modDepth"); addSlider (formant, "FORMANT TONE (dB)", "formant"); addSlider (air, "AIR TONE (dB)", "air"); addSlider (charAmount, "CHARACTER AMOUNT", "characterAmt");
    addSlider (mix, "DRY BLEND", "mix"); addSlider (output, "OUTPUT (dB)", "output"); addSlider (manualF0, "MANUAL F0 (Hz)", "manualF0");
    mode.addItemList ({ "Classic", "Sub", "Hollow", "Square" }, 1); addAndMakeVisible (mode); comboAttachments.push_back (std::make_unique<ComboAttachment> (processor.parameters, "mode", mode));
    character.addItemList ({ "Clean", "Tube", "Tape" }, 1); addAndMakeVisible (character); comboAttachments.push_back (std::make_unique<ComboAttachment> (processor.parameters, "character", character));
    addAndMakeVisible (freeze); buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.parameters, "freeze", freeze));
    addAndMakeVisible (autoGain); buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.parameters, "autoGain", autoGain));
    startTimerHz (12);
}

OctivierAudioProcessorEditor::~OctivierAudioProcessorEditor() { stopTimer(); }
void OctivierAudioProcessorEditor::configureSlider (juce::Slider& s, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff65d4d0)); s.setColour (juce::Slider::thumbColourId, juce::Colour (0xffffb45e)); s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe4edf2));
    s.setName (name); addAndMakeVisible (s);
}
void OctivierAudioProcessorEditor::addSlider (juce::Slider& s, const juce::String& labelText, const char* id)
{
    configureSlider (s, labelText);
    auto label = std::make_unique<juce::Label>(); label->setText (labelText, juce::dontSendNotification); label->setJustificationType (juce::Justification::centred); label->setFont (juce::Font (10.0f, juce::Font::bold)); label->setColour (juce::Label::textColourId, juce::Colour (0xff9cabb8)); addAndMakeVisible (*label); labels.push_back (std::move (label));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, id, s));
}
void OctivierAudioProcessorEditor::timerCallback()
{
    const float f0 = processor.getTrackedFundamental();
    pitchReadout.setText (processor.isTracking() ? "TRACK  " + juce::String (f0, 1) + " Hz  /  " + juce::String (processor.getPitchConfidence() * 100.0f, 0) + "% confidence" : "TRACK  —  waiting for stable monophonic pitch", juce::dontSendNotification);
    oddReadout.setText ("ODD ENERGY  " + juce::String (processor.getOddEnergyDb(), 1) + " dB", juce::dontSendNotification);
    evenReadout.setText ("EVEN ENERGY  " + juce::String (processor.getEvenEnergyDb(), 1) + " dB", juce::dontSendNotification);
}
void OctivierAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff11171d));
    g.setColour (juce::Colour (0xff1c2832)); g.fillRoundedRectangle (12.0f, 12.0f, static_cast<float> (getWidth()-24), 76.0f, 8.0f);
    g.setColour (juce::Colour (0xff65d4d0)); g.fillRect (12.0f, 12.0f, 4.0f, 76.0f);
    g.setColour (juce::Colour (0xff273640)); g.drawHorizontalLine (100, 16.0f, static_cast<float> (getWidth()-16));
    g.setColour (juce::Colour (0xffe4edf2)); g.setFont (juce::Font (10.0f)); g.drawText ("TIMBRE SHAPING • NOT PITCH TRANSLATION", 18, getHeight()-22, getWidth()-36, 14, juce::Justification::centredRight);
}
void OctivierAudioProcessorEditor::resized()
{
    title.setBounds (26, 18, 250, 36); subtitle.setBounds (28, 52, 420, 20); presetBox.setBounds (470, 30, 220, 28); saveButton.setBounds (700, 30, 130, 28);
    pitchReadout.setBounds (24, 94, 420, 22); oddReadout.setBounds (470, 94, 150, 22); evenReadout.setBounds (625, 94, 200, 22);
    const int left = 16, top = 126, colW = (getWidth()-32)/5, rowH = 112;
    std::array<juce::Slider*,15> sliders { &strength,&focus,&oddCut,&evenBoost,&tracking,&gate,&delay,&width,&modDepth,&formant,&air,&charAmount,&mix,&output,&manualF0 };
    for (size_t i=0;i<sliders.size();++i)
    {
        const int col = static_cast<int>(i%5), row = static_cast<int>(i/5);
        sliders[i]->setBounds (left+col*colW+4, top+row*rowH+20, colW-8, 84);
        // labels are kept in creation order, one per slider
        if (i<labels.size()) labels[i]->setBounds (left+col*colW, top+row*rowH, colW, 18);
    }
    mode.setBounds (22, 472, 140, 28); character.setBounds (174, 472, 120, 28); freeze.setBounds (310, 472, 170, 28); autoGain.setBounds (490, 472, 170, 28);
}
