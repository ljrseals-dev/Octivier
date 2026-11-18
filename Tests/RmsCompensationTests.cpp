#include "RmsCompensation.h"

#include <cmath>
#include <iostream>

namespace
{
bool near (float a, float b, float tolerance = 1.0e-4f)
{
    return std::abs (a - b) <= tolerance;
}
}

int main()
{
    bool ok = true;
    RmsCompensation rms;

    // Equal input/output power must remain unity.
    for (int i = 0; i < 20; ++i)
        rms.update (480.0, 480.0, 480, 1, 48000.0, true);
    if (! near (rms.gain(), 1.0f))
    {
        std::cerr << "FAIL: unity transfer changed gain\n";
        ok = false;
    }

    // A 20 dB reduction requests +20 dB, but compensation must stop at +12 dB.
    rms.reset();
    for (int i = 0; i < 150; ++i)
    {
        rms.update (480.0, 0.048, 480, 1, 48000.0, true);
        if (rms.gain() > 3.98107171f || rms.gain() < 1.0f)
        {
            std::cerr << "FAIL: compensation escaped the +/-12 dB bound\n";
            ok = false;
            break;
        }
    }
    if (rms.gain() < 3.90f)
    {
        std::cerr << "FAIL: compensation did not approach the +12 dB ceiling\n";
        ok = false;
    }

    // Disabling compensation must smoothly return the gain toward unity.
    const float beforeDisable = rms.gain();
    for (int i = 0; i < 40; ++i)
        rms.update (480.0, 480.0, 480, 1, 48000.0, false);
    if (!(rms.gain() < beforeDisable && rms.gain() > 1.0f))
    {
        std::cerr << "FAIL: disabling did not return gain smoothly toward unity\n";
        ok = false;
    }

    // Silence and invalid blocks must not poison the state with NaN/Inf.
    rms.reset();
    for (int i = 0; i < 20; ++i)
        rms.update (0.0, 0.0, 480, 2, 48000.0, true);
    const float beforeInvalid = rms.gain();
    rms.update (1.0, 1.0, 0, 2, 48000.0, true);
    rms.update (1.0, 1.0, 480, 2, 0.0, true);
    if (! std::isfinite (rms.gain()) || ! near (rms.gain(), beforeInvalid))
    {
        std::cerr << "FAIL: silence/invalid block corrupted RMS state\n";
        ok = false;
    }

    // Stereo normalization uses the total sample count, not one channel's count.
    rms.reset();
    for (int i = 0; i < 40; ++i)
        rms.update (960.0, 960.0, 480, 2, 48000.0, true);
    if (! near (rms.gain(), 1.0f))
    {
        std::cerr << "FAIL: stereo power normalization is incorrect\n";
        ok = false;
    }

    if (! ok) return 1;
    std::cout << "RMS compensation tests passed\n";
    return 0;
}
