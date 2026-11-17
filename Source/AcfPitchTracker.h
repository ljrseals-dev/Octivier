#pragma once

#include <vector>

// Allocation-free-per-call autocorrelation pitch tracker. prepare() is a
// non-real-time operation; estimate() consumes one chronological linear frame.
class AcfPitchTracker final
{
public:
    bool prepare (double sampleRate, int frameSize,
                  float minF0Hz = 55.0f, float maxF0Hz = 1200.0f);

    float estimate (const float* frame, int frameSize,
                    float gateDb, float sensitivity,
                    float& confidence) noexcept;

    int getFrameSize() const noexcept { return frameSize; }
    bool isPrepared() const noexcept { return prepared; }

private:
    double sampleRate = 0.0;
    float minF0 = 0.0f, maxF0 = 0.0f;
    int frameSize = 0, minLag = 0, maxLag = 0;
    bool prepared = false;
    std::vector<float> lagScores, centeredFrame;
};
