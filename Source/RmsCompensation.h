#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

// Pre-output-trim RMS matcher. All state is fixed-size and caller-thread-owned.
// update() is intended for one call per audio block; gain() is read before the
// block is processed, so gain changes never step partway through a block.
class RmsCompensation final
{
public:
    void reset() noexcept
    {
        inputPower = 0.0;
        outputPower = 0.0;
        compensationGain = 1.0f;
    }

    float gain() const noexcept { return compensationGain; }

    void update (double inputSumSquares, double outputSumSquares,
                 int samplesPerChannel, int channels,
                 double sampleRate, bool enabled) noexcept
    {
        if (samplesPerChannel <= 0 || channels <= 0
            || ! std::isfinite (sampleRate) || sampleRate <= 0.0
            || ! std::isfinite (inputSumSquares) || ! std::isfinite (outputSumSquares)
            || inputSumSquares < 0.0 || outputSumSquares < 0.0)
            return;

        const double count = static_cast<double> (samplesPerChannel)
                           * static_cast<double> (channels);
        const double inputBlockPower = inputSumSquares / count;
        const double outputBlockPower = outputSumSquares / count;
        const double alpha = 1.0 - std::exp (
            -static_cast<double> (samplesPerChannel) / (sampleRate * 0.300));

        inputPower = (1.0 - alpha) * inputPower + alpha * inputBlockPower;
        outputPower = (1.0 - alpha) * outputPower + alpha * outputBlockPower;

        float target = 1.0f;
        if (enabled && inputPower > 1.0e-10 && outputPower > 1.0e-10)
        {
            constexpr float minGain = 0.25118864f; // -12 dB
            constexpr float maxGain = 3.98107171f; // +12 dB
            target = std::clamp (
                static_cast<float> (std::sqrt (inputPower / outputPower)),
                minGain, maxGain);
        }
        compensationGain += static_cast<float> (alpha)
                          * (target - compensationGain);
    }

private:
    double inputPower = 0.0;
    double outputPower = 0.0;
    float compensationGain = 1.0f;
};
