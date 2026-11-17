#include "AcfPitchTracker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr int frameSize = 2048;
constexpr float minF0 = 55.0f, maxF0 = 1200.0f;

float legacyEstimate (const float* input, double sampleRate,
                      float gateDb, float sensitivity, float& confidence)
{
    std::array<float, frameSize> x {};
    double energy = 0.0, mean = 0.0;
    for (int i = 0; i < frameSize; ++i)
    {
        x[static_cast<std::size_t> (i)] = input[static_cast<std::size_t> (i)];
        mean += x[static_cast<std::size_t> (i)];
    }
    mean /= frameSize;
    for (auto& value : x)
    {
        value -= static_cast<float> (mean);
        energy += value * value;
    }
    const float rms = static_cast<float> (std::sqrt (energy / frameSize));
    const float levelDb = 20.0f * std::log10 (std::max (rms, 1.0e-6f));
    if (levelDb < gateDb) { confidence *= 0.75f; return 0.0f; }

    const int minLag = std::max (2, static_cast<int> (sampleRate / maxF0));
    const int maxLag = std::min (frameSize / 2, static_cast<int> (sampleRate / minF0));
    const int lagCount = std::max (0, maxLag - minLag + 1);
    if (lagCount <= 0) { confidence *= 0.8f; return 0.0f; }

    std::array<float, frameSize> lagScores {};
    float globalMax = 0.0f;
    for (int k = 0; k < lagCount; ++k)
    {
        const int lag = minLag + k;
        double xy = 0.0, xx = 1.0e-12, yy = 1.0e-12;
        for (int i = 0; i < frameSize - lag; i += 2)
        {
            const double a = x[static_cast<std::size_t> (i)];
            const double b = x[static_cast<std::size_t> (i + lag)];
            xy += a * b; xx += a * a; yy += b * b;
        }
        const float score = static_cast<float> (xy / std::sqrt (xx * yy));
        lagScores[static_cast<std::size_t> (k)] = score;
        globalMax = std::max (globalMax, score);
    }
    if (globalMax < 0.20f) { confidence *= 0.8f; return 0.0f; }

    const float acceptance = std::clamp (0.90f - 0.10f * sensitivity, 0.75f, 0.95f);
    const float threshold = acceptance * globalMax;
    int bestLag = 0;
    for (int k = 0; k < lagCount; ++k)
        if (lagScores[static_cast<std::size_t> (k)] >= threshold)
        { bestLag = minLag + k; break; }
    if (bestLag == 0) { confidence *= 0.8f; return 0.0f; }

    const float f0 = static_cast<float> (sampleRate / bestLag);
    confidence = std::clamp ((globalMax - 0.15f) / 0.75f, 0.0f, 1.0f);
    return (f0 >= minF0 && f0 <= maxF0) ? f0 : 0.0f;
}
}

int main (int argc, char** argv)
{
    const bool dumpCsv = argc > 1 && std::string (argv[1]) == "--csv";
    if (dumpCsv) std::cout << "frame_index,legacy_hz,legacy_confidence,extracted_hz,extracted_confidence\n";
    constexpr double sampleRate = 48000.0;
    constexpr int totalSamples = 10 * 48000;
    constexpr int hop = 480; // 10 ms; first half sweep, second half silence.
    std::vector<float> input (totalSamples, 0.0f);
    double phase = 0.0;
    for (int i = 0; i < totalSamples / 2; ++i)
    {
        const double progress = static_cast<double> (i) / (totalSamples / 2 - 1);
        const double hz = minF0 + (maxF0 - minF0) * progress;
        phase += 2.0 * 3.14159265358979323846 * hz / sampleRate;
        input[static_cast<std::size_t> (i)] = 0.5f * static_cast<float> (std::sin (phase));
    }

    AcfPitchTracker extracted;
    if (! extracted.prepare (sampleRate, frameSize, minF0, maxF0))
    {
        std::cerr << "FAIL: extracted ACF prepare\n";
        return 1;
    }

    float legacyConfidence = 0.0f, extractedConfidence = 0.0f;
    std::array<float, frameSize> frame {};
    std::uint64_t compared = 0;
    for (int end = hop; end <= totalSamples; end += hop)
    {
        const int start = end - frameSize;
        for (int i = 0; i < frameSize; ++i)
        {
            const int source = start + i;
            frame[static_cast<std::size_t> (i)] =
                source >= 0 ? input[static_cast<std::size_t> (source)] : 0.0f;
        }
        const float legacyHz = legacyEstimate (frame.data(), sampleRate, -55.0f,
                                                0.62f, legacyConfidence);
        const float extractedHz = extracted.estimate (frame.data(), frameSize, -55.0f,
                                                        0.62f, extractedConfidence);
        if (dumpCsv)
            std::cout << compared << ',' << legacyHz << ',' << legacyConfidence << ','
                      << extractedHz << ',' << extractedConfidence << '\n';
        if (legacyHz != extractedHz || legacyConfidence != extractedConfidence)
        {
            std::cerr << "FAIL: ACF extraction differs at frame " << compared
                      << " legacy=" << legacyHz << " extracted=" << extractedHz
                      << " confidence=" << legacyConfidence << '/' << extractedConfidence << '\n';
            return 1;
        }
        ++compared;
    }

    auto& summary = dumpCsv ? std::cerr : std::cout;
    summary << "PASS: " << compared
            << " same-build ACF frames bit-identical across 5 s sweep + 5 s silence.\n";
    summary << "NOTE: this compares the extracted implementation with a frozen in-test copy; "
               "it is not a cross-compiler/architecture guarantee.\n";
    return 0;
}
