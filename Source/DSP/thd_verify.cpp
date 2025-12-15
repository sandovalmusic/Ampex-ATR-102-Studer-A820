/**
 * THD Measurement Verification
 * Tests the Goertzel-based THD measurement against known signals
 *
 * Compile: clang++ -std=c++17 -O2 -o thd_verify thd_verify.cpp
 * Run: ./thd_verify
 */

#include <cmath>
#include <vector>
#include <iostream>
#include <iomanip>

constexpr double PI = 3.14159265358979323846;
constexpr double SAMPLE_RATE = 96000.0;
constexpr int NUM_SAMPLES = 8192;

/**
 * Goertzel algorithm for measuring amplitude at specific frequency
 */
double measureAmplitude(const std::vector<double>& signal, double freq, double fs)
{
    int N = static_cast<int>(signal.size());
    double k = freq * N / fs;
    double w = 2.0 * PI * k / N;
    double cosw = std::cos(w);
    double sinw = std::sin(w);
    double coeff = 2.0 * cosw;

    double s0 = 0.0, s1 = 0.0, s2 = 0.0;

    // Apply Hann window
    for (int i = 0; i < N; ++i) {
        double window = 0.5 * (1.0 - std::cos(2.0 * PI * i / N));
        s0 = signal[i] * window + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    double real = s1 - s2 * cosw;
    double imag = s2 * sinw;
    double magnitude = std::sqrt(real * real + imag * imag);

    // Normalize: DFT gives N/2 for unit sine, Hann window has 0.5 coherent gain
    return (2.0 * magnitude) / (N * 0.5);
}

/**
 * Calculate THD from harmonic amplitudes
 */
double calculateTHD(double fundamental, double h2, double h3, double h4, double h5)
{
    double sumSquares = h2*h2 + h3*h3 + h4*h4 + h5*h5;
    return (std::sqrt(sumSquares) / fundamental) * 100.0;
}

void testPureSine()
{
    std::cout << "=== Test 1: Pure Sine Wave ===\n";
    std::cout << "Expected: ~0% THD (measurement noise floor)\n\n";

    double freq = 1000.0;
    double amplitude = 1.0;

    std::vector<double> signal(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        signal[i] = amplitude * std::sin(2.0 * PI * freq * i / SAMPLE_RATE);
    }

    double f1 = measureAmplitude(signal, freq, SAMPLE_RATE);
    double h2 = measureAmplitude(signal, freq * 2, SAMPLE_RATE);
    double h3 = measureAmplitude(signal, freq * 3, SAMPLE_RATE);
    double h4 = measureAmplitude(signal, freq * 4, SAMPLE_RATE);
    double h5 = measureAmplitude(signal, freq * 5, SAMPLE_RATE);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Input amplitude: " << amplitude << "\n";
    std::cout << "Measured fundamental: " << f1 << " (error: " << (f1 - amplitude) * 100 << "%)\n";
    std::cout << "H2: " << h2 << ", H3: " << h3 << ", H4: " << h4 << ", H5: " << h5 << "\n";
    std::cout << "THD: " << calculateTHD(f1, h2, h3, h4, h5) << "%\n\n";
}

void testKnownDistortion()
{
    std::cout << "=== Test 2: Known Distortion ===\n";
    std::cout << "Signal: 1.0*sin(f) + 0.01*sin(2f) + 0.005*sin(3f)\n";
    std::cout << "Expected THD: sqrt(0.01² + 0.005²) / 1.0 * 100 = 1.118%\n\n";

    double freq = 1000.0;
    double a1 = 1.0, a2 = 0.01, a3 = 0.005;

    std::vector<double> signal(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        double t = 2.0 * PI * freq * i / SAMPLE_RATE;
        signal[i] = a1 * std::sin(t) + a2 * std::sin(2*t) + a3 * std::sin(3*t);
    }

    double f1 = measureAmplitude(signal, freq, SAMPLE_RATE);
    double h2 = measureAmplitude(signal, freq * 2, SAMPLE_RATE);
    double h3 = measureAmplitude(signal, freq * 3, SAMPLE_RATE);
    double h4 = measureAmplitude(signal, freq * 4, SAMPLE_RATE);
    double h5 = measureAmplitude(signal, freq * 5, SAMPLE_RATE);

    double expectedTHD = std::sqrt(a2*a2 + a3*a3) / a1 * 100.0;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Measured fundamental: " << f1 << " (expected: " << a1 << ")\n";
    std::cout << "Measured H2: " << h2 << " (expected: " << a2 << ")\n";
    std::cout << "Measured H3: " << h3 << " (expected: " << a3 << ")\n";
    std::cout << "Measured THD: " << calculateTHD(f1, h2, h3, h4, h5) << "%\n";
    std::cout << "Expected THD: " << expectedTHD << "%\n";
    std::cout << "Error: " << (calculateTHD(f1, h2, h3, h4, h5) - expectedTHD) << "%\n\n";
}

void testHighDistortion()
{
    std::cout << "=== Test 3: High Distortion (10% THD) ===\n";
    std::cout << "Signal: 1.0*sin(f) + 0.08*sin(2f) + 0.06*sin(3f)\n";
    std::cout << "Expected THD: sqrt(0.08² + 0.06²) / 1.0 * 100 = 10%\n\n";

    double freq = 1000.0;
    double a1 = 1.0, a2 = 0.08, a3 = 0.06;

    std::vector<double> signal(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        double t = 2.0 * PI * freq * i / SAMPLE_RATE;
        signal[i] = a1 * std::sin(t) + a2 * std::sin(2*t) + a3 * std::sin(3*t);
    }

    double f1 = measureAmplitude(signal, freq, SAMPLE_RATE);
    double h2 = measureAmplitude(signal, freq * 2, SAMPLE_RATE);
    double h3 = measureAmplitude(signal, freq * 3, SAMPLE_RATE);
    double h4 = measureAmplitude(signal, freq * 4, SAMPLE_RATE);
    double h5 = measureAmplitude(signal, freq * 5, SAMPLE_RATE);

    double expectedTHD = std::sqrt(a2*a2 + a3*a3) / a1 * 100.0;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Measured fundamental: " << f1 << " (expected: " << a1 << ")\n";
    std::cout << "Measured H2: " << h2 << " (expected: " << a2 << ")\n";
    std::cout << "Measured H3: " << h3 << " (expected: " << a3 << ")\n";
    std::cout << "Measured THD: " << calculateTHD(f1, h2, h3, h4, h5) << "%\n";
    std::cout << "Expected THD: " << expectedTHD << "%\n\n";
}

void testCubicSaturation()
{
    std::cout << "=== Test 4: Cubic Saturation y = x - 0.1*x³ ===\n";
    std::cout << "For cubic: THD3 ≈ (3/4) * a3 * A² where A=amplitude, a3=0.1\n";
    std::cout << "At A=1.0: THD3 ≈ 0.75 * 0.1 * 1.0 = 7.5%\n\n";

    double freq = 1000.0;
    double amplitude = 1.0;
    double a3_coeff = 0.1;

    std::vector<double> signal(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        double x = amplitude * std::sin(2.0 * PI * freq * i / SAMPLE_RATE);
        signal[i] = x - a3_coeff * x * x * x;  // Cubic saturation
    }

    double f1 = measureAmplitude(signal, freq, SAMPLE_RATE);
    double h2 = measureAmplitude(signal, freq * 2, SAMPLE_RATE);
    double h3 = measureAmplitude(signal, freq * 3, SAMPLE_RATE);
    double h4 = measureAmplitude(signal, freq * 4, SAMPLE_RATE);
    double h5 = measureAmplitude(signal, freq * 5, SAMPLE_RATE);

    // Theoretical: cubic produces only odd harmonics
    // Third harmonic amplitude ≈ (1/4) * a3 * A³ for y = x - a3*x³
    double theoreticalH3 = 0.25 * a3_coeff * amplitude * amplitude * amplitude;
    double theoreticalTHD = (theoreticalH3 / amplitude) * 100.0;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Measured fundamental: " << f1 << "\n";
    std::cout << "Measured H2: " << h2 << " (should be ~0 for pure cubic)\n";
    std::cout << "Measured H3: " << h3 << " (theoretical: " << theoreticalH3 << ")\n";
    std::cout << "Measured THD: " << calculateTHD(f1, h2, h3, h4, h5) << "%\n";
    std::cout << "Theoretical THD3: " << theoreticalTHD << "%\n\n";
}

void testBiasedCubic()
{
    std::cout << "=== Test 5: Biased Cubic y = (x+b) - a3*(x+b)³ (generates H2) ===\n";
    std::cout << "Bias adds even harmonics via asymmetry\n\n";

    double freq = 1000.0;
    double amplitude = 1.0;
    double a3_coeff = 0.1;
    double bias = 0.1;

    std::vector<double> signal(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        double x = amplitude * std::sin(2.0 * PI * freq * i / SAMPLE_RATE);
        double biased = x + bias;
        signal[i] = biased - a3_coeff * biased * biased * biased - bias;  // Remove DC
    }

    double f1 = measureAmplitude(signal, freq, SAMPLE_RATE);
    double h2 = measureAmplitude(signal, freq * 2, SAMPLE_RATE);
    double h3 = measureAmplitude(signal, freq * 3, SAMPLE_RATE);
    double h4 = measureAmplitude(signal, freq * 4, SAMPLE_RATE);
    double h5 = measureAmplitude(signal, freq * 5, SAMPLE_RATE);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Measured fundamental: " << f1 << "\n";
    std::cout << "Measured H2: " << h2 << " (bias-generated even harmonic)\n";
    std::cout << "Measured H3: " << h3 << "\n";
    std::cout << "E/O Ratio (H2/H3): " << h2/h3 << "\n";
    std::cout << "Measured THD: " << calculateTHD(f1, h2, h3, h4, h5) << "%\n\n";
}

int main()
{
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       THD Measurement Verification                        ║\n";
    std::cout << "║       Sample Rate: 96kHz, Samples: 8192                   ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";

    testPureSine();
    testKnownDistortion();
    testHighDistortion();
    testCubicSaturation();
    testBiasedCubic();

    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "If all tests pass, the THD measurement is working correctly.\n";

    return 0;
}
