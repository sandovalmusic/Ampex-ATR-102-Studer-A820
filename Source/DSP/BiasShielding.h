#pragma once

#include "MathConstants.h"

namespace TapeMachine
{

// Biquad filter (Direct Form II Transposed)
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }

    double process(double input)
    {
        double output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }
};

// HFCut - Cut HF before saturation (models AC bias shielding)
//
// Models the frequency-dependent effectiveness of AC bias at linearizing
// the magnetic recording process. Used with parallel clean HF path:
//   cleanHF = input - HFCut(input)
//   output = saturate(HFCut(input)) + HFBoost(cleanHF)
//
// Uses high shelf + bell architecture for flat response below 5kHz
// with smooth rolloff above. Physics-based targets from McKnight/Bertram.
//
// BIAS FREQUENCIES & TAPE:
//   Ampex ATR-102: 432 kHz bias + GP9 tape @ +9/500nWb
//   Studer A820:   153.6 kHz bias + GP9 tape @ +9/500nWb
//
// Target curves:
//   ATR-102 (GP9): 0dB@<5k, -4dB@5k, -7dB@10k, -9dB@15k, -11dB@20k
//   A820:          0dB@<5k, -2dB@5k, -5dB@10k, -7dB@15k, -9dB@20k
class HFCut
{
public:
    HFCut();
    void setSampleRate(double sampleRate);
    void setMachineMode(bool isAmpex);
    void setMachineAndTape(bool isAmpex, bool isSM900);
    void reset();
    double processSample(double input);

private:
    double fs = 48000.0;
    bool ampexMode = true;
    bool sm900Mode = false;

    // High shelf filters for main HF rolloff
    Biquad shelf1;  // Primary shelf (starts around 7kHz)
    Biquad shelf2;  // Secondary shelf (fine-tune 15-20kHz)

    // Bell filter to shape the knee at 5-6kHz
    Biquad bell;

    // DC gain normalization (ensures 0dB at LF)
    double dcNormGain = 1.0;

    void updateCoefficients();
    double calculateDCGain();
};

} // namespace TapeMachine
