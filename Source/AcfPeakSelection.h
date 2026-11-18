#pragma once

// Scores must contain one sample before and after the candidate range.
// candidateCount is the number of selectable lags; scores[1..candidateCount]
// are candidates, with scores[0] and scores[candidateCount + 1] as neighbors.
inline int firstLocalPeakAboveThreshold (const float* scores,
                                         int candidateCount,
                                         float threshold) noexcept
{
    if (scores == nullptr || candidateCount <= 0)
        return -1;

    for (int i = 0; i < candidateCount; ++i)
    {
        const float left = scores[i];
        const float centre = scores[i + 1];
        const float right = scores[i + 2];
        if (centre >= left && centre >= right && centre >= threshold)
            return i;
    }
    return -1;
}
