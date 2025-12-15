/**
 * Fine Parameter Sweep - Single Mode Tuning
 *
 * Sweeps satA3, satPower, lowLevelScale to match target THD curve
 * Tests at -12, -6, 0, +3, +6 VU and finds best fit
 *
 * Compile: clang++ -std=c++17 -O2 -o fine_sweep fine_sweep.cpp MachineEQ.cpp BiasShielding.cpp HybridTapeProcessor.cpp -I.
 * Run: ./fine_sweep
 */

#include "HybridTapeProcessor.h"
#include <cmath>
#include <vector>
#include <iostream>
#include <iomanip>

using namespace TapeMachine;

constexpr double PI = 3.14159265358979323846;
constexpr double SAMPLE_RATE = 96000.0;
constexpr int NUM_SAMPLES = 8192;
constexpr int PRE_ROLL = 16384;

// Target THD curves (500nW, 30ips)
struct TargetCurve {
    const char* name;
    double biasStrength;  // < 0.74 = Ampex, >= 0.74 = Studer
    int tapeFormula;      // 0 = GP9, 1 = SM900
    double thd_minus12;
    double thd_minus6;
    double thd_0;
    double thd_plus3;
    double thd_plus6;
};

// Targets derived from research (exponential model: THD = THD_0VU * 10^(level/10))
TargetCurve targets[] = {
    { "Studer GP9",   0.80, 0, 0.0114, 0.0452, 0.18, 0.359, 0.717 },
    { "Studer SM900", 0.80, 1, 0.0189, 0.0754, 0.30, 0.599, 1.194 },
    { "Ampex GP9",    0.50, 0, 0.0057, 0.0226, 0.09, 0.180, 0.358 },
    { "Ampex SM900",  0.50, 1, 0.0095, 0.0377, 0.15, 0.299, 0.597 }
};

double measureAmplitude(const std::vector<double>& signal, double freq, double fs)
{
    int N = static_cast<int>(signal.size());
    double k = freq * N / fs;
    double w = 2.0 * PI * k / N;
    double cosw = std::cos(w);
    double sinw = std::sin(w);
    double coeff = 2.0 * cosw;

    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < N; ++i) {
        double window = 0.5 * (1.0 - std::cos(2.0 * PI * i / N));
        s0 = signal[i] * window + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    double real = s1 - s2 * cosw;
    double imag = s2 * sinw;
    return (2.0 * std::sqrt(real * real + imag * imag)) / (N * 0.5);
}

double measureTHD(HybridTapeProcessor& proc, double levelVU, double freq = 1000.0)
{
    double amplitude = std::pow(10.0, levelVU / 20.0);
    double phaseInc = 2.0 * PI * freq / SAMPLE_RATE;
    double phase = 0.0;

    proc.reset();

    // Pre-roll
    for (int i = 0; i < PRE_ROLL; ++i) {
        proc.processSample(amplitude * std::sin(phase));
        phase += phaseInc;
    }

    // Capture
    std::vector<double> output(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        output[i] = proc.processSample(amplitude * std::sin(phase));
        phase += phaseInc;
    }

    double f1 = measureAmplitude(output, freq, SAMPLE_RATE);
    double h2 = measureAmplitude(output, freq * 2, SAMPLE_RATE);
    double h3 = measureAmplitude(output, freq * 3, SAMPLE_RATE);
    double h4 = measureAmplitude(output, freq * 4, SAMPLE_RATE);
    double h5 = measureAmplitude(output, freq * 5, SAMPLE_RATE);

    return (std::sqrt(h2*h2 + h3*h3 + h4*h4 + h5*h5) / f1) * 100.0;
}

double calculateError(double* measured, double* target, int n)
{
    double sumSqError = 0.0;
    for (int i = 0; i < n; ++i) {
        double errorDB = 20.0 * std::log10(measured[i] / target[i]);
        sumSqError += errorDB * errorDB;
    }
    return std::sqrt(sumSqError / n);  // RMS error in dB
}

int main(int argc, char* argv[])
{
    int modeIndex = 0;  // Default: Studer GP9
    if (argc > 1) {
        modeIndex = std::atoi(argv[1]);
        if (modeIndex < 0 || modeIndex > 3) modeIndex = 0;
    }

    TargetCurve& target = targets[modeIndex];

    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Fine Parameter Sweep: " << std::setw(30) << std::left << target.name << "  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";

    std::cout << "Target THD curve:\n";
    std::cout << "  -12VU: " << target.thd_minus12 << "%\n";
    std::cout << "  -6VU:  " << target.thd_minus6 << "%\n";
    std::cout << "  0VU:   " << target.thd_0 << "%\n";
    std::cout << "  +3VU:  " << target.thd_plus3 << "%\n";
    std::cout << "  +6VU:  " << target.thd_plus6 << "%\n\n";

    double targetVals[] = { target.thd_minus12, target.thd_minus6, target.thd_0,
                            target.thd_plus3, target.thd_plus6 };
    double levels[] = { -12.0, -6.0, 0.0, 3.0, 6.0 };

    // Parameter ranges to sweep
    double a3_vals[] = { 0.004, 0.005, 0.006, 0.007, 0.008 };
    double power_vals[] = { 0.6, 0.8, 1.0, 1.2, 1.4 };
    double lowScale_vals[] = { 0.3, 0.5, 0.7, 0.9 };

    // Adjust ranges based on mode
    if (modeIndex == 2) { // Ampex GP9 - lower THD
        a3_vals[0] = 0.002; a3_vals[1] = 0.0025; a3_vals[2] = 0.003;
        a3_vals[3] = 0.0035; a3_vals[4] = 0.004;
    } else if (modeIndex == 3) { // Ampex SM900
        a3_vals[0] = 0.003; a3_vals[1] = 0.004; a3_vals[2] = 0.005;
        a3_vals[3] = 0.006; a3_vals[4] = 0.007;
    } else if (modeIndex == 1) { // Studer SM900 - higher THD
        a3_vals[0] = 0.007; a3_vals[1] = 0.009; a3_vals[2] = 0.011;
        a3_vals[3] = 0.013; a3_vals[4] = 0.015;
    }

    HybridTapeProcessor proc;
    proc.setSampleRate(SAMPLE_RATE);

    double bestError = 1000.0;
    double bestA3 = 0, bestPower = 0, bestLowScale = 0;
    double bestMeasured[5];

    std::cout << "Sweeping parameters...\n";
    std::cout << std::fixed << std::setprecision(4);

    int totalTests = 5 * 5 * 4;
    int testNum = 0;

    for (double a3 : a3_vals) {
        for (double power : power_vals) {
            for (double lowScale : lowScale_vals) {
                testNum++;

                // We need to modify the processor's internal parameters
                // Since we can't directly set them, we'll use a workaround:
                // Create a custom test that simulates the saturation curve

                // For now, just test with current processor settings
                // and report what we find

                if (testNum == 1) {
                    // Just test current settings
                    proc.setParameters(target.biasStrength, 1.0, target.tapeFormula);

                    double measured[5];
                    for (int i = 0; i < 5; ++i) {
                        measured[i] = measureTHD(proc, levels[i]);
                    }

                    double error = calculateError(measured, targetVals, 5);

                    std::cout << "\nCurrent processor settings:\n";
                    std::cout << "Level  | Measured | Target  | Error(dB)\n";
                    std::cout << "-------|----------|---------|----------\n";
                    for (int i = 0; i < 5; ++i) {
                        double errDB = 20.0 * std::log10(measured[i] / targetVals[i]);
                        std::cout << std::setw(6) << levels[i] << " | "
                                  << std::setw(8) << measured[i] << " | "
                                  << std::setw(7) << targetVals[i] << " | "
                                  << std::setw(+7) << std::showpos << errDB
                                  << std::noshowpos << "\n";
                    }
                    std::cout << "\nRMS Error: " << error << " dB\n";
                }
            }
        }
    }

    // Now let's do a direct cubic model sweep to find theoretical best params
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "Theoretical cubic model sweep (y = x - a3*x³ with level scaling):\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";

    bestError = 1000.0;

    for (double a3 : a3_vals) {
        for (double power : power_vals) {
            for (double lowScale : lowScale_vals) {
                double measured[5];

                for (int lvlIdx = 0; lvlIdx < 5; ++lvlIdx) {
                    double amplitude = std::pow(10.0, levels[lvlIdx] / 20.0);

                    // Simulate level-scaled cubic: effectiveA3 = a3 * env^power
                    // with lowLevelScale below threshold
                    double env = amplitude;
                    double effectiveA3 = a3 * std::pow(std::max(0.01, env), power);

                    const double lowThresh = 0.5;
                    if (env < lowThresh) {
                        double t = env / lowThresh;
                        effectiveA3 *= (lowScale + (1.0 - lowScale) * t * t);
                    }

                    // Simple cubic THD: THD3 ≈ (1/4) * a3_eff * A²
                    double thd3 = 0.25 * effectiveA3 * amplitude * amplitude;
                    measured[lvlIdx] = (thd3 / amplitude) * 100.0;  // as percentage
                }

                double error = calculateError(measured, targetVals, 5);

                if (error < bestError) {
                    bestError = error;
                    bestA3 = a3;
                    bestPower = power;
                    bestLowScale = lowScale;
                    for (int i = 0; i < 5; ++i) bestMeasured[i] = measured[i];
                }
            }
        }
    }

    std::cout << "Best theoretical parameters:\n";
    std::cout << "  satA3 = " << bestA3 << "\n";
    std::cout << "  satPower = " << bestPower << "\n";
    std::cout << "  lowLevelScale = " << bestLowScale << "\n";
    std::cout << "  RMS Error = " << bestError << " dB\n\n";

    std::cout << "Level  | Model    | Target  | Error(dB)\n";
    std::cout << "-------|----------|---------|----------\n";
    for (int i = 0; i < 5; ++i) {
        double errDB = 20.0 * std::log10(bestMeasured[i] / targetVals[i]);
        std::cout << std::setw(6) << levels[i] << " | "
                  << std::setw(8) << bestMeasured[i] << " | "
                  << std::setw(7) << targetVals[i] << " | "
                  << std::setw(+7) << std::showpos << errDB
                  << std::noshowpos << "\n";
    }

    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "RECOMMENDED UPDATE for " << target.name << ":\n";
    std::cout << "  satA3 = " << bestA3 << ";\n";
    std::cout << "  satPower = " << bestPower << ";\n";
    std::cout << "  lowLevelScale = " << bestLowScale << ";\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";

    return 0;
}
