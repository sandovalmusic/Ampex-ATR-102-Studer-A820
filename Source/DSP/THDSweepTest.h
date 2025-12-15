#pragma once

#include "HybridTapeProcessor.h"
#include <cmath>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <iomanip>

namespace TapeMachine
{

/**
 * THD Sweep Test - Full Signal Chain Analysis
 *
 * Tests total harmonic distortion through entire HybridTapeProcessor:
 *   HFCut -> J-A Hysteresis -> Cubic Saturation -> MachineEQ
 *
 * Measures THD at multiple levels and frequencies for all 4 modes:
 *   - Ampex GP9, Ampex SM900, Studer GP9, Studer SM900
 *
 * Target curves (500nW, 30ips alignment):
 *   Studer GP9:  0.18% @ 0VU, MOL @ +12dB
 *   Studer SM900: 0.30% @ 0VU, MOL @ +10dB
 *   Ampex GP9:   0.09% @ 0VU, MOL @ +15dB (custom head scaled)
 *   Ampex SM900: 0.15% @ 0VU, MOL @ +13dB (custom head scaled)
 */
class THDSweepTest
{
public:
    struct THDResult {
        double levelVU;
        double frequency;
        double thd2;         // 2nd harmonic %
        double thd3;         // 3rd harmonic %
        double thdTotal;     // Total THD %
        double thdDB;        // THD in dB
        double eoRatio;      // Even/Odd ratio (H2/H3)
        double fundamental;  // Fundamental amplitude
    };

    struct ModeConfig {
        const char* name;
        double biasStrength;     // < 0.74 = Ampex, >= 0.74 = Studer
        int tapeFormula;         // 0 = GP9, 1 = SM900
        double targetTHD_0VU;    // Target THD % at 0VU
        double molHeadroom;      // dB from 0VU to MOL (3% THD)
    };

    // Target THD curves
    // CURRENT CODE TARGETS (from HybridTapeProcessor.cpp comments):
    //   Ampex GP9:   0.06% @ 0VU, MOL +12dB
    //   Ampex SM900: 0.11% @ 0VU, MOL +12dB
    //   Studer GP9:  0.06% @ 0VU, MOL +9dB
    //   Studer SM900: 0.11% @ 0VU, MOL +9dB
    //
    // RESEARCHED TARGETS (500nW, 30ips, custom Ampex head @ 0.032% THD @ 355nW):
    //   Studer GP9:   0.18% @ 0VU, MOL +12dB
    //   Studer SM900: 0.30% @ 0VU, MOL +10dB
    //   Ampex GP9:    0.09% @ 0VU, MOL +15dB
    //   Ampex SM900:  0.15% @ 0VU, MOL +13dB
    //
    // Using RESEARCHED targets for validation:
    static constexpr std::array<ModeConfig, 4> MODES = {{
        { "Studer GP9",   0.80, 0, 0.18, 12.0 },
        { "Studer SM900", 0.80, 1, 0.30, 10.0 },
        { "Ampex GP9",    0.50, 0, 0.09, 15.0 },
        { "Ampex SM900",  0.50, 1, 0.15, 13.0 }
    }};

    // Test levels in VU (relative to 0VU = 500nW reference)
    static constexpr std::array<double, 7> TEST_LEVELS = { -18.0, -12.0, -6.0, 0.0, 3.0, 6.0, 9.0 };

    // Test frequencies in Hz
    static constexpr std::array<double, 5> TEST_FREQUENCIES = { 100.0, 400.0, 1000.0, 4000.0, 10000.0 };

    THDSweepTest(double sampleRate = 96000.0) : fs(sampleRate) {
        processor.setSampleRate(fs);
    }

    /**
     * Run full parameter sweep for a single mode
     * Returns vector of THD results
     */
    std::vector<THDResult> runSweep(int modeIndex)
    {
        std::vector<THDResult> results;
        const auto& mode = MODES[modeIndex];

        processor.setParameters(mode.biasStrength, 1.0, mode.tapeFormula);
        processor.reset();

        for (double levelVU : TEST_LEVELS) {
            for (double freq : TEST_FREQUENCIES) {
                THDResult result = measureTHD(freq, levelVU);
                result.levelVU = levelVU;
                result.frequency = freq;
                results.push_back(result);
            }
        }

        return results;
    }

    /**
     * Calculate expected THD at a given level based on target curve
     * Uses exponential model: THD = THD_0VU * 10^(level/10)
     */
    static double expectedTHD(const ModeConfig& mode, double levelVU)
    {
        // THD increases ~10x per 10dB (slope of ~1 on log-log)
        return mode.targetTHD_0VU * std::pow(10.0, levelVU / 10.0);
    }

    /**
     * Print formatted results table
     */
    void printResults(int modeIndex, const std::vector<THDResult>& results)
    {
        const auto& mode = MODES[modeIndex];

        std::cout << "\n========================================\n";
        std::cout << "MODE: " << mode.name << "\n";
        std::cout << "Target: " << mode.targetTHD_0VU << "% @ 0VU, MOL @ +"
                  << mode.molHeadroom << "dB\n";
        std::cout << "========================================\n\n";

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "Level(VU) | Freq(Hz) | THD(%)   | THD(dB)  | H2(%)    | H3(%)    | E/O Ratio | Target(%)\n";
        std::cout << "----------|----------|----------|----------|----------|----------|-----------|----------\n";

        for (const auto& r : results) {
            double target = expectedTHD(mode, r.levelVU);
            std::cout << std::setw(9) << r.levelVU << " | "
                      << std::setw(8) << r.frequency << " | "
                      << std::setw(8) << r.thdTotal << " | "
                      << std::setw(8) << r.thdDB << " | "
                      << std::setw(8) << r.thd2 << " | "
                      << std::setw(8) << r.thd3 << " | "
                      << std::setw(9) << r.eoRatio << " | "
                      << std::setw(8) << target << "\n";
        }
    }

    /**
     * Print summary comparison to targets
     */
    void printSummary(int modeIndex, const std::vector<THDResult>& results)
    {
        const auto& mode = MODES[modeIndex];

        std::cout << "\n--- " << mode.name << " Summary (1kHz) ---\n";
        std::cout << "Level | Measured | Target  | Error\n";
        std::cout << "------|----------|---------|-------\n";

        for (const auto& r : results) {
            if (std::abs(r.frequency - 1000.0) < 1.0) {
                double target = expectedTHD(mode, r.levelVU);
                double errorDB = 20.0 * std::log10(r.thdTotal / target);
                std::cout << std::setw(5) << r.levelVU << " | "
                          << std::setw(7) << std::setprecision(4) << r.thdTotal << "% | "
                          << std::setw(6) << target << "% | "
                          << std::setprecision(2) << std::showpos << errorDB
                          << std::noshowpos << " dB\n";
            }
        }
    }

    /**
     * Run all 4 modes and print complete report
     */
    void runFullSweep()
    {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║     THD SWEEP TEST - Full Signal Chain Analysis              ║\n";
        std::cout << "║     500nW/m @ 30ips Alignment - All 4 Modes                  ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n";

        for (int i = 0; i < 4; ++i) {
            auto results = runSweep(i);
            printResults(i, results);
            printSummary(i, results);
        }

        std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    }

private:
    double fs;
    HybridTapeProcessor processor;

    // FFT size for THD measurement (must be power of 2)
    static constexpr int FFT_SIZE = 8192;
    static constexpr int NUM_CYCLES = 64;  // Number of cycles to analyze

    /**
     * Generate test tone and measure THD through processor
     */
    THDResult measureTHD(double frequency, double levelVU)
    {
        THDResult result = {};

        // Calculate amplitude from VU level
        // 0VU = 1.0 (unity), +6VU = 2.0, -6VU = 0.5, etc.
        double amplitude = std::pow(10.0, levelVU / 20.0);

        // Calculate samples needed for whole cycles
        int samplesPerCycle = static_cast<int>(fs / frequency);
        int totalSamples = samplesPerCycle * NUM_CYCLES;
        totalSamples = std::max(totalSamples, FFT_SIZE);

        // Pre-roll to settle filters (2x the analysis length)
        int preRoll = totalSamples * 2;

        // Generate input and process
        std::vector<double> output(totalSamples);
        double phase = 0.0;
        double phaseInc = 2.0 * M_PI * frequency / fs;

        // Pre-roll (discard output)
        processor.reset();
        for (int i = 0; i < preRoll; ++i) {
            double input = amplitude * std::sin(phase);
            processor.processSample(input);
            phase += phaseInc;
        }

        // Capture output
        for (int i = 0; i < totalSamples; ++i) {
            double input = amplitude * std::sin(phase);
            output[i] = processor.processSample(input);
            phase += phaseInc;
        }

        // Measure harmonics using DFT at exact harmonic frequencies
        double fundamental = measureHarmonicAmplitude(output, frequency, 1);
        double h2 = measureHarmonicAmplitude(output, frequency, 2);
        double h3 = measureHarmonicAmplitude(output, frequency, 3);
        double h4 = measureHarmonicAmplitude(output, frequency, 4);
        double h5 = measureHarmonicAmplitude(output, frequency, 5);

        // Calculate THD
        result.fundamental = fundamental;
        result.thd2 = (h2 / fundamental) * 100.0;
        result.thd3 = (h3 / fundamental) * 100.0;

        double sumSquares = h2*h2 + h3*h3 + h4*h4 + h5*h5;
        result.thdTotal = (std::sqrt(sumSquares) / fundamental) * 100.0;
        result.thdDB = 20.0 * std::log10(result.thdTotal / 100.0);
        result.eoRatio = (h3 > 1e-10) ? (h2 / h3) : 999.0;

        return result;
    }

    /**
     * Measure amplitude of specific harmonic using Goertzel-style DFT
     * More accurate than FFT bin interpolation for exact frequencies
     */
    double measureHarmonicAmplitude(const std::vector<double>& signal,
                                     double fundamentalFreq, int harmonic)
    {
        double targetFreq = fundamentalFreq * harmonic;
        int N = static_cast<int>(signal.size());

        // Goertzel algorithm for single frequency
        double k = targetFreq * N / fs;
        double w = 2.0 * M_PI * k / N;
        double cosw = std::cos(w);
        double sinw = std::sin(w);
        double coeff = 2.0 * cosw;

        double s0 = 0.0, s1 = 0.0, s2 = 0.0;

        // Apply Hann window and compute
        for (int i = 0; i < N; ++i) {
            double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / N));
            s0 = signal[i] * window + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        // Compute magnitude
        double real = s1 - s2 * cosw;
        double imag = s2 * sinw;
        double magnitude = std::sqrt(real * real + imag * imag);

        // Normalize for Hann window (factor of 2) and DFT scaling
        return (2.0 * magnitude) / (N * 0.5);
    }
};

// Define static constexpr members (C++17 inline would be better but keeping C++14 compat)
constexpr std::array<THDSweepTest::ModeConfig, 4> THDSweepTest::MODES;
constexpr std::array<double, 7> THDSweepTest::TEST_LEVELS;
constexpr std::array<double, 5> THDSweepTest::TEST_FREQUENCIES;

} // namespace TapeMachine
