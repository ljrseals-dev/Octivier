#pragma once
#include <JuceHeader.h>
#include <array>
#include <string_view>

namespace octivier::presets
{
    struct ParamValue { const char* id; float value; };

    // A factory preset is a complete 19-parameter snapshot. Every entry is
    // written on selection so no value leaks from the previously selected preset.
    struct Preset
    {
        std::string_view name;
        std::array<ParamValue, 19> params;
    };

    inline constexpr float kDefStrength  = 0.78f;
    inline constexpr float kDefFocus     = 0.00f;
    inline constexpr float kDefOddCut    = 18.0f;
    inline constexpr float kDefEvenBoost = 8.00f;
    inline constexpr float kDefTracking  = 0.62f;
    inline constexpr float kDefGate      = -55.0f;
    inline constexpr float kDefDelay     = 42.0f;
    inline constexpr float kDefWidth     = 0.55f;
    inline constexpr float kDefModDepth  = 0.35f;
    inline constexpr float kDefFormant   = 0.00f;
    inline constexpr float kDefAir       = 0.00f;
    inline constexpr float kDefMode      = 0.00f;
    inline constexpr float kDefCharacter = 0.00f;
    inline constexpr float kDefCharAmt   = 0.00f;
    inline constexpr float kDefMix       = 0.00f;
    inline constexpr float kDefOutput    = 0.00f;
    inline constexpr float kDefAutoGain  = 1.00f;
    inline constexpr float kDefFreeze    = 0.00f;
    inline constexpr float kDefManualF0  = 220.0f;

    inline Preset makePreset (std::string_view name,
        float strength = kDefStrength, float focus = kDefFocus,
        float oddCut = kDefOddCut, float evenBoost = kDefEvenBoost,
        float tracking = kDefTracking, float gate = kDefGate,
        float delay = kDefDelay, float width = kDefWidth, float modDepth = kDefModDepth,
        float formant = kDefFormant, float air = kDefAir,
        float mode = kDefMode, float character = kDefCharacter, float charAmt = kDefCharAmt,
        float mix = kDefMix, float output = kDefOutput,
        float autoGain = kDefAutoGain, float freeze = kDefFreeze, float manualF0 = kDefManualF0)
    {
        return Preset { name, {{
            { "strength", strength }, { "focus", focus },
            { "oddCut", oddCut }, { "evenBoost", evenBoost },
            { "tracking", tracking }, { "gate", gate },
            { "delay", delay }, { "width", width },
            { "modDepth", modDepth }, { "formant", formant },
            { "air", air }, { "mode", mode },
            { "character", character }, { "characterAmt", charAmt },
            { "mix", mix }, { "output", output },
            { "autoGain", autoGain }, { "freeze", freeze },
            { "manualF0", manualF0 }
        }} };
    }

    inline const std::array<Preset, 11>& all()
    {
        static const std::array<Preset, 11> table {{
            makePreset ("Natural Vocal Double"),
            makePreset ("Bright Pop Double", 0.90f, 0.45f, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, 38.0f, 0.70f, kDefModDepth,
                        kDefFormant, 2.0f),
            makePreset ("Warm Soul", 0.65f, -0.35f, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, kDefDelay, kDefWidth, kDefModDepth,
                        kDefFormant, kDefAir, kDefMode, 1.0f, 0.12f),
            makePreset ("Bass Bite", 0.65f, -0.50f, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, kDefDelay, kDefWidth, kDefModDepth,
                        kDefFormant, kDefAir, 3.0f),
            makePreset ("Sub Power", 0.80f, kDefFocus, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, kDefDelay, kDefWidth, kDefModDepth,
                        kDefFormant, kDefAir, 1.0f),
            makePreset ("Hollow Ghost", 0.80f, kDefFocus, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, kDefDelay, kDefWidth, kDefModDepth,
                        kDefFormant, kDefAir, 2.0f),
            makePreset ("Square Lead", 0.90f, kDefFocus, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, kDefDelay, kDefWidth, kDefModDepth,
                        kDefFormant, kDefAir, 3.0f),
            makePreset ("Wide Choir", 0.70f, kDefFocus, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, 52.0f, 0.90f),
            makePreset ("Freeze A3", kDefStrength, kDefFocus, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, kDefDelay, kDefWidth, kDefModDepth,
                        kDefFormant, kDefAir, kDefMode, kDefCharacter, kDefCharAmt,
                        kDefMix, kDefOutput, kDefAutoGain, 1.0f, 220.0f),
            makePreset ("Live Tight", kDefStrength, kDefFocus, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, 18.0f, 0.20f, 0.0f),
            makePreset ("Null Bypass", 0.0f, kDefFocus, kDefOddCut, kDefEvenBoost,
                        kDefTracking, kDefGate, kDefDelay, kDefWidth, kDefModDepth,
                        kDefFormant, kDefAir, kDefMode, kDefCharacter, kDefCharAmt,
                        1.0f)
        }};
        return table;
    }

    inline void apply (int index, juce::AudioProcessorValueTreeState& apvts)
    {
        const auto& table = all();
        if (index < 0 || index >= static_cast<int> (table.size())) return;
        for (const auto& pv : table[static_cast<size_t> (index)].params)
            if (auto* param = apvts.getParameter (pv.id))
                param->setValueNotifyingHost (param->convertTo0to1 (pv.value));
    }
}
