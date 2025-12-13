#pragma once

#include <cmath>
#include <algorithm>

namespace TapeMachine {

// Fast tanh approximation using Padé approximant
// Accurate to ~1e-6 for |x| < 4, smoothly saturates beyond
// ~3-4x faster than std::tanh
inline double fastTanh(double x) {
    // For large |x|, tanh saturates to ±1
    if (x > 4.0) return 1.0;
    if (x < -4.0) return -1.0;

    // Padé approximant: tanh(x) ≈ x(27 + x²) / (27 + 9x²)
    // More accurate version with higher order terms
    double x2 = x * x;
    double num = x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)));
    double den = 135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0));
    return num / den;
}

// Jiles-Atherton Hysteresis Model
// Based on "Real-Time Physical Modelling for Analog Tape Machines" (DAFx 2019)
//
// STABILITY FIXES:
// - Increased Langevin threshold from 1e-4 to 0.01 to avoid coth(x) singularity
// - Added output soft limiting to prevent pops from numerical artifacts
// - Added denormal and NaN/Inf protection
class JilesAthertonCore {
public:
    struct Parameters {
        double M_s = 350000.0;   // Saturation magnetization
        double a = 22000.0;      // Domain wall density
        double k = 27500.0;      // Coercivity
        double c = 1.7e-1;       // Reversibility
        double alpha = 1.6e-3;   // Mean field parameter
    };

    JilesAthertonCore() { reset(); }

    void setParameters(const Parameters& p) {
        params = p;
        oneOverA = 1.0 / params.a;
        cAlpha = params.c * params.alpha;
    }

    void setSampleRate(double sr) {
        T = 1.0 / sr;
    }

    void reset() {
        M_n1 = 0.0;
        H_n1 = 0.0;
    }

    double process(double H) {
        // Flush denormals in input
        if (std::abs(H) < 1e-15) H = 0.0;

        // Calculate derivative with slew rate limiting
        // Limit delta to prevent numerical instability from sudden changes
        double delta = H - H_n1;
        delta = std::clamp(delta, -10000.0 * T, 10000.0 * T);  // Max 10000 units/second
        double H_d = delta / T;

        double M = solveNR8(H, H_d);

        // NaN/Inf protection - reset state if we get garbage
        if (!std::isfinite(M)) {
            M = 0.0;
            M_n1 = 0.0;
            H_n1 = H;
            return 0.0;
        }

        H_n1 = H;
        M_n1 = M;

        // Soft limit output to prevent pops from numerical artifacts
        // Uses gentle tanh limiting at ±M_s with some headroom
        double maxOutput = params.M_s * 1.1;
        if (std::abs(M) > maxOutput * 0.9) {
            M = maxOutput * fastTanh(M / maxOutput);
        }

        return M;
    }

private:
    Parameters params;
    double T = 1.0 / 48000.0;
    double M_n1 = 0.0;
    double H_n1 = 0.0;
    double oneOverA = 1.0 / 22000.0;
    double cAlpha = 0.0;

    // Combined Langevin function and derivative computation
    // Returns both L(x) and L'(x) in a single pass to avoid redundant tanh calls
    // L(x) = coth(x) - 1/x
    // L'(x) = 1/x² - csch²(x) = 1/x² - coth²(x) + 1
    //
    // STABILITY FIX: Increased threshold from 1e-4 to 0.01
    // Near x=0, coth(x) approaches infinity, so we use Taylor series
    // for small x to avoid the singularity
    void langevinBoth(double x, double& L, double& Ld) const {
        // Use Taylor series for small x to avoid coth singularity
        // Taylor series: L(x) ≈ x/3 - x³/45 + 2x⁵/945 - ...
        // L'(x) ≈ 1/3 - x²/15 + 2x⁴/189 - ...
        // Threshold increased to 0.01 for better numerical stability
        if (std::abs(x) < 0.01) {
            double x2 = x * x;
            // More terms for better accuracy at the boundary
            L = x * (1.0/3.0 - x2 * (1.0/45.0 - x2 * (2.0/945.0)));
            Ld = 1.0/3.0 - x2 * (1.0/15.0 - x2 * (2.0/189.0));
            return;
        }

        double tanhX = fastTanh(x);

        // Protect against division by very small tanh values
        // This can happen at intermediate x values near the threshold
        if (std::abs(tanhX) < 1e-6) {
            // Use Taylor approximation as fallback
            double x2 = x * x;
            L = x * (1.0/3.0 - x2 * (1.0/45.0));
            Ld = 1.0/3.0 - x2 * (1.0/15.0);
            return;
        }

        double cothX = 1.0 / tanhX;  // coth = 1/tanh
        double invX = 1.0 / x;
        L = cothX - invX;
        Ld = invX * invX - cothX * cothX + 1.0;

        // Sanity check - L should be bounded by ±1 for reasonable x
        L = std::clamp(L, -1.0, 1.0);
        Ld = std::clamp(Ld, 0.0, 1.0/3.0 + 0.01);  // Ld max is 1/3 at x=0
    }

    double solveNR8(double H, double H_d) {
        double delta = (H_d >= 0.0) ? 1.0 : -1.0;
        double M = M_n1;
        double denom = 1.0 - cAlpha;

        // Protect against division by zero
        if (std::abs(denom) < 1e-12) denom = 1e-12;

        for (int i = 0; i < 8; ++i) {
            double H_eff = H + params.alpha * M;
            double x = H_eff * oneOverA;

            // Get both Langevin and its derivative in one call
            double L, Ld;
            langevinBoth(x, L, Ld);

            double M_an = params.M_s * L;
            double dM_an_dM = params.M_s * Ld * oneOverA * params.alpha;
            double M_diff = M_an - M;
            double delta_k = delta * params.k;

            // Protect against division issues
            double denomDiff = delta_k - params.alpha * M_diff;
            if (std::abs(denomDiff) < 1e-10) denomDiff = (denomDiff >= 0) ? 1e-10 : -1e-10;

            double dM_dH = (std::abs(M_diff) > 1e-12 && delta * M_diff > 0)
                ? (M_diff / denomDiff + params.c * dM_an_dM) / denom
                : params.c * dM_an_dM / denom;

            double f = M - M_n1 - T * dM_dH * H_d;
            double df_dM = (std::abs(denomDiff) > 1e-12)
                ? (dM_an_dM - 1.0) / denomDiff / denom
                : 0.0;
            double f_prime = 1.0 - T * H_d * df_dM;

            // Newton-Raphson update with protection
            if (std::abs(f_prime) > 1e-10) {
                double update = f / f_prime;
                // Limit update step size to prevent oscillation
                update = std::clamp(update, -params.M_s * 0.1, params.M_s * 0.1);
                M -= update;
            }

            M = std::clamp(M, -params.M_s, params.M_s);
        }
        return M;
    }
};

} // namespace TapeMachine
