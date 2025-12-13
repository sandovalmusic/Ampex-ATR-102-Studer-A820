#include "BiasShielding.h"
#include <algorithm>

namespace TapeMachine
{

// High shelf filter design (2nd order)
// shelfDB > 0 boosts highs, shelfDB < 0 cuts highs
static void designHighShelf(Biquad& filter, double fc, double shelfDB, double Q, double fs)
{
    double A = std::pow(10.0, shelfDB / 40.0);  // sqrt of amplitude
    double w0 = 2.0 * M_PI * fc / fs;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / (2.0 * Q);

    double a0 = (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * std::sqrt(A) * alpha;
    filter.b0 = (A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * std::sqrt(A) * alpha)) / a0;
    filter.b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0)) / a0;
    filter.b2 = (A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * std::sqrt(A) * alpha)) / a0;
    filter.a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosw0)) / a0;
    filter.a2 = ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * std::sqrt(A) * alpha) / a0;
}

// Peaking EQ (bell) filter design
// gainDB > 0 boosts, gainDB < 0 cuts at center frequency
static void designBell(Biquad& filter, double fc, double gainDB, double Q, double fs)
{
    double A = std::pow(10.0, gainDB / 40.0);  // sqrt of amplitude
    double w0 = 2.0 * M_PI * fc / fs;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / (2.0 * Q);

    double a0 = 1.0 + alpha / A;
    filter.b0 = (1.0 + alpha * A) / a0;
    filter.b1 = (-2.0 * cosw0) / a0;
    filter.b2 = (1.0 - alpha * A) / a0;
    filter.a1 = (-2.0 * cosw0) / a0;
    filter.a2 = (1.0 - alpha / A) / a0;
}

HFCut::HFCut()
{
    updateCoefficients();
    reset();
}

void HFCut::setSampleRate(double sampleRate)
{
    fs = sampleRate;
    updateCoefficients();
}

void HFCut::setMachineMode(bool isAmpex)
{
    if (ampexMode != isAmpex)
    {
        ampexMode = isAmpex;
        updateCoefficients();
    }
}

void HFCut::reset()
{
    shelf1.reset();
    shelf2.reset();
    bell.reset();
}

double HFCut::calculateDCGain()
{
    // Calculate DC gain of each biquad: H(z=1) = (b0 + b1 + b2) / (1 + a1 + a2)
    auto biquadDCGain = [](const Biquad& bq) {
        return (bq.b0 + bq.b1 + bq.b2) / (1.0 + bq.a1 + bq.a2);
    };

    // Total DC gain is product of all stages
    double totalGain = biquadDCGain(shelf1)
                     * biquadDCGain(shelf2)
                     * biquadDCGain(bell);

    return totalGain;
}

void HFCut::updateCoefficients()
{
    // New architecture: Shelf1 + Shelf2 + Bell
    // Achieves flat response below 5kHz with smooth HF rolloff
    //
    // Target curves (from TARGETS.md):
    //   AMPEX: 0dB@<5k, -3dB@5k, -6dB@10k, -8dB@15k, -10dB@20k
    //   STUDER: 0dB@<5k, -2dB@5k, -5dB@10k, -7dB@15k, -9dB@20k

    if (ampexMode)
    {
        // AMPEX ATR-102: 432 kHz bias + Quantegy GP9 tape
        // GP9 has higher coercivity (370 Oe vs 320 Oe for 456) requiring ~1 dB more HF cut
        // MORE HF cut (transparent mastering character - HF bypasses saturation)
        //
        // GP9 targets: 0dB@<5k, -4dB@5k, -7dB@10k, -9dB@15k, -11dB@20k
        // (vs 456: 0dB@<5k, -3dB@5k, -6dB@10k, -8dB@15k, -10dB@20k)
        //
        // Optimized parameters for GP9:
        // - Shelf1: 7000 Hz, -7.0 dB, Q=1.00 (was -6.0)
        // - Shelf2: 15000 Hz, -4.0 dB, Q=1.00 (was -3.5)
        // - Bell: 5000 Hz, -3.5 dB, Q=2.0 (was -3.0)
        designHighShelf(shelf1, 7000.0, -7.0, 1.0, fs);
        designHighShelf(shelf2, 15000.0, -4.0, 1.0, fs);
        designBell(bell, 5000.0, -3.5, 2.0, fs);
    }
    else
    {
        // STUDER A820: 153.6 kHz bias + GP9 tape
        // LESS HF cut (warmer multitrack character - more HF into saturation)
        //
        // Optimized parameters (max error 0.21 dB):
        // - Shelf1: 7500 Hz, -6.0 dB, Q=0.80
        // - Shelf2: 16000 Hz, -3.0 dB, Q=1.00
        // - Bell: 6000 Hz, -2.0 dB, Q=2.0
        designHighShelf(shelf1, 7500.0, -6.0, 0.8, fs);
        designHighShelf(shelf2, 16000.0, -3.0, 1.0, fs);
        designBell(bell, 6000.0, -2.0, 2.0, fs);
    }

    // Normalize to 0dB at DC (LF content goes through saturation at natural level)
    double dcGain = calculateDCGain();
    dcNormGain = 1.0 / dcGain;
}

double HFCut::processSample(double input)
{
    double x = shelf1.process(input);
    x = shelf2.process(x);
    x = bell.process(x);

    // Apply DC normalization (0dB at LF)
    return x * dcNormGain;
}

} // namespace TapeMachine
