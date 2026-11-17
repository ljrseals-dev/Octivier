#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"
#include <iostream>
#include <cmath>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    OctivierAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> b (2, 48000);
    juce::MidiBuffer midi;
    // A 220 Hz harmonic-rich synthetic source. This is a crash/finite-output smoke test,
    // not a perceptual or plugin-host certification test.
    for (int i=0; i<b.getNumSamples(); ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        float x = 0.0f;
        for (int h=1; h<=12; ++h) x += (0.08f / h) * std::sin (juce::MathConstants<float>::twoPi * 220.0f * h * t);
        b.setSample (0, i, x); b.setSample (1, i, x);
    }
    for (int start=0; start<b.getNumSamples(); start += 512)
    {
        juce::AudioBuffer<float> block (2, juce::jmin (512, b.getNumSamples()-start));
        for (int ch=0; ch<2; ++ch) block.copyFrom (ch, 0, b, ch, start, block.getNumSamples());
        p.processBlock (block, midi);
        for (int ch=0; ch<2; ++ch)
            for (int i=0; i<block.getNumSamples(); ++i)
                if (!std::isfinite (block.getSample (ch, i))) { std::cerr << "FAIL: non-finite output\n"; return 1; }
    }
    juce::MemoryBlock state; p.getStateInformation (state);
    if (state.getSize() == 0) { std::cerr << "FAIL: empty state\n"; return 1; }
    // Preset switching must overwrite values unique to the previously selected preset.
    octivier::presets::apply (2, p.parameters);  // Warm Soul (Tube)
    octivier::presets::apply (10, p.parameters); // Null Bypass (Clean)
    auto* character = p.parameters.getRawParameterValue ("character");
    auto* characterAmt = p.parameters.getRawParameterValue ("characterAmt");
    auto* strength = p.parameters.getRawParameterValue ("strength");
    auto* mix = p.parameters.getRawParameterValue ("mix");
    if (character == nullptr || characterAmt == nullptr || strength == nullptr || mix == nullptr
        || character->load() != 0.0f || characterAmt->load() != 0.0f
        || strength->load() != 0.0f || mix->load() != 1.0f)
    {
        std::cerr << "FAIL: preset switch retained stale parameter values\\n";
        return 1;
    }

    std::cout << "PASS: finite-output, state-serialization, and preset-snapshot smoke checks. Not a host or sound-quality certification.\\n";
    return 0;
}
