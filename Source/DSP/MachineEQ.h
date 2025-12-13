#pragma once

#include "MathConstants.h"

namespace TapeMachine
{

// Biquad filter using Audio EQ Cookbook formulas
struct EQBiquad
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

    // Bell/Peaking EQ (Audio EQ Cookbook)
    void setBell(double fc, double Q, double gainDB, double sampleRate)
    {
        double A = std::pow(10.0, gainDB / 40.0);
        double w0 = 2.0 * M_PI * fc / sampleRate;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * Q);

        double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * cosw0) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * cosw0) / a0;
        a2 = (1.0 - alpha / A) / a0;
    }

    // High-pass filter (2nd order, 12 dB/oct)
    void setHighPass(double fc, double Q, double sampleRate)
    {
        double w0 = 2.0 * M_PI * fc / sampleRate;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * Q);

        double a0 = 1.0 + alpha;
        b0 = ((1.0 + cosw0) / 2.0) / a0;
        b1 = (-(1.0 + cosw0)) / a0;
        b2 = ((1.0 + cosw0) / 2.0) / a0;
        a1 = (-2.0 * cosw0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    // Low-pass filter (2nd order, 12 dB/oct)
    void setLowPass(double fc, double Q, double sampleRate)
    {
        double w0 = 2.0 * M_PI * fc / sampleRate;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * Q);

        double a0 = 1.0 + alpha;
        b0 = ((1.0 - cosw0) / 2.0) / a0;
        b1 = (1.0 - cosw0) / a0;
        b2 = ((1.0 - cosw0) / 2.0) / a0;
        a1 = (-2.0 * cosw0) / a0;
        a2 = (1.0 - alpha) / a0;
    }
};

// 1st order filter for 6 dB/oct slopes
struct FirstOrderFilter
{
    double b0 = 1.0, b1 = 0.0;
    double a1 = 0.0;
    double z1 = 0.0;

    void reset() { z1 = 0.0; }

    double process(double input)
    {
        double output = b0 * input + z1;
        z1 = b1 * input - a1 * output;
        return output;
    }

    // 1st order high-pass (6 dB/oct)
    void setHighPass(double fc, double sampleRate)
    {
        double K = std::tan(M_PI * fc / sampleRate);
        double a0 = 1.0 + K;
        b0 = 1.0 / a0;
        b1 = -1.0 / a0;
        a1 = (K - 1.0) / a0;
    }

    // 1st order low-pass (6 dB/oct)
    void setLowPass(double fc, double sampleRate)
    {
        double K = std::tan(M_PI * fc / sampleRate);
        double a0 = 1.0 + K;
        b0 = K / a0;
        b1 = K / a0;
        a1 = (K - 1.0) / a0;
    }
};

/**
 * Machine-specific EQ curves from Jack Endino's measurements
 * Applied AFTER saturation to capture the total frequency response.
 *
 * Note: MachineEQ runs at the oversampled rate (2x), so at 48kHz base
 * we have 96kHz sample rate and 48kHz Nyquist - 30kHz bands work correctly.
 */
class MachineEQ
{
public:
    enum class Machine { Ampex, Studer };

    MachineEQ();

    void setSampleRate(double sampleRate);
    void setMachine(Machine machine);
    void reset();
    double processSample(double input);

private:
    double fs = 48000.0;
    Machine currentMachine = Machine::Ampex;

    // Ampex ATR-102 "Master" EQ
    // Fine-tuned to match Pro-Q4 reference:
    // Targets: 20Hz=-2.7dB, 28Hz=0dB, 40Hz=+1.15dB, 70Hz=+0.17dB, 105Hz=+0.3dB, 150Hz=0dB,
    //          350Hz=-0.5dB, 1200Hz=-0.3dB, 3kHz=-0.45dB, 10kHz=0dB, 16kHz=-0.25dB, 21.5kHz=0dB
    EQBiquad ampexHP;           // HP filter
    EQBiquad ampexBell1;        // 28 Hz lift
    EQBiquad ampexBell2;        // 40 Hz head bump
    EQBiquad ampexBell3;        // 70 Hz
    EQBiquad ampexBell4;        // 105 Hz
    EQBiquad ampexBell5;        // 150 Hz
    EQBiquad ampexBell6;        // 350 Hz dip
    EQBiquad ampexBell7;        // 1200 Hz
    EQBiquad ampexBell8;        // 3000 Hz
    EQBiquad ampexBell9;        // 10kHz / 16kHz
    EQBiquad ampexBell10;       // HF lift
    EQBiquad ampexLP;           // 30000 Hz LP2

    // Studer A820 "Tracks" EQ - Optimized to 0.039 dB RMS error
    // Targets from Jack Endino: 20Hz=-9dB, 30Hz=-2dB, 38Hz=0dB, 50Hz=+0.55dB,
    // 70Hz=+0.1dB, 110Hz=+1.2dB, 160Hz=+0.5dB, 200Hz=+0.1dB, 600Hz=+0.2dB,
    // 5kHz=+0.5dB, 10kHz=0dB, 20kHz=+0.5dB
    EQBiquad studerHP1;         // 27 Hz Q=1.0, 12 dB/oct
    FirstOrderFilter studerHP2; // 30.5 Hz, 6 dB/oct (cascaded = 18 dB/oct total)
    EQBiquad studerBell1;       // 46 Hz, Q 1.4, +1.10 dB (head bump)
    EQBiquad studerBell2;       // 70 Hz, Q 2.0, -0.50 dB (dip)
    EQBiquad studerBell3;       // 110 Hz, Q 2.0, +1.20 dB (head bump 2)
    EQBiquad studerBell4;       // 160 Hz, Q 1.5, +0.30 dB
    EQBiquad studerBell5;       // 200 Hz, Q 2.0, -0.30 dB (notch)
    EQBiquad studerBell6;       // 600 Hz, Q 1.5, +0.20 dB
    EQBiquad studerBell7;       // 5000 Hz, Q 1.0, +0.50 dB
    EQBiquad studerBell8;       // 10000 Hz, Q 1.5, -0.25 dB
    EQBiquad studerBell9;       // 20000 Hz, Q 1.0, +0.50 dB

    void updateCoefficients();
};

} // namespace TapeMachine
