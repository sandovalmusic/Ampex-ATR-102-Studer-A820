#include "HybridTapeProcessor.h"
#include <algorithm>

namespace TapeMachine
{

HybridTapeProcessor::HybridTapeProcessor()
{
    updateCachedValues();
    reset();
}

void HybridTapeProcessor::setSampleRate(double sampleRate)
{
    fs = sampleRate;
    hfCut.setSampleRate(sampleRate);
    jaCore.setSampleRate(sampleRate);
    machineEQ.setSampleRate(sampleRate);

    // Envelope follower coefficients for level-dependent J-A blend
    // Fast attack (~1ms) to catch transients, slow release (~50ms) for smooth decay
    envAttack = std::exp(-1.0 / (0.001 * sampleRate));
    envRelease = std::exp(-1.0 / (0.050 * sampleRate));

    // Fade-in increment: reach 1.0 in FADE_IN_TIME_MS milliseconds
    fadeInIncrement = 1.0 / (FADE_IN_TIME_MS * 0.001 * sampleRate);

    // Configure dispersive allpass cascade for HF phase smear
    for (int i = 0; i < NUM_DISPERSIVE_STAGES; ++i) {
        double freq = dispersiveCornerFreq * std::pow(2.0, i * 0.5);
        dispersiveAllpass[i].setFrequency(freq, sampleRate);
    }

    // Design 4th-order Butterworth high-pass at 5 Hz for DC blocking
    double fc = 5.0;
    double w0 = 2.0 * M_PI * fc / sampleRate;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / (2.0 * 0.7071);

    double b0 = (1.0 + cosw0) / 2.0;
    double b1 = -(1.0 + cosw0);
    double b2 = (1.0 + cosw0) / 2.0;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    dcBlocker1.b0 = b0 / a0;
    dcBlocker1.b1 = b1 / a0;
    dcBlocker1.b2 = b2 / a0;
    dcBlocker1.a1 = a1 / a0;
    dcBlocker1.a2 = a2 / a0;

    dcBlocker2.b0 = dcBlocker1.b0;
    dcBlocker2.b1 = dcBlocker1.b1;
    dcBlocker2.b2 = dcBlocker1.b2;
    dcBlocker2.a1 = dcBlocker1.a1;
    dcBlocker2.a2 = dcBlocker1.a2;
}

void HybridTapeProcessor::reset()
{
    dcBlocker1.reset();
    dcBlocker2.reset();
    hfCut.reset();
    jaCore.reset();
    machineEQ.reset();

    for (int i = 0; i < NUM_DISPERSIVE_STAGES; ++i) {
        dispersiveAllpass[i].reset();
    }

    for (int i = 0; i < DELAY_BUFFER_SIZE; ++i) {
        delayBuffer[i] = 0.0;
    }
    delayWriteIndex = 0;
    allpassState = 0.0;
    jaEnvelope = 0.0;
    satEnvelope = 0.0;
    fadeInGain = 0.0;  // Reset fade-in on reset
}

void HybridTapeProcessor::setParameters(double biasStrength, double inputGain)
{
    double clampedBias = std::clamp(biasStrength, 0.0, 1.0);
    bool newIsAmpexMode = (clampedBias < 0.74);

    if (newIsAmpexMode != isAmpexMode || inputGain != currentInputGain)
    {
        currentBiasStrength = clampedBias;
        currentInputGain = inputGain;
        updateCachedValues();
    }
}

void HybridTapeProcessor::updateCachedValues()
{
    isAmpexMode = (currentBiasStrength < 0.74);

    // === Realistic J-A Parameters (DAFx 2019 paper) ===
    // These are calibrated for actual tape behavior
    JilesAthertonCore::Parameters jaParams;
    jaParams.M_s = 350000.0;   // Saturation magnetization
    jaParams.a = 22000.0;      // Domain wall density
    jaParams.k = 27500.0;      // Coercivity
    jaParams.c = 0.98;         // High reversibility for calibrated 30 IPS
    jaParams.alpha = 1.6e-3;   // Mean field parameter
    jaCore.setParameters(jaParams);

    // Output scale for unity gain at 0VU (0.316)
    // Calculated from: 0.316 / (J-A output at 0.316 input) ≈ 146
    jaOutputScale = 146.0;

    // J-A output is scaled to unity at 0VU (0.316 input)
    // So saturate() receives similar normalized signal levels as our standalone tests
    // Use the same saturation parameters directly

    if (isAmpexMode) {
        // AMPEX ATR-102 (MASTER MODE)
        // E/O ratio ~0.50 (target 0.50, odd-dominant)
        // Level-scaled cubic: effectiveA3 = a3 * level^power
        // Tuned with J-A in the loop
        satA3 = 0.0030;   // Base cubic coefficient
        satPower = 0.35;  // THD slope = 2.35 on log-log (steeper for smoother +3/+6)
        inputBias = 0.075; // DC bias for even harmonics (tuned for E/O = 0.50)
        lowLevelScale = 0.20;  // 20% a3 at very low levels
        dispersiveCornerFreq = 10000.0;
        // J-A blend: 432kHz bias = more linearization = less hysteresis
        jaBlend = 0.06;   // 6% - transparent mastering character
    } else {
        // STUDER A820 (TRACKS MODE)
        // E/O ratio ~1.12 (target 1.12, even-dominant)
        // Level-scaled cubic: effectiveA3 = a3 * level^power
        // Tuned with J-A in the loop
        satA3 = 0.0058;   // Base cubic coefficient
        satPower = 0.48;  // THD slope = 2.48 on log-log
        inputBias = 0.18; // DC bias for even harmonics (tuned for E/O = 1.12)
        lowLevelScale = 0.80;  // 80% a3 at very low levels
        dispersiveCornerFreq = 2800.0;
        // J-A blend: 153.6kHz bias = less linearization = more hysteresis
        jaBlend = 0.12;   // 12% - more tape compression character
    }

    // Azimuth delay: Ampex 8μs, Studer 12μs
    double delayMicroseconds = isAmpexMode ? 8.0 : 12.0;
    cachedDelaySamples = delayMicroseconds * 1e-6 * fs;

    // Reconfigure allpass filters
    for (int i = 0; i < NUM_DISPERSIVE_STAGES; ++i) {
        double freq = dispersiveCornerFreq * std::pow(2.0, i * 0.5);
        dispersiveAllpass[i].setFrequency(freq, fs);
    }

    // Update machine EQ
    machineEQ.setMachine(isAmpexMode ? MachineEQ::Machine::Ampex : MachineEQ::Machine::Studer);

    // Update AC bias shielding curve
    hfCut.setMachineMode(isAmpexMode);
}

double HybridTapeProcessor::saturate(double x)
{
    // Level-scaled cubic saturation with DC bias for even harmonics
    // effectiveA3 = satA3 * level^satPower
    // This gives THD slope of (2 + satPower) on log-log scale
    // Steeper than pure cubic, matching real tape behavior

    // Update saturation envelope (tracks signal level for a3 scaling)
    double absLevel = std::abs(x);
    double satEnvCoeff = (absLevel > satEnvelope) ? 0.9 : 0.999;
    satEnvelope = satEnvCoeff * satEnvelope + (1.0 - satEnvCoeff) * absLevel;

    // Add bias for even harmonic generation (E/O ratio control)
    double biased = x + inputBias;

    // Scale a3 coefficient based on envelope level
    // effectiveA3 = satA3 * (envelope)^power
    // Using std::pow which compilers optimize well for fractional exponents
    double clampedEnv = std::max(0.01, satEnvelope);
    double effectiveA3 = satA3 * std::pow(clampedEnv, satPower);

    // Extra reduction at low levels (below ~-6dB) to match tape's low-level linearity
    // Using squared curve so reduction is concentrated at very low levels
    const double lowThreshold = 0.5;
    if (clampedEnv < lowThreshold) {
        double t = clampedEnv / lowThreshold;  // 0 to 1
        double tSquared = t * t;  // Concentrate reduction at very low levels
        effectiveA3 *= (lowLevelScale + (1.0 - lowLevelScale) * tSquared);
    }

    // Cubic saturation: y = x - a3*x³
    double biasedSq = biased * biased;
    double saturated = biased - effectiveA3 * biasedSq * biased;

    return saturated;
}

double HybridTapeProcessor::processSample(double input)
{
    double gained = input * currentInputGain;

    // === PARALLEL PATH PROCESSING (AC Bias Shielding) ===
    // LF goes to saturation via HFCut
    // HF bypasses saturation: cleanHF = input - HFCut(input)
    // This is a complementary filter - cleanHF extracts what HFCut removed
    double hfCutSignal = hfCut.processSample(gained);
    double cleanHF = gained - hfCutSignal;

    // === LEVEL-DEPENDENT J-A BLENDING ===
    // Simulates AC bias linearization: J-A is nearly linear at normal levels
    // and progressively engages nonlinearity at high levels
    double absLevel = std::abs(hfCutSignal);

    // Envelope follower for smooth blend transitions
    double envCoeff = (absLevel > jaEnvelope) ? envAttack : envRelease;
    jaEnvelope = envCoeff * jaEnvelope + (1.0 - envCoeff) * absLevel;

    // === J-A HYSTERESIS (magnetic feel) ===
    // Machine-specific blend based on AC bias frequency:
    // Higher bias = more linearization = less hysteresis character
    // Ampex (432kHz): 6%, Studer (153.6kHz): 12%
    double jaOut = jaCore.process(hfCutSignal) * jaOutputScale;

    // STABILITY FIX: Soft limit J-A output to prevent pops from numerical artifacts
    // The 146x scaling can amplify small glitches to audible levels
    // Limit to ±2.0 (well beyond normal signal range) with soft knee
    if (std::abs(jaOut) > 1.5) {
        double sign = (jaOut >= 0.0) ? 1.0 : -1.0;
        double excess = std::abs(jaOut) - 1.5;
        jaOut = sign * (1.5 + 0.5 * std::tanh(excess * 2.0));
    }

    // NaN/Inf protection - pass through dry signal if J-A produces garbage
    if (!std::isfinite(jaOut)) {
        jaOut = hfCutSignal;
    }

    double blended = hfCutSignal * (1.0 - jaBlend) + jaOut * jaBlend;

    // === LEVEL-SCALED CUBIC SATURATION ===
    // Adds bias internally for even harmonics (E/O ratio control)
    // effectiveA3 = a3 * level^power for steeper THD curve
    double saturated = saturate(blended);

    // === COMBINE PATHS ===
    double output = saturated + cleanHF * cleanHfBlend;

    // Machine-specific EQ
    output = machineEQ.processSample(output);

    // HF dispersive allpass (tape head phase smear)
    for (int i = 0; i < NUM_DISPERSIVE_STAGES; ++i) {
        output = dispersiveAllpass[i].process(output);
    }

    // DC blocking
    output = dcBlocker1.process(output);
    output = dcBlocker2.process(output);

    // Fade-in to prevent pop from DC blocker initialization
    if (fadeInGain < 1.0) {
        output *= fadeInGain;
        fadeInGain += fadeInIncrement;
        if (fadeInGain > 1.0) fadeInGain = 1.0;
    }

    return output;
}

double HybridTapeProcessor::processRightChannel(double input)
{
    double processed = processSample(input);

    // Azimuth delay using Thiran allpass interpolation
    // Allpass preserves flat magnitude response (no HF roll-off)
    // Only adds phase shift for the timing difference

    delayBuffer[delayWriteIndex] = processed;

    // Integer and fractional parts of delay
    int intDelay = static_cast<int>(cachedDelaySamples);
    double frac = cachedDelaySamples - intDelay;

    // For very small delays (< 0.1 samples), just return processed signal
    if (cachedDelaySamples < 0.1) {
        delayWriteIndex = (delayWriteIndex + 1) % DELAY_BUFFER_SIZE;
        return processed;
    }

    // Thiran allpass coefficient: a = (1-d)/(1+d) for fractional delay d
    double allpassCoeff = (1.0 - frac) / (1.0 + frac);

    // Read positions for integer delay
    int readIndex = (delayWriteIndex - intDelay - 1 + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
    int readIndexNext = (readIndex + 1) % DELAY_BUFFER_SIZE;

    // First-order Thiran allpass: y[n] = a*x[n] + x[n-1] - a*y[n-1]
    double x_curr = delayBuffer[readIndexNext];
    double x_prev = delayBuffer[readIndex];

    double delayed = allpassCoeff * x_curr + x_prev - allpassCoeff * allpassState;
    allpassState = delayed;

    delayWriteIndex = (delayWriteIndex + 1) % DELAY_BUFFER_SIZE;

    return delayed;
}

} // namespace TapeMachine
