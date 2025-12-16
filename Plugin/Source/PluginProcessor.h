#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <random>
#include <chrono>
#include "DSP/HybridTapeProcessor.h"

// Math constants for filter calculations (float precision for JUCE compatibility)
namespace PluginConstants
{
    constexpr float PI_F = 3.14159265f;
    constexpr float TWO_PI_F = 6.28318530718f;
    constexpr float BUTTERWORTH_Q_F = 0.707f;
}

//==============================================================================
/**
 * Ampex ATR-102 | Studer A820ulator Plugin
 *
 * Wraps the HybridTapeProcessor for use as a VST3/AU plugin.
 *
 * Features:
 * - Machine mode selection (Ampex ATR-102 vs Studer A820)
 * - Input trim control
 * - Auto gain compensation on/off
 * - Zero latency
 * - Stereo processing (independent L/R channels)
 */
class TapeMachinePluginSimulatorAudioProcessor : public juce::AudioProcessor,
                                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    TapeMachinePluginSimulatorAudioProcessor();
    ~TapeMachinePluginSimulatorAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter IDs
    static constexpr const char* PARAM_MACHINE_MODE = "machineMode";
    static constexpr const char* PARAM_TAPE_FORMULA = "tapeFormula";
    static constexpr const char* PARAM_INPUT_TRIM = "inputTrim";
    static constexpr const char* PARAM_OUTPUT_TRIM = "outputTrim";

    // Access to parameter tree state
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // Get current output level in dB for metering
    float getCurrentLevelDB() const { return currentLevelDB.load(); }

private:
    //==============================================================================
    // Parameter creation helper
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter tree state
    juce::AudioProcessorValueTreeState parameters;

    // DSP processors (stereo - one per channel)
    TapeMachine::HybridTapeProcessor tapeProcessorLeft;
    TapeMachine::HybridTapeProcessor tapeProcessorRight;

    // Atomic parameter pointers for efficient access in process block
    std::atomic<float>* machineModeParam = nullptr;
    std::atomic<float>* tapeFormulaParam = nullptr;
    std::atomic<float>* inputTrimParam = nullptr;
    std::atomic<float>* outputTrimParam = nullptr;

    // Level metering
    std::atomic<float> currentLevelDB { -96.0f };

    // 2x Minimum Phase Oversampling
    // Automatically disabled at sample rates >= 88.2kHz (already sufficient headroom)
    // Uses JUCE's IIR half-band polyphase filters for minimum phase response
    using Oversampler = juce::dsp::Oversampling<float>;
    std::unique_ptr<Oversampler> oversampler;
    bool useOversampling = true;  // False when sampleRate >= 88200

    // Crosstalk filter for Studer mode
    // Simulates adjacent track bleed on 24-track tape machines
    // Bandpassed mono signal mixed at -50dB into both channels
    struct CrosstalkFilter
    {
        // Simple biquad for HP and LP
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
            float a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;

            void reset() { z1 = z2 = 0.0f; }

            float process(float input)
            {
                float output = b0 * input + z1;
                z1 = b1 * input - a1 * output + z2;
                z2 = b2 * input - a2 * output;
                return output;
            }

            void setHighPass(float fc, float Q, float sampleRate)
            {
                float w0 = PluginConstants::TWO_PI_F * fc / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * Q);
                float a0 = 1.0f + alpha;
                b0 = ((1.0f + cosw0) / 2.0f) / a0;
                b1 = (-(1.0f + cosw0)) / a0;
                b2 = ((1.0f + cosw0) / 2.0f) / a0;
                a1 = (-2.0f * cosw0) / a0;
                a2 = (1.0f - alpha) / a0;
            }

            void setLowPass(float fc, float Q, float sampleRate)
            {
                float w0 = PluginConstants::TWO_PI_F * fc / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * Q);
                float a0 = 1.0f + alpha;
                b0 = ((1.0f - cosw0) / 2.0f) / a0;
                b1 = (1.0f - cosw0) / a0;
                b2 = ((1.0f - cosw0) / 2.0f) / a0;
                a1 = (-2.0f * cosw0) / a0;
                a2 = (1.0f - alpha) / a0;
            }
        };

        Biquad highpass;  // ~100Hz HP
        Biquad lowpass;   // ~8kHz LP
        float gain = 0.00178f;  // -55dB (Studer A820 spec: >55dB stereo crosstalk)

        void prepare(float sampleRate)
        {
            highpass.setHighPass(100.0f, 0.707f, sampleRate);
            lowpass.setLowPass(8000.0f, 0.707f, sampleRate);
            reset();
        }

        void reset()
        {
            highpass.reset();
            lowpass.reset();
        }

        float process(float monoInput)
        {
            float filtered = highpass.process(monoInput);
            filtered = lowpass.process(filtered);
            return filtered * gain;
        }
    };

    CrosstalkFilter crosstalkFilter;

    // Wow Modulator - True pitch-based wow via modulated delay line
    // Real tape wow is frequency modulation from transport speed variations
    // Only active for Studer A820 - ATR-102 has servo-controlled transport with negligible wow
    struct WowModulator
    {
        // Delay buffer for interpolated delay line
        static constexpr int MAX_DELAY_SAMPLES = 512;  // ~10ms at 48kHz, plenty for subtle wow
        float delayBufferL[MAX_DELAY_SAMPLES] = {0};
        float delayBufferR[MAX_DELAY_SAMPLES] = {0};
        int writeIndex = 0;

        // LFO phases (3 incommensurate frequencies for organic feel)
        float phase1 = 0.0f;
        float phase2 = 0.0f;
        float phase3 = 0.0f;
        float initialPhase1 = 0.0f;
        float initialPhase2 = 0.0f;
        float initialPhase3 = 0.0f;

        // LFO frequencies (Hz) - slow wow rates typical of multitrack transports
        static constexpr float freq1 = 0.5f;    // Primary capstan wow
        static constexpr float freq2 = 0.83f;   // Reel motor variation
        static constexpr float freq3 = 0.23f;   // Slow drift

        float sampleRate = 48000.0f;
        float baseDelaySamples = 0.0f;      // Center delay point
        float modulationDepthSamples = 0.0f; // Max deviation in samples
        bool enabled = false;

        WowModulator()
        {
            // Randomize LFO phases for unique behavior per plugin instance
            try
            {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_real_distribution<float> dist(0.0f, PluginConstants::TWO_PI_F);
                initialPhase1 = dist(gen);
                initialPhase2 = dist(gen);
                initialPhase3 = dist(gen);
            }
            catch (...)
            {
                auto seed = static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count());
                std::mt19937 gen(seed);
                std::uniform_real_distribution<float> dist(0.0f, PluginConstants::TWO_PI_F);
                initialPhase1 = dist(gen);
                initialPhase2 = dist(gen);
                initialPhase3 = dist(gen);
            }
            phase1 = initialPhase1;
            phase2 = initialPhase2;
            phase3 = initialPhase3;
        }

        void prepare(float sr, bool isAmpex)
        {
            sampleRate = sr;

            if (isAmpex)
            {
                // Ampex ATR-102: Servo-controlled direct-drive capstan
                // "Nearly non-existent speed drift and ultra-low flutter"
                // Disable wow entirely for accuracy
                enabled = false;
                baseDelaySamples = 0.0f;
                modulationDepthSamples = 0.0f;
            }
            else
            {
                // Studer A820: Multitrack with heavier reels
                // Typical wow spec ~0.02% - very subtle pitch variation
                // 0.02% of 1kHz = 0.2Hz variation
                // At 48kHz, 0.02% speed change = ~0.01 samples variation per sample
                // We use a base delay of ~2ms with ±0.02% modulation
                enabled = true;
                baseDelaySamples = sampleRate * 0.002f;  // 2ms base delay (~96 samples at 48k)
                // 0.02% wow = base_delay * 0.0002 modulation depth
                modulationDepthSamples = baseDelaySamples * 0.0004f;  // ~0.04 samples at 48k
            }

            reset();
        }

        void reset()
        {
            for (int i = 0; i < MAX_DELAY_SAMPLES; ++i)
            {
                delayBufferL[i] = 0.0f;
                delayBufferR[i] = 0.0f;
            }
            writeIndex = 0;
            phase1 = initialPhase1;
            phase2 = initialPhase2;
            phase3 = initialPhase3;
        }

        // Process stereo sample with wow modulation
        void processSample(float& left, float& right)
        {
            if (!enabled)
                return;

            // Update LFO phases (per-sample for smooth modulation)
            float phaseInc = PluginConstants::TWO_PI_F / sampleRate;
            phase1 += freq1 * phaseInc;
            phase2 += freq2 * phaseInc;
            phase3 += freq3 * phaseInc;

            if (phase1 > PluginConstants::TWO_PI_F) phase1 -= PluginConstants::TWO_PI_F;
            if (phase2 > PluginConstants::TWO_PI_F) phase2 -= PluginConstants::TWO_PI_F;
            if (phase3 > PluginConstants::TWO_PI_F) phase3 -= PluginConstants::TWO_PI_F;

            // Combine LFOs with different weights
            float lfo = std::sin(phase1) * 0.5f +
                        std::sin(phase2) * 0.3f +
                        std::sin(phase3) * 0.2f;

            // Calculate modulated delay time
            float delaySamples = baseDelaySamples + lfo * modulationDepthSamples;

            // Write current samples to delay buffer
            delayBufferL[writeIndex] = left;
            delayBufferR[writeIndex] = right;

            // Calculate read position with linear interpolation
            float readPosFloat = static_cast<float>(writeIndex) - delaySamples;
            if (readPosFloat < 0.0f) readPosFloat += MAX_DELAY_SAMPLES;

            int readPos0 = static_cast<int>(readPosFloat);
            int readPos1 = (readPos0 + 1) % MAX_DELAY_SAMPLES;
            float frac = readPosFloat - static_cast<float>(readPos0);
            readPos0 = readPos0 % MAX_DELAY_SAMPLES;

            // Linear interpolation for smooth delay modulation
            left = delayBufferL[readPos0] * (1.0f - frac) + delayBufferL[readPos1] * frac;
            right = delayBufferR[readPos0] * (1.0f - frac) + delayBufferR[readPos1] * frac;

            // Advance write index
            writeIndex = (writeIndex + 1) % MAX_DELAY_SAMPLES;
        }
    };

    WowModulator wowModulator;

    // Channel Tolerance EQ - models subtle frequency response variations
    // between tape heads/channels due to manufacturing tolerances
    // Based on Studer A820 specs: ±1dB from 60Hz-20kHz, ±2dB at extremes
    // We use conservative values: ±0.3dB low shelf, ±0.4dB high shelf
    struct ToleranceEQ
    {
        // Biquad shelving filters
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
            float a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;

            void reset() { z1 = z2 = 0.0f; }

            float process(float input)
            {
                float output = b0 * input + z1;
                z1 = b1 * input - a1 * output + z2;
                z2 = b2 * input - a2 * output;
                return output;
            }

            void setLowShelf(float fc, float gainDB, float Q, float sampleRate)
            {
                float A = std::pow(10.0f, gainDB / 40.0f);
                float omega = PluginConstants::TWO_PI_F * fc / sampleRate;
                float cosOmega = std::cos(omega);
                float sinOmega = std::sin(omega);
                float alpha = sinOmega / (2.0f * Q);

                float a0 = (A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha;
                b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha)) / a0;
                b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosOmega)) / a0;
                b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha)) / a0;
                a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosOmega)) / a0;
                a2 = ((A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha) / a0;
            }

            void setHighShelf(float fc, float gainDB, float Q, float sampleRate)
            {
                float A = std::pow(10.0f, gainDB / 40.0f);
                float omega = PluginConstants::TWO_PI_F * fc / sampleRate;
                float cosOmega = std::cos(omega);
                float sinOmega = std::sin(omega);
                float alpha = sinOmega / (2.0f * Q);

                float a0 = (A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha;
                b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha)) / a0;
                b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosOmega)) / a0;
                b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha)) / a0;
                a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosOmega)) / a0;
                a2 = ((A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha) / a0;
            }
        };

        // Per-channel filters (L and R can have different tolerances)
        Biquad lowShelfL, highShelfL;
        Biquad lowShelfR, highShelfR;

        // Randomized parameters (set once at construction)
        float lowFreqL = 70.0f, lowFreqR = 70.0f;     // ~70Hz ±10Hz
        float highFreqL = 15000.0f, highFreqR = 15000.0f;  // ~15kHz ±1kHz
        float lowGainL = 0.0f, lowGainR = 0.0f;       // ±0.3dB
        float highGainL = 0.0f, highGainR = 0.0f;     // ±0.4dB

        float sampleRate = 48000.0f;
        bool isStereo = true;  // If false, L and R use same random values

        // Machine type for tolerance differences
        bool isAmpex = true;

        // Constructor randomizes tolerances per instance
        // Called once at plugin instantiation - generates random offsets
        // Actual filter coefficients set in prepare() based on machine type
        ToleranceEQ()
        {
            // Use random_device with fallback for Windows compatibility
            // On some Windows systems, std::random_device may throw or be deterministic
            std::mt19937 gen;
            try
            {
                std::random_device rd;
                gen.seed(rd());
            }
            catch (...)
            {
                // Fallback: use time-based seed if random_device fails
                auto seed = static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count());
                gen.seed(seed);
            }

            // Generate normalized random values (-1 to +1)
            // These get scaled by machine-specific tolerances in prepare()
            std::uniform_real_distribution<float> normalizedDist(-1.0f, 1.0f);

            // Store normalized random values for later scaling
            lowFreqL = normalizedDist(gen);
            lowGainL = normalizedDist(gen);
            highFreqL = normalizedDist(gen);
            highGainL = normalizedDist(gen);

            lowFreqR = normalizedDist(gen);
            lowGainR = normalizedDist(gen);
            highFreqR = normalizedDist(gen);
            highGainR = normalizedDist(gen);
        }

        void prepare(float sr, bool stereoMode, bool ampexMode)
        {
            sampleRate = sr;
            isStereo = stereoMode;
            isAmpex = ampexMode;

            // Machine-specific tolerances for freshly calibrated machines
            // Ampex ATR-102: Precision 2-track mastering deck, tighter tolerances
            // Studer A820: Multitrack, slightly more channel variation
            float lowFreqCenter, lowFreqRange, lowGainRange;
            float highFreqCenter, highFreqRange, highGainRange;

            if (isAmpex)
            {
                // Ampex ATR-102: Precision 2-track mastering deck
                // "Most accurate analogue tape recorder ever produced"
                // User reports: ±0.5dB 35Hz-15kHz, flat 100Hz-10kHz
                // Freshly calibrated = extremely tight tolerances
                lowFreqCenter = 50.0f;
                lowFreqRange = 8.0f;        // ±8Hz variation
                lowGainRange = 0.05f;       // ±0.05dB (max 0.1dB L/R diff)
                highFreqCenter = 15000.0f;
                highFreqRange = 500.0f;     // ±500Hz variation
                highGainRange = 0.02f;      // ±0.02dB (essentially flat)
            }
            else
            {
                // Studer A820: Multitrack
                // Spec: ±1dB from 60-20kHz @ 30ips (theaudioarchive.com)
                // Freshly calibrated multitrack still has more variation than 2-track
                lowFreqCenter = 70.0f;
                lowFreqRange = 12.0f;       // ±12Hz variation
                lowGainRange = 0.15f;       // ±0.15dB (max 0.3dB L/R diff)
                highFreqCenter = 12000.0f;
                highFreqRange = 1000.0f;    // ±1kHz variation
                highGainRange = 0.05f;      // ±0.05dB (max 0.1dB L/R diff)
            }

            // Scale the normalized random values (-1 to +1) to actual tolerances
            float actualLowFreqL = lowFreqCenter + lowFreqL * lowFreqRange;
            float actualLowGainL = lowGainL * lowGainRange;
            float actualHighFreqL = highFreqCenter + highFreqL * highFreqRange;
            float actualHighGainL = highGainL * highGainRange;

            float actualLowFreqR = lowFreqCenter + lowFreqR * lowFreqRange;
            float actualLowGainR = lowGainR * lowGainRange;
            float actualHighFreqR = highFreqCenter + highFreqR * highFreqRange;
            float actualHighGainR = highGainR * highGainRange;

            // Q of 1.0 gives steeper shelf transition than Butterworth (0.707)
            // This keeps 10-15kHz more consistent between L/R channels
            float Q = 1.0f;

            if (isStereo)
            {
                // Stereo: L and R have independent random tolerances
                lowShelfL.setLowShelf(actualLowFreqL, actualLowGainL, Q, sampleRate);
                highShelfL.setHighShelf(actualHighFreqL, actualHighGainL, Q, sampleRate);
                lowShelfR.setLowShelf(actualLowFreqR, actualLowGainR, Q, sampleRate);
                highShelfR.setHighShelf(actualHighFreqR, actualHighGainR, Q, sampleRate);
            }
            else
            {
                // Mono: L and R use same tolerance (L values)
                lowShelfL.setLowShelf(actualLowFreqL, actualLowGainL, Q, sampleRate);
                highShelfL.setHighShelf(actualHighFreqL, actualHighGainL, Q, sampleRate);
                lowShelfR.setLowShelf(actualLowFreqL, actualLowGainL, Q, sampleRate);
                highShelfR.setHighShelf(actualHighFreqL, actualHighGainL, Q, sampleRate);
            }

            reset();
        }

        void reset()
        {
            lowShelfL.reset();
            highShelfL.reset();
            lowShelfR.reset();
            highShelfR.reset();
        }

        void processSample(float& left, float& right)
        {
            left = lowShelfL.process(left);
            left = highShelfL.process(left);
            right = lowShelfR.process(right);
            right = highShelfR.process(right);
        }
    };

    ToleranceEQ toleranceEQ;

    // Print-Through (Studer mode only)
    // Simulates magnetic bleed between tape layers on the reel
    // Creates subtle pre-echo ~65ms before the main signal
    // Signal-dependent: louder signals create stronger magnetic bleed
    // Real print-through is proportional to the recorded flux level
    struct PrintThrough
    {
        // Delay buffer for 65ms at up to 192kHz
        static constexpr int MAX_DELAY_SAMPLES = 12480;  // 65ms @ 192kHz
        float bufferL[MAX_DELAY_SAMPLES] = {0};
        float bufferR[MAX_DELAY_SAMPLES] = {0};
        int writeIndex = 0;
        int delaySamples = 0;

        // Base print-through coefficient (scales with signal level)
        // At unity (0dBFS), this gives approximately -58dB of print-through
        // GP9 tape has ~3dB less print-through than older formulations (456)
        // Quieter signals produce proportionally less print-through
        static constexpr float printCoeff = 0.00126f;  // -58dB at unity (GP9 spec)

        // Minimum threshold - signals below this won't produce audible print-through
        // Prevents noise floor from creating constant low-level artifacts
        static constexpr float noiseFloor = 0.001f;  // -60dB

        float sampleRate = 48000.0f;

        void prepare(float sr)
        {
            sampleRate = sr;
            // 65ms delay for 30 IPS tape layer spacing
            delaySamples = static_cast<int>(0.065f * sampleRate);
            if (delaySamples >= MAX_DELAY_SAMPLES)
                delaySamples = MAX_DELAY_SAMPLES - 1;
            reset();
        }

        void reset()
        {
            for (int i = 0; i < MAX_DELAY_SAMPLES; ++i)
            {
                bufferL[i] = 0.0f;
                bufferR[i] = 0.0f;
            }
            writeIndex = 0;
        }

        void processSample(float& left, float& right)
        {
            // Read from delay buffer (pre-echo from 65ms ago)
            int readIndex = writeIndex - delaySamples;
            if (readIndex < 0) readIndex += MAX_DELAY_SAMPLES;

            float delayedL = bufferL[readIndex];
            float delayedR = bufferR[readIndex];

            // Signal-dependent print-through:
            // The amount of magnetic bleed is proportional to the recorded signal level
            // Louder passages create stronger magnetization, hence more print-through
            float absL = std::abs(delayedL);
            float absR = std::abs(delayedR);

            // Apply soft knee above noise floor for natural response
            // Print level scales quadratically with amplitude (magnetic flux relationship)
            float printLevelL = (absL > noiseFloor) ? printCoeff * absL : 0.0f;
            float printLevelR = (absR > noiseFloor) ? printCoeff * absR : 0.0f;

            float preEchoL = delayedL * printLevelL;
            float preEchoR = delayedR * printLevelR;

            // Write current sample to delay buffer
            bufferL[writeIndex] = left;
            bufferR[writeIndex] = right;

            // Advance write index
            writeIndex = (writeIndex + 1) % MAX_DELAY_SAMPLES;

            // Mix pre-echo into output
            left += preEchoL;
            right += preEchoR;
        }
    };

    PrintThrough printThrough;

    // Track machine mode and tape formula changes (per-instance, not static)
    int lastMachineMode = -1;
    int lastTapeFormula = -1;

    // Auto-gain: Track the last input trim to detect changes
    float lastInputTrimValue = 1.0f;  // Default 0dB
    bool isUpdatingOutputTrim = false;  // Prevent listener recursion

    // Parameter listener callback
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeMachinePluginSimulatorAudioProcessor)
};
