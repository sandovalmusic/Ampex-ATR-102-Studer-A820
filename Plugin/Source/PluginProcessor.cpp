#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>  // for std::clamp

//==============================================================================
TapeMachinePluginSimulatorAudioProcessor::TapeMachinePluginSimulatorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#else
    :
#endif
      parameters (*this, nullptr, juce::Identifier ("TapeMachinePluginSimulator"),
                  createParameterLayout())
{
    // Get atomic parameter pointers for efficient access
    machineModeParam = parameters.getRawParameterValue (PARAM_MACHINE_MODE);
    tapeFormulaParam = parameters.getRawParameterValue (PARAM_TAPE_FORMULA);
    inputTrimParam = parameters.getRawParameterValue (PARAM_INPUT_TRIM);
    outputTrimParam = parameters.getRawParameterValue (PARAM_OUTPUT_TRIM);

    // Register parameter listener for auto-gain linking and SharedInstanceManager updates
    parameters.addParameterListener (PARAM_INPUT_TRIM, this);
    parameters.addParameterListener (PARAM_OUTPUT_TRIM, this);
    parameters.addParameterListener (PARAM_MACHINE_MODE, this);
    lastInputTrimValue = 1.0f;  // Match default (0dB)

    // Register with shared instance manager for Master/Tracks communication
    // Use the initial parameter value (default is 0 = Master)
    int initialMode = static_cast<int>(*machineModeParam);
    sharedInstanceManager.registerInstance(initialMode, "");
    lastMachineMode = initialMode;  // Initialize to prevent spurious update

    // Initialize SharedInstanceManager with actual parameter values (not defaults)
    // This ensures Master mode sees correct values when Tracks instances start
    if (auto* driveParam = parameters.getParameter (PARAM_INPUT_TRIM))
    {
        if (auto* volumeParam = parameters.getParameter (PARAM_OUTPUT_TRIM))
        {
            float driveNorm = driveParam->getValue();  // Already normalized 0-1
            float volumeNorm = volumeParam->getValue();  // Already normalized 0-1
            sharedInstanceManager.updateParams (driveNorm, volumeNorm);
        }
    }
}

TapeMachinePluginSimulatorAudioProcessor::~TapeMachinePluginSimulatorAudioProcessor()
{
    parameters.removeParameterListener (PARAM_INPUT_TRIM, this);
    parameters.removeParameterListener (PARAM_OUTPUT_TRIM, this);
    parameters.removeParameterListener (PARAM_MACHINE_MODE, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
TapeMachinePluginSimulatorAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Machine Mode (0 = Master, 1 = Tracks)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        PARAM_MACHINE_MODE,
        "Machine Mode",
        juce::StringArray { "Master", "Tracks" },
        0  // Default: Master
    ));

    // Tape Formula (0 = GP9, 1 = SM900)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        PARAM_TAPE_FORMULA,
        "Tape Formula",
        juce::StringArray { "GP9", "SM900" },
        0  // Default: GP9
    ));

    // Input Trim (Drive): -12dB to +12dB, default 0dB = 1.0x
    // Range: 0.25x (-12dB) to 4.0x (+12dB), symmetric around 0dB
    // No skew needed - 0dB (1.0) is naturally at 50% of this symmetric range
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PARAM_INPUT_TRIM,
        "Input Trim",
        juce::NormalisableRange<float> (0.25f, 4.0f, 0.001f, 0.5f),
        1.0f,  // Default 0dB
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            float db = 20.0f * std::log10(value);
            return juce::String(db, 1) + " dB";
        }
    ));

    // Output Trim (Volume): -12dB to +12dB, default 0dB = 1.0x
    // Range: 0.25x (-12dB) to 4.0x (+12dB), symmetric around 0dB
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PARAM_OUTPUT_TRIM,
        "Output Trim",
        juce::NormalisableRange<float> (0.25f, 4.0f, 0.001f, 0.5f),
        1.0f,  // Default 0dB
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            float db = 20.0f * std::log10(value);
            return juce::String(db, 1) + " dB";
        }
    ));

    return layout;
}

//==============================================================================
const juce::String TapeMachinePluginSimulatorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TapeMachinePluginSimulatorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool TapeMachinePluginSimulatorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool TapeMachinePluginSimulatorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double TapeMachinePluginSimulatorAudioProcessor::getTailLengthSeconds() const
{
    return 0.05;  // 50ms tail for DC blocker and filter decay
}

int TapeMachinePluginSimulatorAudioProcessor::getNumPrograms()
{
    return 1;
}

int TapeMachinePluginSimulatorAudioProcessor::getCurrentProgram()
{
    return 0;
}

// Program/preset management - not implemented (single-preset plugin)
void TapeMachinePluginSimulatorAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused(index); }
const juce::String TapeMachinePluginSimulatorAudioProcessor::getProgramName (int index) { juce::ignoreUnused(index); return {}; }
void TapeMachinePluginSimulatorAudioProcessor::changeProgramName (int index, const juce::String& newName) { juce::ignoreUnused(index, newName); }

//==============================================================================
void TapeMachinePluginSimulatorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Disable oversampling at high sample rates (88.2kHz+)
    // At 96kHz+ native, Nyquist is already 48kHz+ providing adequate headroom
    // for saturation harmonics without the phase artifacts from decimation filter
    useOversampling = (sampleRate < 88200.0);

    if (useOversampling)
    {
        // Initialize 2x minimum phase oversampling
        // filterHalfBandPolyphaseIIR = minimum phase IIR filters (no linear phase latency)
        constexpr int oversamplingOrder = 1;  // 2^1 = 2x oversampling
        oversampler = std::make_unique<Oversampler> (
            2,  // numChannels (stereo)
            oversamplingOrder,
            Oversampler::filterHalfBandPolyphaseIIR,  // Minimum phase IIR
            false  // Not using maximum quality (faster)
        );
        oversampler->initProcessing (static_cast<size_t> (samplesPerBlock));

        // Report latency to DAW (oversampler adds some latency)
        setLatencySamples (static_cast<int> (oversampler->getLatencyInSamples()));

        // Initialize tape processors at OVERSAMPLED sample rate (2x)
        const double oversampledRate = sampleRate * 2.0;
        tapeProcessorLeft.setSampleRate (oversampledRate);
        tapeProcessorRight.setSampleRate (oversampledRate);
    }
    else
    {
        // High sample rate mode - no oversampling needed
        oversampler.reset();  // Release oversampler memory
        setLatencySamples (0);  // Zero latency at high sample rates

        // Initialize tape processors at native sample rate
        tapeProcessorLeft.setSampleRate (sampleRate);
        tapeProcessorRight.setSampleRate (sampleRate);
    }

    tapeProcessorLeft.reset();
    tapeProcessorRight.reset();

    // Set default Ampex ATR-102 parameters (Master mode)
    const double defaultBias = 0.65;
    tapeProcessorLeft.setParameters (defaultBias, 1.0);
    tapeProcessorRight.setParameters (defaultBias, 1.0);

    // Initialize crosstalk filter at base sample rate (applied after downsampling)
    crosstalkFilter.prepare (static_cast<float> (sampleRate));

    // Initialize wow modulator (default to Ampex/disabled, will update in processBlock)
    wowModulator.prepare (static_cast<float> (sampleRate), true);

    // Initialize tolerance EQ (randomized per instance)
    // Stereo mode = different tolerances per channel, Mono = same for both
    // Default to Ampex mode, will update in processBlock if needed
    bool isStereo = (getTotalNumInputChannels() >= 2);
    toleranceEQ.prepare (static_cast<float> (sampleRate), isStereo, true);

    // Initialize print-through (Studer mode only, but prepare always)
    printThrough.prepare (static_cast<float> (sampleRate));
}

void TapeMachinePluginSimulatorAudioProcessor::releaseResources()
{
    // Reset processors when playback stops
    tapeProcessorLeft.reset();
    tapeProcessorRight.reset();
    if (oversampler)
        oversampler->reset();
    crosstalkFilter.reset();
    wowModulator.reset();
    toleranceEQ.reset();
    printThrough.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TapeMachinePluginSimulatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Support mono and stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input and output layouts must match
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void TapeMachinePluginSimulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any output channels that don't have input
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Get parameter values
    const int machineMode = static_cast<int> (*machineModeParam);
    const int tapeFormula = static_cast<int> (*tapeFormulaParam);
    const float inputTrimValue = *inputTrimParam;
    const float outputTrimValue = *outputTrimParam;

    // Update processor parameters based on machine mode and tape formula
    // Machine Mode: Master (0) = Ampex ATR-102, Tracks (1) = Studer A820
    // Tape Formula: GP9 (0), SM900 (1)
    // The bias value determines which internal parameters are used (threshold at 0.74)
    const double bias = (machineMode == 0) ? 0.65 : 0.82;

    // Set processor parameters (input gain = 1.0, we apply drive externally via inputTrim)
    tapeProcessorLeft.setParameters (bias, 1.0, tapeFormula);
    tapeProcessorRight.setParameters (bias, 1.0, tapeFormula);

    const int numSamples = buffer.getNumSamples();
    float peakLevel = 0.0f;

    // Apply input trim (Drive) BEFORE oversampling and measure level for metering
    // Same input gain for both modes - user controls drive with the knob
    const float globalInputGain = 0.501f;  // -6dB for both modes

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* channelData = buffer.getWritePointer (ch);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            channelData[sample] *= inputTrimValue * globalInputGain;
            peakLevel = std::max (peakLevel, std::abs (channelData[sample]));
        }
    }

    // === TAPE PROCESSING ===
    // At high sample rates (88.2kHz+), process at native rate (no oversampling)
    // At lower sample rates, use 2x oversampling for anti-aliasing

    if (useOversampling)
    {
        // === OVERSAMPLING: Upsample to 2x rate ===
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::AudioBlock<float> oversampledBlock = oversampler->processSamplesUp (block);

        // Process at oversampled rate (2x sample rate)
        const int oversampledNumSamples = static_cast<int> (oversampledBlock.getNumSamples());

        for (int sample = 0; sample < oversampledNumSamples; ++sample)
        {
            // Process left channel
            float leftSample = oversampledBlock.getSample (0, sample);
            float leftProcessed = static_cast<float> (tapeProcessorLeft.processSample (leftSample));
            oversampledBlock.setSample (0, sample, leftProcessed);

            // Process right channel (with azimuth delay)
            if (oversampledBlock.getNumChannels() > 1)
            {
                float rightSample = oversampledBlock.getSample (1, sample);
                float rightProcessed = static_cast<float> (tapeProcessorRight.processRightChannel (rightSample));
                oversampledBlock.setSample (1, sample, rightProcessed);
            }
        }

        // === OVERSAMPLING: Downsample back to original rate ===
        oversampler->processSamplesDown (block);
    }
    else
    {
        // === HIGH SAMPLE RATE: Process at native rate (no oversampling) ===
        // At 96kHz+, Nyquist is 48kHz+ providing adequate headroom for saturation harmonics
        // Zero latency, no decimation filter phase artifacts

        float* leftData = buffer.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            leftData[sample] = static_cast<float> (tapeProcessorLeft.processSample (leftData[sample]));
        }

        if (totalNumInputChannels > 1)
        {
            float* rightData = buffer.getWritePointer (1);
            for (int sample = 0; sample < numSamples; ++sample)
            {
                rightData[sample] = static_cast<float> (tapeProcessorRight.processRightChannel (rightData[sample]));
            }
        }
    }

    // === POST-PROCESSING GAIN COMPENSATION ===
    // Tape processing has inherent gain changes - compensate to maintain unity
    // Measured at 0VU (-10dBFS): Ampex -0.25dB, Studer +0.20dB
    const float tapeGainComp = (machineMode == 0) ? 1.029f : 0.977f;
    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* channelData = buffer.getWritePointer (ch);
        for (int sample = 0; sample < numSamples; ++sample)
            channelData[sample] *= tapeGainComp;
    }

    // === CROSSTALK: Studer mode only ===
    // Simulates adjacent track bleed on 24-track tape machines
    // Adds bandpassed mono signal at -55dB to both channels
    if (machineMode == 1 && totalNumInputChannels >= 2)  // Studer mode, stereo only
    {
        float* leftData = buffer.getWritePointer (0);
        float* rightData = buffer.getWritePointer (1);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Sum to mono
            float mono = (leftData[sample] + rightData[sample]) * 0.5f;
            // Bandpass and attenuate
            float crosstalk = crosstalkFilter.process (mono);
            // Add to both channels
            leftData[sample] += crosstalk;
            rightData[sample] += crosstalk;
        }
    }

    // === WOW MODULATION: Studer only ===
    // True pitch-based wow via modulated delay line
    // Ampex ATR-102 has servo-controlled transport with negligible wow - disabled
    // Studer A820 multitrack has subtle wow (~0.02%) from heavier reels
    {
        // Update modulator and tolerance EQ settings if machine mode or tape formula changed
        // Note: lastMachineMode/lastTapeFormula are member variables, not static, to support multiple instances
        if (machineMode != lastMachineMode || tapeFormula != lastTapeFormula)
        {
            bool isAmpex = (machineMode == 0);
            wowModulator.prepare (static_cast<float> (getSampleRate()), isAmpex);
            toleranceEQ.prepare (static_cast<float> (getSampleRate()),
                                 totalNumInputChannels >= 2, isAmpex);

            // Reset all DSP components on mode/formula switch to prevent pops from filter state discontinuities
            // The tape processor's built-in 50ms fade-in will smoothly bring audio back
            tapeProcessorLeft.reset();
            tapeProcessorRight.reset();
            crosstalkFilter.reset();
            printThrough.reset();
            wowModulator.reset();
            toleranceEQ.reset();
            if (oversampler)
                oversampler->reset();

            // Update shared instance manager with new mode
            sharedInstanceManager.setMode(machineMode);

            lastMachineMode = machineMode;
            lastTapeFormula = tapeFormula;
        }

        // Apply wow modulation (per-sample processing with interpolated delay)
        if (totalNumInputChannels >= 2)
        {
            float* leftData = buffer.getWritePointer (0);
            float* rightData = buffer.getWritePointer (1);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                wowModulator.processSample (leftData[sample], rightData[sample]);
            }
        }
        else if (totalNumInputChannels == 1)
        {
            float* monoData = buffer.getWritePointer (0);
            float dummy = 0.0f;

            for (int sample = 0; sample < numSamples; ++sample)
            {
                wowModulator.processSample (monoData[sample], dummy);
            }
        }
    }

    // === TOLERANCE EQ: Both modes, machine-specific ===
    // Models subtle channel-to-channel frequency response variations
    // due to tape head manufacturing tolerances on freshly calibrated machines
    // Ampex ATR-102: ±0.10dB low (60Hz), ±0.12dB high (16kHz) - precision mastering
    // Studer A820:   ±0.15dB low (75Hz), ±0.18dB high (15kHz) - multitrack variation
    // Stereo instances get different L/R tolerances; mono instances use same for both
    {
        if (totalNumInputChannels >= 2)
        {
            float* leftData = buffer.getWritePointer (0);
            float* rightData = buffer.getWritePointer (1);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                toleranceEQ.processSample (leftData[sample], rightData[sample]);
            }
        }
        else if (totalNumInputChannels == 1)
        {
            float* monoData = buffer.getWritePointer (0);
            float dummy = 0.0f;

            for (int sample = 0; sample < numSamples; ++sample)
            {
                toleranceEQ.processSample (monoData[sample], dummy);
            }
        }
    }

    // === PRINT-THROUGH: Studer mode only ===
    // Simulates magnetic bleed between tape layers creating subtle pre-echo
    // Signal-dependent: louder passages create proportionally more print-through
    // Real-world multitrack tape (more layers, more print-through than 2-track)
    // 65ms delay represents tape layer spacing at 30 IPS
    if (machineMode == 1 && totalNumInputChannels >= 2)  // Studer mode, stereo only
    {
        float* leftData = buffer.getWritePointer (0);
        float* rightData = buffer.getWritePointer (1);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            printThrough.processSample (leftData[sample], rightData[sample]);
        }
    }
    else if (machineMode == 1 && totalNumInputChannels == 1)  // Studer mode, mono
    {
        float* monoData = buffer.getWritePointer (0);
        float dummy = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            printThrough.processSample (monoData[sample], dummy);
        }
    }

    // Apply output trim (Volume) and final makeup gain
    // finalMakeupGain is exact inverse of globalInputGain for unity gain
    const float finalMakeupGain = 1.0f / globalInputGain;  // Master: +22dB, Tracks: +18dB

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* channelData = buffer.getWritePointer (ch);
        for (int sample = 0; sample < numSamples; ++sample)
            channelData[sample] *= outputTrimValue * finalMakeupGain;
    }

    // Update meter level (convert to dB)
    float levelDB;
    if (peakLevel > 0.0001f)
        levelDB = 20.0f * std::log10 (peakLevel);
    else
        levelDB = -96.0f;
    currentLevelDB.store (levelDB);

    // Update shared instance manager with current level and params
    sharedInstanceManager.updateLevel (levelDB);
    sharedInstanceManager.updateHeartbeat();

    // Check for remote param updates from Master mode instances
    // This allows Master to control Tracks instances' Drive and Volume
    float remoteDrive, remoteVolume;
    if (sharedInstanceManager.checkForParamUpdates (remoteDrive, remoteVolume))
    {
        // Apply remote param changes
        // remoteDrive/remoteVolume are already normalized 0-1, which matches JUCE's internal format
        // We use setValueNotifyingHost directly with the normalized value - JUCE handles
        // the skew factor conversion internally when accessing the actual parameter value

        // Set flag to prevent auto-gain linking (but we still need to update SharedInstanceManager)
        isReceivingRemoteUpdate = true;

        if (auto* param = parameters.getParameter (PARAM_INPUT_TRIM))
            param->setValueNotifyingHost (remoteDrive);
        if (auto* param = parameters.getParameter (PARAM_OUTPUT_TRIM))
            param->setValueNotifyingHost (remoteVolume);

        // Also update lastInputTrimValue to prevent auto-gain from fighting
        // Convert normalized to actual value using the parameter's range
        if (auto* param = parameters.getParameter (PARAM_INPUT_TRIM))
            lastInputTrimValue = param->convertFrom0to1 (remoteDrive);

        isReceivingRemoteUpdate = false;

        // Update SharedInstanceManager with the new values so Master sees them
        // This completes the round-trip: Master sends -> Tracks receives -> Tracks echoes back
        sharedInstanceManager.updateParams (remoteDrive, remoteVolume);
    }
}

//==============================================================================
bool TapeMachinePluginSimulatorAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TapeMachinePluginSimulatorAudioProcessor::createEditor()
{
    return new TapeMachinePluginSimulatorAudioProcessorEditor (*this);
}

//==============================================================================
void TapeMachinePluginSimulatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Save parameter state
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TapeMachinePluginSimulatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore parameter state
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// Parameter listener callback for auto-gain linking
void TapeMachinePluginSimulatorAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == PARAM_INPUT_TRIM && !isUpdatingOutputTrim && !isReceivingRemoteUpdate)
    {
        // Auto-gain: When Drive (input trim) changes, adjust Output Trim to compensate
        // This keeps monitoring level constant while allowing saturation to increase
        //
        // Logic: If Drive goes from 0.5 to 2.0 (4x increase = +12dB),
        //        Output should go from current to current/4 (-12dB compensation)
        //
        // We calculate the RATIO of change and apply it inversely to output trim

        float ratio = lastInputTrimValue / newValue;  // Inverse ratio
        float currentOutputTrim = *outputTrimParam;
        float newOutputTrim = currentOutputTrim * ratio;

        // Clamp to valid output trim range (0.25 to 4.0)
        newOutputTrim = std::clamp (newOutputTrim, 0.25f, 4.0f);

        // Update output trim parameter (with recursion guard)
        isUpdatingOutputTrim = true;
        if (auto* param = parameters.getParameter (PARAM_OUTPUT_TRIM))
            param->setValueNotifyingHost (param->convertTo0to1 (newOutputTrim));
        isUpdatingOutputTrim = false;

        // Remember current input trim for next delta calculation
        lastInputTrimValue = newValue;

        // Update shared instance manager with normalized param values
        // Use JUCE's convertTo0to1 to properly handle the skew factor
        float driveNorm = 0.5f;
        float volumeNorm = 0.5f;
        if (auto* driveParam = parameters.getParameter (PARAM_INPUT_TRIM))
            driveNorm = driveParam->convertTo0to1 (newValue);
        if (auto* volumeParam = parameters.getParameter (PARAM_OUTPUT_TRIM))
            volumeNorm = volumeParam->convertTo0to1 (newOutputTrim);
        sharedInstanceManager.updateParams (driveNorm, volumeNorm);
    }
    else if (parameterID == PARAM_OUTPUT_TRIM && !isUpdatingOutputTrim && !isReceivingRemoteUpdate)
    {
        // Volume changed independently (e.g., user double-clicked to reset)
        // Update SharedInstanceManager so Master sees the change
        float driveNorm = 0.5f;
        float volumeNorm = 0.5f;
        if (auto* driveParam = parameters.getParameter (PARAM_INPUT_TRIM))
            driveNorm = driveParam->getValue();  // Already normalized
        if (auto* volumeParam = parameters.getParameter (PARAM_OUTPUT_TRIM))
            volumeNorm = volumeParam->getValue();  // Already normalized
        sharedInstanceManager.updateParams (driveNorm, volumeNorm);
    }
    else if (parameterID == PARAM_MACHINE_MODE)
    {
        // Update shared instance manager immediately when mode changes
        // Mode: 0 = Master, 1 = Tracks
        sharedInstanceManager.setMode (static_cast<int> (newValue));
    }
}

//==============================================================================
// Called by the DAW when track properties change (e.g., track name)
void TapeMachinePluginSimulatorAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    // Update track name in shared instance manager if it changed
    if (properties.name.isNotEmpty())
    {
        sharedInstanceManager.setTrackName (properties.name);
        DBG("Track name updated to: " << properties.name);
    }
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeMachinePluginSimulatorAudioProcessor();
}
