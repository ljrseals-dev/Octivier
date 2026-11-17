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
    // A sustained note followed by silence must not leave a stale fundamental active.
    {
        juce::AudioBuffer<float> silence (2, 512);
        silence.clear();
        for (int pass = 0; pass < 20; ++pass)
            p.processBlock (silence, midi);
        if (p.getTrackedFundamental() != 0.0f)
        {
            std::cerr << "FAIL: pitch gate retained a stale fundamental after silence\\n";
            return 1;
        }
    }

    // The right-channel delay LFO must remain smooth across delay-ring wraps.
    {
        if (auto* widthParam = p.parameters.getParameter ("width"))
            widthParam->setValueNotifyingHost (widthParam->convertTo0to1 (1.0f));
        if (auto* modParam = p.parameters.getParameter ("modDepth"))
            modParam->setValueNotifyingHost (modParam->convertTo0to1 (4.0f));
        if (auto* strengthParam = p.parameters.getParameter ("strength"))
            strengthParam->setValueNotifyingHost (strengthParam->convertTo0to1 (0.0f));

        juce::AudioBuffer<float> block (2, 4096);
        float previousRight = 0.0f;
        float maxAdjacentJump = 0.0f;
        std::uint64_t sample = 0;
        for (int pass = 0; pass < 40; ++pass)
        {
            for (int i = 0; i < block.getNumSamples(); ++i, ++sample)
            {
                const float value = 0.1f * std::sin (juce::MathConstants<float>::twoPi
                                                     * 220.0f * static_cast<float> (sample) / 48000.0f);
                block.setSample (0, i, value);
                block.setSample (1, i, value);
            }
            p.processBlock (block, midi);
            for (int i = 0; i < block.getNumSamples(); ++i)
            {
                const float current = block.getSample (1, i);
                const float jump = std::abs (current - previousRight);
                if (jump > maxAdjacentJump) maxAdjacentJump = jump;
                previousRight = current;
            }
        }
        if (maxAdjacentJump > 0.05f)
        {
            std::cerr << "FAIL: delay LFO discontinuity, max adjacent jump="
                      << maxAdjacentJump << '\\n';
            return 1;
        }
    }

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
