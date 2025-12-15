#pragma once

#include "MathConstants.h"
#include "BiasShielding.h"
#include "JilesAthertonCore.h"
#include "MachineEQ.h"

namespace TapeMachine
{

/**
 * Tape Formula enumeration
 * GP9: Quantegy GP9 - Clean but "boring", higher MOL, steeper distortion curve
 * SM900: Emtec SM900 - Warmer, lower MOL, more gradual/linear distortion, bottom-end enhancement
 */
enum class TapeFormula
{
    GP9 = 0,
    SM900 = 1
};

/**
 * Hybrid Tape Saturation Processor
 *
 * Architecture:
 *   1. AC Bias Shielding (HFCut) - Splits HF for clean bypass
 *   2. J-A Hysteresis - Realistic magnetic feel (DAFx paper params, c=0.98)
 *   3. Level-Scaled Cubic Saturation - THD curve with DC bias for E/O control
 *   4. Clean HF Path - Recombines with saturated signal (sums to unity)
 *
 * Level-Scaled Cubic: effectiveA3 = a3Base * level^power
 *   - Gives THD slope of (2 + power) on log-log scale
 *   - Steeper than pure cubic, matching real tape behavior
 *
 * MASTER MODE (Ampex ATR-102):
 *   - E/O ratio ~0.48 at 0dB (target 0.50, odd-dominant)
 *   - Saturation: a3=0.0032, bias=0.08, power=0.33, lowLevelScale=0.35
 *
 * TRACKS MODE (Studer A820):
 *   - E/O ratio ~1.14 at 0dB (target 1.12, even-dominant)
 *   - Saturation: a3=0.0066, bias=0.19, power=0.50, lowLevelScale=0.80
 */
class HybridTapeProcessor
{
public:
    HybridTapeProcessor();
    ~HybridTapeProcessor() = default;

    void setSampleRate(double sampleRate);
    void reset();

    /**
     * @param biasStrength - < 0.74 = Master (Ampex), >= 0.74 = Tracks (Studer)
     * @param inputGain - Input gain scaling
     * @param tapeFormula - 0 = GP9, 1 = SM900
     */
    void setParameters(double biasStrength, double inputGain, int tapeFormula = 0);

    /**
     * Direct parameter override for testing/calibration
     * Call AFTER setParameters() to override specific values
     */
    void setTestParameters(double testSatA3, double testSatPower,
                           double testLowLevelScale, double testJaBlend);

    double processSample(double input);
    double processRightChannel(double input);  // With azimuth delay

private:
    // Azimuth delay buffer (supports up to 384kHz)
    static constexpr int DELAY_BUFFER_SIZE = 8;
    double delayBuffer[DELAY_BUFFER_SIZE] = {0.0};
    int delayWriteIndex = 0;
    double cachedDelaySamples = 0.0;
    double allpassState = 0.0;  // Thiran allpass filter state for fractional delay

    // Parameters
    double currentBiasStrength = 0.5;
    double currentInputGain = 1.0;
    bool isAmpexMode = true;
    TapeFormula currentTapeFormula = TapeFormula::GP9;
    double fs = 48000.0;

    // Global input bias for E/O ratio (even harmonics)
    double inputBias = 0.0;

    // Level-scaled cubic saturation parameters
    double satA3 = 0.0028;   // Base cubic coefficient
    double satPower = 0.5;   // Level scaling exponent (THD slope = 2 + power)
    double satEnvelope = 0.0; // Saturation envelope follower
    double lowLevelScale = 0.5;  // Min a3 scale at very low levels (machine-specific)

    // J-A hysteresis blend (machine-specific based on AC bias frequency)
    // Higher bias freq = more linearization = less hysteresis character
    double jaBlend = 0.10;   // Ampex: 0.06 (432kHz bias), Studer: 0.12 (153.6kHz bias)

    // DC blocking (4th-order Butterworth @ 5Hz)
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        void reset() { z1 = z2 = 0.0; }
        double process(double input) {
            double output = b0 * input + z1;
            z1 = b1 * input - a1 * output + z2;
            z2 = b2 * input - a2 * output;
            return output;
        }
    };
    Biquad dcBlocker1, dcBlocker2;

    // AC Bias Shielding (parallel path)
    // HFCut extracts LF for saturation, cleanHF = input - HFCut(input) bypasses
    HFCut hfCut;
    double cleanHfBlend = 1.0;

    // Dispersive allpass (HF phase smear)
    struct AllpassFilter {
        double coefficient = 0.0;
        double z1 = 0.0;
        void setFrequency(double freq, double sampleRate) {
            double w0 = 2.0 * M_PI * freq / sampleRate;
            double tanHalf = std::tan(w0 / 2.0);
            coefficient = (1.0 - tanHalf) / (1.0 + tanHalf);
        }
        void reset() { z1 = 0.0; }
        double process(double input) {
            double output = coefficient * input + z1;
            z1 = input - coefficient * output;
            return output;
        }
    };
    static constexpr int NUM_DISPERSIVE_STAGES = 4;
    AllpassFilter dispersiveAllpass[NUM_DISPERSIVE_STAGES];
    double dispersiveCornerFreq = 10000.0;

    // Jiles-Atherton hysteresis (realistic DAFx parameters)
    JilesAthertonCore jaCore;
    double jaOutputScale = 1.0;  // Calculated for unity gain at 0VU

    // J-A envelope follower (for smooth level tracking)
    double jaEnvelope = 0.0;        // Envelope follower state
    double envAttack = 0.0;         // Attack coefficient (computed from sample rate)
    double envRelease = 0.0;        // Release coefficient (computed from sample rate)

    // Machine EQ
    MachineEQ machineEQ;

    // Startup fade-in to prevent pop from DC blocker initialization
    double fadeInGain = 0.0;       // Current fade-in gain (0 to 1)
    double fadeInIncrement = 0.0;  // Increment per sample (set in setSampleRate)
    static constexpr double FADE_IN_TIME_MS = 150.0;  // 150ms fade-in (4th-order DC blocker needs time)

    void updateCachedValues();
    double saturate(double x);  // Main saturation function
};

} // namespace TapeMachine
