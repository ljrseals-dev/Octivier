#include "AcfPitchTracker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

bool AcfPitchTracker::prepare (double sr, int requestedFrameSize,
                               float minF0Hz, float maxF0Hz)
{
    prepared = false;
    sampleRate = sr;
    minF0 = minF0Hz;
    maxF0 = maxF0Hz;
    frameSize = requestedFrameSize;

    if (! std::isfinite (sr) || sr <= 0.0
        || sr > static_cast<double> (std::numeric_limits<int>::max())
        || ! std::isfinite (minF0Hz) || ! std::isfinite (maxF0Hz)
        || minF0Hz <= 0.0f || minF0Hz >= maxF0Hz
        || requestedFrameSize < 4)
        return false;

    minLag = std::max (2, static_cast<int> (sr / maxF0Hz));
    maxLag = std::min (requestedFrameSize / 2,
                       static_cast<int> (sr / minF0Hz));
    if (maxLag <= minLag)
        return false;

    lagScores.assign (static_cast<std::size_t> (maxLag - minLag + 1), 0.0f);
    centeredFrame.assign (static_cast<std::size_t> (requestedFrameSize), 0.0f);
    prepared = true;
    return true;
}

float AcfPitchTracker::estimate (const float* frame, int suppliedFrameSize,
                                 float gateDb, float sensitivity,
                                 float& confidence) noexcept
{
    if (! prepared || frame == nullptr || suppliedFrameSize != frameSize)
        return 0.0f;

    // Preserve the shipped ACF sequence: mean removal, RMS gate, then
    // normalized downsampled correlation over the full lag range.
    double energy = 0.0, mean = 0.0;
    for (int i = 0; i < frameSize; ++i)
        mean += frame[static_cast<std::size_t> (i)];
    mean /= frameSize;

    for (int i = 0; i < frameSize; ++i)
    {
        const float value = frame[static_cast<std::size_t> (i)] - static_cast<float> (mean);
        centeredFrame[static_cast<std::size_t> (i)] = value;
        energy += static_cast<double> (value) * value;
    }
    const float rms = static_cast<float> (std::sqrt (energy / frameSize));
    const float levelDb = 20.0f * std::log10 (std::max (rms, 1.0e-6f));
    if (levelDb < gateDb)
    {
        confidence *= 0.75f;
        return 0.0f;
    }

    const int lagCount = std::max (0, maxLag - minLag + 1);
    if (lagCount <= 0 || lagScores.size() < static_cast<std::size_t> (lagCount))
    {
        confidence *= 0.8f;
        return 0.0f;
    }

    float globalMax = 0.0f;
    for (int k = 0; k < lagCount; ++k)
    {
        const int lag = minLag + k;
        double xy = 0.0, xx = 1.0e-12, yy = 1.0e-12;
        for (int i = 0; i < frameSize - lag; i += 2)
        {
            const double a = centeredFrame[static_cast<std::size_t> (i)];
            const double b = centeredFrame[static_cast<std::size_t> (i + lag)];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }
        const float score = static_cast<float> (xy / std::sqrt (xx * yy));
        lagScores[static_cast<std::size_t> (k)] = score;
        globalMax = std::max (globalMax, score);
    }
    if (globalMax < 0.20f)
    {
        confidence *= 0.8f;
        return 0.0f;
    }

    const float acceptance = std::clamp (0.90f - 0.10f * sensitivity, 0.75f, 0.95f);
    const float threshold = acceptance * globalMax;
    int bestLag = 0;
    for (int k = 0; k < lagCount; ++k)
        if (lagScores[static_cast<std::size_t> (k)] >= threshold)
        {
            bestLag = minLag + k;
            break;
        }
    if (bestLag == 0)
    {
        confidence *= 0.8f;
        return 0.0f;
    }

    const float f0 = static_cast<float> (sampleRate / bestLag);
    confidence = std::clamp ((globalMax - 0.15f) / 0.75f, 0.0f, 1.0f);
    return (f0 >= minF0 && f0 <= maxF0) ? f0 : 0.0f;
}
