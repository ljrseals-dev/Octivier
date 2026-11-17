#include "AcfPitchTracker.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

bool checkPitch (double sr, int frameSize, float minF0, float maxF0,
                 double hz, bool saw)
{
    AcfPitchTracker tracker;
    if (! tracker.prepare (sr, frameSize, minF0, maxF0))
    {
        std::cerr << "FAIL: ACF prepare sr=" << sr << " frame=" << frameSize << '\n';
        return false;
    }

    std::vector<float> frame (static_cast<std::size_t> (frameSize));
    double phase = 0.217;
    for (int i = 0; i < frameSize; ++i)
    {
        const double cycles = hz * i / sr + phase;
        const double fraction = cycles - std::floor (cycles);
        frame[static_cast<std::size_t> (i)] = saw
            ? static_cast<float> (2.0 * fraction - 1.0)
            : static_cast<float> (std::sin (2.0 * kPi * cycles));
    }

    float confidence = 0.0f;
    const float detected = tracker.estimate (frame.data(), frameSize, -90.0f, 0.62f, confidence);
    const double cents = detected > 0.0f
        ? std::abs (1200.0 * std::log2 (detected / hz)) : 1.0e6;
    if (detected <= 0.0f || cents > 35.0 || confidence < 0.1f)
    {
        std::cerr << "FAIL: ACF " << (saw ? "saw" : "sine") << " sr=" << sr
                  << " target=" << hz << " detected=" << detected
                  << " cents=" << cents << " conf=" << confidence << '\n';
        return false;
    }
    return true;
}
}

int main()
{
    bool ok = true;
    // The 40 Hz low edge requires 4,096 samples at 48 kHz and 8,192 at 96 kHz.
    for (int midi = 28; midi <= 96; ++midi)
    {
        const double hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        ok &= checkPitch (48000.0, 4096, 40.0f, 2200.0f, hz, false);
        ok &= checkPitch (48000.0, 4096, 40.0f, 2200.0f, hz, true);
    }
    for (int midi = 28; midi <= 96; ++midi)
    {
        const double hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        ok &= checkPitch (96000.0, 8192, 40.0f, 2200.0f, hz, false);
        ok &= checkPitch (96000.0, 8192, 40.0f, 2200.0f, hz, true);
    }

    AcfPitchTracker invalidInputTracker;
    invalidInputTracker.prepare (48000.0, 2048, 55.0f, 1200.0f);
    std::array<float, 2048> invalidFrame {};
    invalidFrame[512] = std::numeric_limits<float>::quiet_NaN();
    float invalidConfidence = 0.9f;
    if (invalidInputTracker.estimate (invalidFrame.data(), 2048, -90.0f, 0.62f,
                                      invalidConfidence) != 0.0f
        || invalidConfidence != 0.0f)
    {
        std::cerr << "FAIL: ACF did not reject a non-finite input frame safely\n";
        ok = false;
    }

    if (! ok) return 1;
    std::cout << "PASS: ACF clean sine/saw semitone sweep E1-C7 at 48/96 kHz, "
                 "including low-F0 reachability and non-finite input rejection.\n";
    return 0;
}
