#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "TrackStripComponent.h"

//==============================================================================
/**
 * Ampex ATR-102 | Studer A820ulator GUI Editor
 *
 * Simple but functional interface with:
 * - Machine mode selector (Ampex/Studer)
 * - Input trim slider
 * - PPM-style level meter with color gradient
 */
class TapeMachinePluginSimulatorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                 public juce::Timer
{
public:
    TapeMachinePluginSimulatorAudioProcessorEditor (TapeMachinePluginSimulatorAudioProcessor&);
    ~TapeMachinePluginSimulatorAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // Reference to processor
    TapeMachinePluginSimulatorAudioProcessor& audioProcessor;

    // UI Components
    juce::Label titleLabel;

    // Machine Mode
    juce::Label machineModeLabel;
    juce::ComboBox machineModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> machineModeAttachment;

    // Input Trim
    juce::Label inputTrimLabel;
    juce::Slider inputTrimSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputTrimAttachment;

    // Output Trim
    juce::Label outputTrimLabel;
    juce::Slider outputTrimSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputTrimAttachment;

    // PPM Meter
    juce::Rectangle<float> meterBounds;
    float meterLevel = -96.0f;  // Start silent, not at 0dB (which would show red)
    juce::Colour getMeterColour (float levelDB) const;

    // Styling
    juce::Colour backgroundColour;
    juce::Colour accentColour;
    juce::Colour textColour;

    // Track strip container for Master mode (shows Tracks instances)
    LowTHD::TrackStripContainer trackStripContainer;

    // Track current mode for showing/hiding track strips
    int lastMachineMode = -1;

    // Base width before track strips
    static constexpr int BASE_WIDTH = 500;
    static constexpr int TRACK_STRIP_WIDTH = 550;  // Width of track strip panel (fits ~8 tracks without scrolling)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeMachinePluginSimulatorAudioProcessorEditor)
};
