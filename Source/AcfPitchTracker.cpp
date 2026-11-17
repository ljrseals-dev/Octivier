#include "AcfPitchTracker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

bool AcfPitchTracker::prepare (double sr, int requestedFrameSize,
                               float minF0Hz, float maxF0Hz)
{
    prepared = false;
    sampleRate = 0.0;
    minF0 = maxF0 = 0.0f;
    frameSize = minLag = maxLag = 0;

    if (! std::isfinite (sr) || sr <= 0.0
        || sr > static_cast<double> (std::numeric_limits<int>::max())
        || ! std::isfinite (minF0Hz) || ! std::isfinite (maxF0Hz)
        || minF0Hz <= 0.0f || minF0Hz >= maxF0Hz
        || static_cast<double> (maxF0Hz) >= sr * 0.5
        || requestedFrameSize < 4)
        return false;

    const double minPeriod = sr / static_cast<double> (minF0Hz);
    const double maxPeriod = sr / static_cast<double> (maxF0Hz);
    if (! std::isfinite (minPeriod) || ! std::isfinite (maxPeriod)
        || minPeriod > static_cast<double> (std::numeric_limits<int>::max())
        || maxPeriod > static_cast<double> (std::numeric_limits<int>::max()))
        return false;

    // Preserve the original integer lag bounds, but never claim a prepared
    // configuration if half the frame cannot reach the requested low F0.
    const int requiredMaxLag = static_cast<int> (std::ceil (minPeriod));
    if (requestedFrameSize / 2 < requiredMaxLag)
        return false;

    const int candidateMinLag = std::max (2, static_cast<int> (maxPeriod));
    const int candidateMaxLag = std::min (requestedFrameSize / 2,
                                          static_cast<int> (minPeriod));
    if (candidateMaxLag <= candidateMinLag)
        return false;

    sampleRate = sr;
    minF0 = minF0Hz;
    maxF0 = maxF0Hz;
    frameSize = requestedFrameSize;
    minLag = candidateMinLag;
    maxLag = candidateMaxLag;
    lagScores.assign (static_cast<std::size_t> (maxLag - minLag + 1), 0.0f);
    centeredFrame.assign (static_cast<std::size_t> (requestedFrameSize), 0.0f);
    prepared = true;
    return true;
}

float AcfPitchTracker::estimate (const float* frame, int suppliedFrameSize,
                                 float gateDb, float sensitivity,
                                 float& confidence, PeakSelection selection) noexcept
{
    if (! prepared || frame == nullptr || suppliedFrameSize != frameSize
        || ! std::isfinite (gateDb) || ! std::isfinite (sensitivity))
    {
        confidence = 0.0f;
        return 0.0f;
    }
    if (! std::isfinite (confidence))
        confidence = 0.0f;

    double energy = 0.0, mean = 0.0;
    for (int i = 0; i < frameSize; ++i)
    {
        const float sample = frame[static_cast<std::size_t> (i)];
        if (! std::isfinite (sample))
        {
            confidence = 0.0f;
            return 0.0f;
        }
        mean += sample;
    }
    mean /= frameSize;

    for (int i = 0; i < frameSize; ++i)
    {
        const float value = frame[static_cast<std::size_t> (i)] - static_cast<float> (mean);
        centeredFrame[static_cast<std::size_t> (i)] = value;
        energy += static_cast<double> (value) * value;
    }
    const float rms = static_cast<float> (std::sqrt (energy / frameSize));
    const float levelDb = 20.0f * std::log10 (std::max (rms, 1.0e-6f));
    if (! std::isfinite (levelDb) || levelDb < gateDb)
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
    if (! std::isfinite (globalMax) || globalMax < 0.20f)
    {
        confidence *= 0.8f;
        return 0.0f;
    }

    const float acceptance = std::clamp (0.90f - 0.10f * sensitivity, 0.75f, 0.95f);
    const float threshold = acceptance * globalMax;
    int bestIndex = -1;
    int bestLag = 0;
    float bestScore = -std::numeric_limits<float>::infinity();

    if (selection == PeakSelection::LegacyShortestAboveThreshold)
    {
        // Exact pre-fix selection path retained for the extraction equivalence
        // test; the plugin and benchmark use strongest-local-peak selection.
        for (int k = 0; k < lagCount; ++k)
            if (lagScores[static_cast<std::size_t> (k)] >= threshold)
            {
                bestIndex = k;
                bestLag = minLag + k;
                bestScore = lagScores[static_cast<std::size_t> (k)];
                break;
            }
    }
    else
    {
        for (int k = 0; k < lagCount; ++k)
        {
            const float score = lagScores[static_cast<std::size_t> (k)];
            const float left = k > 0 ? lagScores[static_cast<std::size_t> (k - 1)]
                                     : -std::numeric_limits<float>::infinity();
            const float right = k + 1 < lagCount
                ? lagScores[static_cast<std::size_t> (k + 1)]
                : -std::numeric_limits<float>::infinity();
            const bool localMaximum = score >= left && score >= right;
            if (! localMaximum || score < threshold)
                continue;

            // A shorter lag is only a tie-breaker, not the selection rule.
            if (score > bestScore + 1.0e-6f)
            {
                bestIndex = k;
                bestLag = minLag + k;
                bestScore = score;
            }
        }
    }

    if (bestIndex < 0)
    {
        confidence *= 0.8f;
        return 0.0f;
    }

    double refinedLag = static_cast<double> (bestLag);
    if (selection == PeakSelection::StrongestLocalPeak
        && bestIndex > 0 && bestIndex + 1 < lagCount)
    {
        const double left = lagScores[static_cast<std::size_t> (bestIndex - 1)];
        const double centre = lagScores[static_cast<std::size_t> (bestIndex)];
        const double right = lagScores[static_cast<std::size_t> (bestIndex + 1)];
        const double denominator = left - 2.0 * centre + right;
        if (std::abs (denominator) > 1.0e-12)
            refinedLag += std::clamp (0.5 * (left - right) / denominator, -0.5, 0.5);
    }

    const float f0 = static_cast<float> (sampleRate / refinedLag);
    confidence = std::clamp (((selection == PeakSelection::LegacyShortestAboveThreshold
                                  ? globalMax : bestScore) - 0.15f) / 0.75f,
                             0.0f, 1.0f);
    return (std::isfinite (f0) && f0 >= minF0 && f0 <= maxF0) ? f0 : 0.0f;
}
