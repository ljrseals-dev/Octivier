#include "AcfPeakSelection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;
constexpr int kFrameSize = 2048;
constexpr float kMinF0 = 55.0f;
constexpr float kMaxF0 = 1200.0f;

bool checkSine (double frequency, double phase)
{
    std::array<float, kFrameSize> frame {};
    double mean = 0.0;
    for (int i = 0; i < kFrameSize; ++i)
    {
        frame[static_cast<std::size_t> (i)] = static_cast<float> (
            std::sin (2.0 * kPi * frequency * i / kSampleRate + phase));
        mean += frame[static_cast<std::size_t> (i)];
    }
    mean /= kFrameSize;

    double energy = 0.0;
    for (auto& sample : frame)
    {
        sample -= static_cast<float> (mean);
        energy += static_cast<double> (sample) * sample;
    }
    if (energy <= 1.0e-12)
        return false;

    const int minLag = std::max (2, static_cast<int> (kSampleRate / kMaxF0));
    const int maxLag = std::min (kFrameSize / 2,
                                 static_cast<int> (std::ceil (kSampleRate / kMinF0)));
    const int candidateCount = maxLag - minLag + 1;
    std::vector<float> scores (static_cast<std::size_t> (candidateCount + 2), 0.0f);
    float globalMax = 0.0f;

    for (int offset = 0; offset < candidateCount + 2; ++offset)
    {
        const int lag = minLag - 1 + offset;
        double xy = 0.0, xx = 1.0e-12, yy = 1.0e-12;
        for (int i = 0; i < kFrameSize - lag; i += 2)
        {
            const double a = frame[static_cast<std::size_t> (i)];
            const double b = frame[static_cast<std::size_t> (i + lag)];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }
        scores[static_cast<std::size_t> (offset)] =
            static_cast<float> (xy / std::sqrt (xx * yy));
        if (offset > 0 && offset <= candidateCount)
            globalMax = std::max (globalMax, scores[static_cast<std::size_t> (offset)]);
    }

    const float threshold = 0.85f * globalMax;
    const int selected = firstLocalPeakAboveThreshold (scores.data(), candidateCount, threshold);
    if (selected < 0)
    {
        std::cerr << "No local peak for " << frequency << " Hz, phase " << phase << '\\n';
        return false;
    }

    const double left = scores[static_cast<std::size_t> (selected)];
    const double centre = scores[static_cast<std::size_t> (selected + 1)];
    const double right = scores[static_cast<std::size_t> (selected + 2)];
    const double denominator = left - 2.0 * centre + right;
    double refinedLag = minLag + selected;
    if (std::abs (denominator) > 1.0e-12)
        refinedLag += std::clamp (0.5 * (left - right) / denominator, -0.5, 0.5);

    const double detected = kSampleRate / refinedLag;
    const double cents = std::abs (1200.0 * std::log2 (detected / frequency));

    // Document the old behavior: the first above-threshold lag is the
    // short-lag shoulder, not the low-frequency sine's periodicity peak.
    int legacy = -1;
    for (int i = 0; i < candidateCount; ++i)
        if (scores[static_cast<std::size_t> (i + 1)] >= threshold)
        {
            legacy = i;
            break;
        }

    if (cents > 35.0)
    {
        std::cerr << "Wrong ACF sine selection: target=" << frequency
                  << " detected=" << detected << " cents=" << cents
                  << " legacyLag=" << (legacy < 0 ? 0 : minLag + legacy)
                  << " selectedLag=" << refinedLag << '\\n';
        return false;
    }
    return true;
}
}

int main()
{
    constexpr std::array<double, 12> frequencies {
        55.0, 58.2705, 61.7354, 65.4064, 73.4162, 82.4069,
        87.3071, 98.0, 110.0, 130.8128, 164.8138, 220.0
    };
    constexpr std::array<double, 5> phases { 0.0, 0.37, 0.91, 1.73, 2.61 };

    bool ok = true;
    for (const auto frequency : frequencies)
        for (const auto phase : phases)
            ok = checkSine (frequency, phase) && ok;

    if (! ok)
        return 1;

    std::cout << "PASS: low-frequency sine selection at 48 kHz / 2048 samples "
                 "across 12 fundamentals and 5 phases.\\n";
    return 0;
}
