#include "YinPitchTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

int YinPitchTracker::nextPowerOfTwo (int value) noexcept
{
    if (value <= 1) return 1;
    int result = 1;
    while (result < value && result <= (std::numeric_limits<int>::max() / 2))
        result *= 2;
    return result >= value ? result : 0;
}

bool YinPitchTracker::prepare (double sr, float minF0Hz, float maxF0Hz,
                               float multiplier)
{
    prepared = false;
    sampleRate = sr;
    minF0 = minF0Hz;
    maxF0 = maxF0Hz;
    periodMultiplier = multiplier;
    frameSize = tauMin = tauMax = 0;
    difference.clear();
    cmnd.clear();

    if (! std::isfinite (sr) || sr <= 0.0
        || ! std::isfinite (minF0Hz) || ! std::isfinite (maxF0Hz)
        || ! std::isfinite (multiplier)
        || minF0Hz <= 0.0f || maxF0Hz <= minF0Hz
        || multiplier <= 0.0f)
        return false;

    const double required = std::ceil (static_cast<double> (multiplier)
                                       * sr / static_cast<double> (minF0Hz));
    if (! std::isfinite (required) || required < 4.0
        || required > static_cast<double> (std::numeric_limits<int>::max()))
        return false;

    const int size = nextPowerOfTwo (static_cast<int> (required));
    if (size < 4) return false;

    const double minPeriod = std::floor (sr / static_cast<double> (maxF0Hz));
    const double maxPeriod = std::ceil (sr / static_cast<double> (minF0Hz));
    if (! std::isfinite (minPeriod) || ! std::isfinite (maxPeriod)
        || minPeriod > static_cast<double> (std::numeric_limits<int>::max())
        || maxPeriod > static_cast<double> (std::numeric_limits<int>::max()))
        return false;

    const int low = std::max (2, static_cast<int> (minPeriod));
    const int high = std::min (size / 2, static_cast<int> (maxPeriod));
    // A valid tracker must actually cover the requested minimum frequency.
    if (high <= low || high < static_cast<int> (maxPeriod))
        return false;

    frameSize = size;
    tauMin = low;
    tauMax = high;
    difference.assign (static_cast<std::size_t> (tauMax) + 1, 0.0f);
    cmnd.assign (static_cast<std::size_t> (tauMax) + 1, 1.0f);
    prepared = true;
    return true;
}

float YinPitchTracker::estimate (const float* frame, int suppliedFrameSize,
                                 float gateDb, float& confidence) noexcept
{
    if (! prepared || frame == nullptr || suppliedFrameSize != frameSize)
    {
        confidence = 0.0f;
        return 0.0f;
    }

    double energy = 0.0, mean = 0.0;
    for (int i = 0; i < frameSize; ++i)
        mean += frame[static_cast<std::size_t> (i)];
    mean /= frameSize;

    for (int i = 0; i < frameSize; ++i)
    {
        const double value = static_cast<double> (frame[static_cast<std::size_t> (i)]) - mean;
        energy += value * value;
    }
    const float rms = static_cast<float> (std::sqrt (energy / frameSize));
    const float levelDb = 20.0f * std::log10 (std::max (rms, 1.0e-6f));
    if (levelDb < gateDb)
    {
        confidence *= 0.75f;
        return 0.0f;
    }

    difference[0] = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < frameSize - tau; ++i)
        {
            const double a = static_cast<double> (frame[static_cast<std::size_t> (i)]) - mean;
            const double b = static_cast<double> (frame[static_cast<std::size_t> (i + tau)]) - mean;
            const double delta = a - b;
            sum += delta * delta;
        }
        difference[static_cast<std::size_t> (tau)] = static_cast<float> (sum);
    }

    cmnd[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += difference[static_cast<std::size_t> (tau)];
        cmnd[static_cast<std::size_t> (tau)] =
            running > 0.0 ? static_cast<float> (difference[static_cast<std::size_t> (tau)]
                                                * tau / running) : 1.0f;
    }

    constexpr float threshold = 0.15f;
    int selected = 0;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cmnd[static_cast<std::size_t> (tau)] < threshold)
        {
            while (tau < tauMax
                   && cmnd[static_cast<std::size_t> (tau + 1)]
                        < cmnd[static_cast<std::size_t> (tau)])
                ++tau;
            selected = tau;
            break;
        }
    }
    if (selected == 0)
    {
        float best = threshold;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            const float value = cmnd[static_cast<std::size_t> (tau)];
            if (value < best) { best = value; selected = tau; }
        }
    }
    if (selected == 0)
    {
        confidence *= 0.8f;
        return 0.0f;
    }

    double refined = selected;
    if (selected > tauMin && selected < tauMax)
    {
        const double left = cmnd[static_cast<std::size_t> (selected - 1)];
        const double centre = cmnd[static_cast<std::size_t> (selected)];
        const double right = cmnd[static_cast<std::size_t> (selected + 1)];
        const double denominator = left - 2.0 * centre + right;
        if (std::abs (denominator) > 1.0e-12)
            refined += 0.5 * (left - right) / denominator;
    }

    const float hz = static_cast<float> (sampleRate / refined);
    confidence = std::clamp (1.0f - cmnd[static_cast<std::size_t> (selected)], 0.0f, 1.0f);
    return (hz >= minF0 && hz <= maxF0) ? hz : 0.0f;
}
