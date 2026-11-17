#pragma once

#include <vector>

// YIN difference-function pitch tracker. prepare() may allocate; estimate()
// performs no allocation and accepts exactly getFrameSize() chronological samples.
class YinPitchTracker final
{
public:
    bool prepare (double sampleRate, float minF0Hz, float maxF0Hz,
                  float periodMultiplier = 2.5f);

    float estimate (const float* frame, int frameSize,
                    float gateDb, float& confidence) noexcept;

    int getFrameSize() const noexcept { return frameSize; }
    int getTauMin() const noexcept { return tauMin; }
    int getTauMax() const noexcept { return tauMax; }
    float getPeriodMultiplier() const noexcept { return periodMultiplier; }
    bool isPrepared() const noexcept { return prepared; }

private:
    static int nextPowerOfTwo (int value) noexcept;

    double sampleRate = 0.0;
    float minF0 = 0.0f, maxF0 = 0.0f, periodMultiplier = 2.5f;
    int frameSize = 0, tauMin = 0, tauMax = 0;
    bool prepared = false;
    std::vector<float> difference, cmnd;
};
