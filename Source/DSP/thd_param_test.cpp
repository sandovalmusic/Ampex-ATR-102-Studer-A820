/**
 * Quick parameter test - find satA3 values that produce target THD
 * Using isolated cubic saturation model
 *
 * Compile: clang++ -std=c++17 -O2 -o thd_param_test thd_param_test.cpp
 */

#include <cmath>
#include <iostream>
#include <iomanip>

constexpr double PI = 3.14159265358979323846;

// Simulate cubic saturation and measure THD3
// y = x - a3 * x³
// For sine input: THD3 = (a3/4) * A² / (1 - 3*a3/4) approximately
double simulateCubicTHD(double a3, double amplitude)
{
    constexpr int N = 8192;
    constexpr double fs = 96000.0;
    constexpr double freq = 1000.0;

    // Generate signal through cubic saturation
    double sumFund = 0.0, sumH3 = 0.0;
    for (int i = 0; i < N; ++i) {
        double t = 2.0 * PI * freq * i / fs;
        double x = amplitude * std::sin(t);
        double y = x - a3 * x * x * x;

        // Simple correlation to extract harmonics
        double window = 0.5 * (1.0 - std::cos(2.0 * PI * i / N));
        sumFund += y * std::sin(t) * window;
        sumH3 += y * std::sin(3 * t) * window;
    }

    double fund = std::abs(sumFund) * 4.0 / N;
    double h3 = std::abs(sumH3) * 4.0 / N;

    return (h3 / fund) * 100.0;
}

// Find a3 value that produces target THD
double findA3ForTargetTHD(double targetTHD, double amplitude)
{
    double a3_low = 0.0001;
    double a3_high = 0.5;

    for (int iter = 0; iter < 50; ++iter) {
        double a3_mid = (a3_low + a3_high) / 2.0;
        double thd = simulateCubicTHD(a3_mid, amplitude);

        if (thd < targetTHD) {
            a3_low = a3_mid;
        } else {
            a3_high = a3_mid;
        }
    }

    return (a3_low + a3_high) / 2.0;
}

int main()
{
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Parameter Finder - satA3 for Target THD at 0VU          ║\n";
    std::cout << "║  Pure cubic model: y = x - a3*x³                          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";

    // Targets at 0VU (amplitude = 1.0)
    struct Target {
        const char* name;
        double targetTHD;  // %
    };

    Target targets[] = {
        { "Ampex GP9",    0.09 },
        { "Ampex SM900",  0.15 },
        { "Studer GP9",   0.18 },
        { "Studer SM900", 0.30 }
    };

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Finding satA3 values for PURE CUBIC model:\n";
    std::cout << "(Note: Actual processor has J-A hysteresis, HFCut which add ~10-20% more THD)\n\n";

    std::cout << "Mode          | Target THD | Calc satA3 | Verify THD\n";
    std::cout << "--------------|------------|------------|------------\n";

    for (const auto& t : targets) {
        double a3 = findA3ForTargetTHD(t.targetTHD, 1.0);
        double verify = simulateCubicTHD(a3, 1.0);

        std::cout << std::setw(13) << t.name << " | "
                  << std::setw(9) << t.targetTHD << "% | "
                  << std::setw(10) << a3 << " | "
                  << std::setw(9) << verify << "%\n";
    }

    std::cout << "\n--- Accounting for J-A/HFCut overhead (~15% reduction) ---\n\n";

    std::cout << "Mode          | Target THD | Adjusted satA3\n";
    std::cout << "--------------|------------|---------------\n";

    for (const auto& t : targets) {
        // Reduce target by 15% to account for J-A contribution
        double adjustedTarget = t.targetTHD * 0.85;
        double a3 = findA3ForTargetTHD(adjustedTarget, 1.0);

        std::cout << std::setw(13) << t.name << " | "
                  << std::setw(9) << t.targetTHD << "% | "
                  << std::setw(13) << a3 << "\n";
    }

    std::cout << "\n--- THD across levels with recommended satA3 ---\n\n";

    double ampexGP9_a3 = findA3ForTargetTHD(0.09 * 0.85, 1.0);

    std::cout << "Using Ampex GP9 satA3 = " << ampexGP9_a3 << ":\n";
    std::cout << "Level(VU) | Amplitude | THD(%)\n";
    std::cout << "----------|-----------|--------\n";

    double levels[] = { -18, -12, -6, 0, 3, 6, 9 };
    for (double lvl : levels) {
        double amp = std::pow(10.0, lvl / 20.0);
        double thd = simulateCubicTHD(ampexGP9_a3, amp);
        std::cout << std::setw(9) << lvl << " | "
                  << std::setw(9) << amp << " | "
                  << std::setw(6) << thd << "\n";
    }

    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "RECOMMENDATION: Start with these satA3 values and fine-tune.\n";
    std::cout << "The level-scaling (satPower) affects the THD curve slope.\n";

    return 0;
}
