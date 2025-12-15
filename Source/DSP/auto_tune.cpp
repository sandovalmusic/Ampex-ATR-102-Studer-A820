/**
 * Auto-Tune Parameter Sweep
 *
 * Tests actual processor with many parameter combinations
 * to find optimal THD curve fit for each mode.
 *
 * Compile: clang++ -std=c++17 -O3 -o auto_tune auto_tune.cpp MachineEQ.cpp BiasShielding.cpp HybridTapeProcessor.cpp -I.
 */

#include "HybridTapeProcessor.h"
#include <cmath>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>

using namespace TapeMachine;

constexpr double PI = 3.14159265358979323846;
constexpr double SAMPLE_RATE = 96000.0;
constexpr int NUM_SAMPLES = 8192;
constexpr int PRE_ROLL = 16384;

struct TargetCurve {
    const char* name;
    double biasStrength;
    int tapeFormula;
    double thd[5];  // -12, -6, 0, +3, +6 VU
};

TargetCurve targets[] = {
    { "Studer GP9",   0.80, 0, {0.0114, 0.0452, 0.18, 0.359, 0.717} },
    { "Studer SM900", 0.80, 1, {0.0189, 0.0754, 0.30, 0.599, 1.194} },
    { "Ampex GP9",    0.50, 0, {0.0057, 0.0226, 0.09, 0.180, 0.358} },
    { "Ampex SM900",  0.50, 1, {0.0095, 0.0377, 0.15, 0.299, 0.597} }
};

double levels[] = {-12.0, -6.0, 0.0, 3.0, 6.0};

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

    for (int i = 0; i < PRE_ROLL; ++i) {
        proc.processSample(amplitude * std::sin(phase));
        phase += phaseInc;
    }

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

double calculateRMSError(double* measured, double* target)
{
    double sumSqError = 0.0;
    for (int i = 0; i < 5; ++i) {
        double errorDB = 20.0 * std::log10(measured[i] / target[i]);
        sumSqError += errorDB * errorDB;
    }
    return std::sqrt(sumSqError / 5.0);
}

struct ParamSet {
    double satA3;
    double satPower;
    double lowLevelScale;
    double jaBlend;
    double rmsError;
    double measured[5];
};

void runSweep(int modeIndex)
{
    TargetCurve& target = targets[modeIndex];

    std::cout << "╔═══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Auto-Tune Sweep: " << std::setw(40) << std::left << target.name << "  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════════╝\n\n";

    std::cout << "Target THD curve:\n";
    for (int i = 0; i < 5; ++i) {
        std::cout << "  " << std::setw(4) << levels[i] << "VU: " << target.thd[i] << "%\n";
    }
    std::cout << "\n";

    // Define parameter ranges based on mode
    std::vector<double> a3_vals, power_vals, lowScale_vals, jaBlend_vals;

    if (modeIndex == 0) { // Studer GP9
        for (double v = 0.0042; v <= 0.0052; v += 0.0001) a3_vals.push_back(v);
        for (double v = 0.40; v <= 0.50; v += 0.01) power_vals.push_back(v);
        for (double v = 0.48; v <= 0.58; v += 0.01) lowScale_vals.push_back(v);
        for (double v = 0.010; v <= 0.016; v += 0.001) jaBlend_vals.push_back(v);
    } else if (modeIndex == 1) { // Studer SM900
        for (double v = 0.0072; v <= 0.0082; v += 0.0001) a3_vals.push_back(v);
        for (double v = 0.40; v <= 0.50; v += 0.01) power_vals.push_back(v);
        for (double v = 0.48; v <= 0.58; v += 0.01) lowScale_vals.push_back(v);
        for (double v = 0.010; v <= 0.016; v += 0.001) jaBlend_vals.push_back(v);
    } else if (modeIndex == 2) { // Ampex GP9
        for (double v = 0.0028; v <= 0.0038; v += 0.0001) a3_vals.push_back(v);
        for (double v = 0.24; v <= 0.34; v += 0.01) power_vals.push_back(v);
        for (double v = 0.74; v <= 0.84; v += 0.01) lowScale_vals.push_back(v);
        for (double v = 0.004; v <= 0.010; v += 0.001) jaBlend_vals.push_back(v);
    } else { // Ampex SM900
        for (double v = 0.0046; v <= 0.0056; v += 0.0001) a3_vals.push_back(v);
        for (double v = 0.24; v <= 0.34; v += 0.01) power_vals.push_back(v);
        for (double v = 0.74; v <= 0.84; v += 0.01) lowScale_vals.push_back(v);
        for (double v = 0.004; v <= 0.010; v += 0.001) jaBlend_vals.push_back(v);
    }

    int totalTests = a3_vals.size() * power_vals.size() * lowScale_vals.size() * jaBlend_vals.size();
    std::cout << "Testing " << totalTests << " parameter combinations...\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  satA3: " << a3_vals.front() << " to " << a3_vals.back() << " (" << a3_vals.size() << " values)\n";
    std::cout << "  satPower: " << power_vals.front() << " to " << power_vals.back() << " (" << power_vals.size() << " values)\n";
    std::cout << "  lowLevelScale: " << lowScale_vals.front() << " to " << lowScale_vals.back() << " (" << lowScale_vals.size() << " values)\n";
    std::cout << "  jaBlend: " << jaBlend_vals.front() << " to " << jaBlend_vals.back() << " (" << jaBlend_vals.size() << " values)\n\n";

    HybridTapeProcessor proc;
    proc.setSampleRate(SAMPLE_RATE);

    ParamSet best;
    best.rmsError = 1000.0;

    int testCount = 0;
    int progressStep = totalTests / 50;
    if (progressStep < 1) progressStep = 1;

    for (double a3 : a3_vals) {
        for (double power : power_vals) {
            for (double lowScale : lowScale_vals) {
                for (double jaBlend : jaBlend_vals) {
                    testCount++;
                    if (testCount % progressStep == 0) {
                        std::cout << "\r  Progress: " << (testCount * 100 / totalTests) << "% (best so far: "
                                  << best.rmsError << " dB)" << std::flush;
                    }

                    // Set mode first, then override with test parameters
                    proc.setParameters(target.biasStrength, 1.0, target.tapeFormula);
                    proc.setTestParameters(a3, power, lowScale, jaBlend);

                    double measured[5];
                    for (int i = 0; i < 5; ++i) {
                        measured[i] = measureTHD(proc, levels[i]);
                    }

                    double error = calculateRMSError(measured, target.thd);

                    if (error < best.rmsError) {
                        best.rmsError = error;
                        best.satA3 = a3;
                        best.satPower = power;
                        best.lowLevelScale = lowScale;
                        best.jaBlend = jaBlend;
                        for (int i = 0; i < 5; ++i) best.measured[i] = measured[i];
                    }
                }
            }
        }
    }

    std::cout << "\r  Progress: 100%                                    \n\n";

    std::cout << "═══════════════════════════════════════════════════════════════════\n";
    std::cout << "BEST PARAMETERS FOUND:\n";
    std::cout << "═══════════════════════════════════════════════════════════════════\n\n";

    std::cout << "RMS Error: " << best.rmsError << " dB\n\n";
    std::cout << "Parameters:\n";
    std::cout << "  satA3 = " << best.satA3 << ";\n";
    std::cout << "  satPower = " << best.satPower << ";\n";
    std::cout << "  lowLevelScale = " << best.lowLevelScale << ";\n";
    std::cout << "  jaBlend = " << best.jaBlend << ";\n\n";

    std::cout << "Level  | Measured | Target  | Error(dB)\n";
    std::cout << "-------|----------|---------|----------\n";
    for (int i = 0; i < 5; ++i) {
        double errDB = 20.0 * std::log10(best.measured[i] / target.thd[i]);
        std::cout << std::setw(6) << levels[i] << " | "
                  << std::setw(8) << best.measured[i] << " | "
                  << std::setw(7) << target.thd[i] << " | "
                  << std::setw(+7) << std::showpos << errDB
                  << std::noshowpos << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char* argv[])
{
    if (argc > 1) {
        int modeIndex = std::atoi(argv[1]);
        if (modeIndex >= 0 && modeIndex <= 3) {
            runSweep(modeIndex);
        } else {
            std::cout << "Usage: ./auto_tune [mode]\n";
            std::cout << "  0 = Studer GP9\n";
            std::cout << "  1 = Studer SM900\n";
            std::cout << "  2 = Ampex GP9\n";
            std::cout << "  3 = Ampex SM900\n";
            std::cout << "  (no arg) = all modes\n";
        }
    } else {
        // Run all modes
        for (int i = 0; i < 4; ++i) {
            runSweep(i);
            std::cout << "\n";
        }
    }

    return 0;
}
