#include "AcfPitchTracker.h"
#include "YinPitchTracker.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
bool checkYin (double sr, float minF0, float maxF0, float multiplier, int expectedFrame)
{
    YinPitchTracker tracker;
    if (! tracker.prepare (sr, minF0, maxF0, multiplier))
    {
        std::cerr << "FAIL: YIN prepare rejected " << sr << " Hz / " << minF0
                  << " Hz / " << multiplier << "x\n";
        return false;
    }
    if (tracker.getFrameSize() != expectedFrame
        || tracker.getTauMax() < static_cast<int> (std::ceil (sr / minF0)))
    {
        std::cerr << "FAIL: YIN frame does not cover requested low F0 at "
                  << sr << " Hz; frame=" << tracker.getFrameSize()
                  << " tauMax=" << tracker.getTauMax() << '\n';
        return false;
    }

    std::vector<float> frame (static_cast<std::size_t> (tracker.getFrameSize()));
    constexpr double twoPi = 6.28318530717958647692;
    for (int i = 0; i < tracker.getFrameSize(); ++i)
        frame[static_cast<std::size_t> (i)] = 0.5f * static_cast<float> (
            std::sin (twoPi * minF0 * i / sr));

    float confidence = 0.0f;
    const float detected = tracker.estimate (frame.data(), tracker.getFrameSize(),
                                               -90.0f, confidence);
    const float cents = detected > 0.0f
        ? std::abs (1200.0f * std::log2 (detected / minF0)) : 1.0e6f;
    if (detected <= 0.0f || cents > 15.0f || confidence <= 0.0f)
    {
        std::cerr << "FAIL: YIN low-frequency fixture at " << sr
                  << " Hz detected=" << detected << " target=" << minF0
                  << " confidence=" << confidence << '\n';
        return false;
    }
    return true;
}
}

int main()
{
    bool ok = true;
    ok &= checkYin (48000.0, 55.0f, 2200.0f, 2.0f, 2048);
    ok &= checkYin (96000.0, 55.0f, 2200.0f, 2.0f, 4096);
    ok &= checkYin (48000.0, 55.0f, 2200.0f, 2.5f, 4096);
    ok &= checkYin (96000.0, 55.0f, 2200.0f, 2.5f, 8192);
    ok &= checkYin (48000.0, 40.0f, 2200.0f, 2.5f, 4096);
    ok &= checkYin (96000.0, 40.0f, 2200.0f, 2.5f, 8192);

    YinPitchTracker invalid;
    if (invalid.prepare (96000.0, 55.0f, 2200.0f, 1.0f)
        || invalid.prepare (96000.0, 0.0f, 2200.0f)
        || invalid.prepare (96000.0, 2200.0f, 55.0f))
    {
        std::cerr << "FAIL: invalid YIN configuration was accepted\n";
        ok = false;
    }

    YinPitchTracker nyquistInvalid;
    if (nyquistInvalid.prepare (48000.0, 40.0f, 24000.0f))
    {
        std::cerr << "FAIL: YIN accepted a max F0 at Nyquist\n";
        ok = false;
    }

    YinPitchTracker overflowTracker;
    if (! overflowTracker.prepare (48000.0, 55.0f, 1200.0f))
    {
        std::cerr << "FAIL: YIN overflow fixture prepare\n";
        ok = false;
    }
    else
    {
        std::vector<float> extremeFrame (static_cast<std::size_t> (overflowTracker.getFrameSize()));
        for (std::size_t i = 0; i < extremeFrame.size(); ++i)
            extremeFrame[i] = (i & 1U) != 0U ? -std::numeric_limits<float>::max()
                                             : std::numeric_limits<float>::max();
        float extremeConfidence = 0.8f;
        if (overflowTracker.estimate (extremeFrame.data(), overflowTracker.getFrameSize(),
                                      -90.0f, extremeConfidence) != 0.0f
            || extremeConfidence != 0.0f)
        {
            std::cerr << "FAIL: YIN did not reject an unrepresentable difference function\n";
            ok = false;
        }
    }

    AcfPitchTracker acf;
    if (! acf.prepare (48000.0, 2048, 55.0f, 1200.0f)
        || acf.prepare (96000.0, 2048, 55.0f, 1200.0f)
        || acf.prepare (48000.0, 2048, 55.0f, 24000.0f)
        || acf.prepare (48000.0, 64, 55.0f, 1200.0f)
        || acf.prepare (std::numeric_limits<double>::quiet_NaN(), 2048, 55.0f, 1200.0f))
    {
        std::cerr << "FAIL: ACF accepted an invalid or low-F0-unreachable configuration\n";
        ok = false;
    }

    if (! ok) return 1;
    std::cout << "PASS: YIN frame sizing, 96 kHz low-F0 reachability, invalid-config rejection, and ACF preparation.\n";
    std::cout << "NOTE: this is a preparation/low-frequency smoke test, not the full accuracy/noise/transient benchmark.\n";
    return 0;
}
