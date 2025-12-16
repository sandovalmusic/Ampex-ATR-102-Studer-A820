#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
TapeMachinePluginSimulatorAudioProcessorEditor::TapeMachinePluginSimulatorAudioProcessorEditor (TapeMachinePluginSimulatorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Define color scheme (vintage tape aesthetic)
    backgroundColour = juce::Colour (0xff2b2b2b);  // Dark grey
    accentColour = juce::Colour (0xffcc8844);      // Warm copper/gold
    textColour = juce::Colour (0xffeaeaea);        // Light grey

    // Title Label
    titleLabel.setText ("LOW THD TAPE SIMULATOR", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, accentColour);
    addAndMakeVisible (titleLabel);

    // Machine Mode ComboBox (Ampex ATR-102 vs Studer A820)
    machineModeLabel.setText ("Machine", juce::dontSendNotification);
    machineModeLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    machineModeLabel.setJustificationType (juce::Justification::centredLeft);
    machineModeLabel.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (machineModeLabel);

    machineModeCombo.addItem ("Ampex ATR-102", 1);
    machineModeCombo.addItem ("Studer A820", 2);
    machineModeCombo.setSelectedId (1, juce::dontSendNotification);
    machineModeCombo.setColour (juce::ComboBox::backgroundColourId, backgroundColour.brighter (0.2f));
    machineModeCombo.setColour (juce::ComboBox::textColourId, textColour);
    machineModeCombo.setColour (juce::ComboBox::outlineColourId, accentColour);
    machineModeCombo.setColour (juce::ComboBox::arrowColourId, accentColour);
    addAndMakeVisible (machineModeCombo);

    machineModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        audioProcessor.getValueTreeState(),
        TapeMachinePluginSimulatorAudioProcessor::PARAM_MACHINE_MODE,
        machineModeCombo
    );

    // Tape Formula ComboBox
    tapeFormulaLabel.setText ("Tape", juce::dontSendNotification);
    tapeFormulaLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    tapeFormulaLabel.setJustificationType (juce::Justification::centredLeft);
    tapeFormulaLabel.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (tapeFormulaLabel);

    tapeFormulaCombo.addItem ("GP9", 1);
    tapeFormulaCombo.addItem ("SM900", 2);
    tapeFormulaCombo.setSelectedId (1, juce::dontSendNotification);
    tapeFormulaCombo.setColour (juce::ComboBox::backgroundColourId, backgroundColour.brighter (0.2f));
    tapeFormulaCombo.setColour (juce::ComboBox::textColourId, textColour);
    tapeFormulaCombo.setColour (juce::ComboBox::outlineColourId, accentColour);
    tapeFormulaCombo.setColour (juce::ComboBox::arrowColourId, accentColour);
    addAndMakeVisible (tapeFormulaCombo);

    tapeFormulaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        audioProcessor.getValueTreeState(),
        TapeMachinePluginSimulatorAudioProcessor::PARAM_TAPE_FORMULA,
        tapeFormulaCombo
    );

    // Input Trim Slider (labeled as "Drive" for clarity)
    inputTrimLabel.setText ("Drive", juce::dontSendNotification);
    inputTrimLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    inputTrimLabel.setJustificationType (juce::Justification::centredLeft);
    inputTrimLabel.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (inputTrimLabel);

    inputTrimSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    inputTrimSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    inputTrimSlider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    inputTrimSlider.setColour (juce::Slider::rotarySliderOutlineColourId, backgroundColour.brighter (0.3f));
    inputTrimSlider.setColour (juce::Slider::textBoxTextColourId, textColour);
    inputTrimSlider.setColour (juce::Slider::textBoxBackgroundColourId, backgroundColour.brighter (0.1f));
    inputTrimSlider.setColour (juce::Slider::textBoxOutlineColourId, accentColour.withAlpha (0.5f));
    addAndMakeVisible (inputTrimSlider);

    inputTrimAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getValueTreeState(),
        TapeMachinePluginSimulatorAudioProcessor::PARAM_INPUT_TRIM,
        inputTrimSlider
    );

    // Output Trim Slider (labeled as "Volume" for clarity)
    outputTrimLabel.setText ("Volume", juce::dontSendNotification);
    outputTrimLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    outputTrimLabel.setJustificationType (juce::Justification::centredLeft);
    outputTrimLabel.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (outputTrimLabel);

    outputTrimSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outputTrimSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    outputTrimSlider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    outputTrimSlider.setColour (juce::Slider::rotarySliderOutlineColourId, backgroundColour.brighter (0.3f));
    outputTrimSlider.setColour (juce::Slider::textBoxTextColourId, textColour);
    outputTrimSlider.setColour (juce::Slider::textBoxBackgroundColourId, backgroundColour.brighter (0.1f));
    outputTrimSlider.setColour (juce::Slider::textBoxOutlineColourId, accentColour.withAlpha (0.5f));
    addAndMakeVisible (outputTrimSlider);

    outputTrimAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getValueTreeState(),
        TapeMachinePluginSimulatorAudioProcessor::PARAM_OUTPUT_TRIM,
        outputTrimSlider
    );

    // Set window size
    setSize (BASE_WIDTH, 400);

    // Start timer for meter updates (30 fps)
    startTimerHz (30);
}

TapeMachinePluginSimulatorAudioProcessorEditor::~TapeMachinePluginSimulatorAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
juce::Colour TapeMachinePluginSimulatorAudioProcessorEditor::getMeterColour (float levelDB) const
{
    // Meter shows INPUT to tape (after trim, before saturation)
    // Map tape operating levels based on typical calibration:
    // -6 VU ≈ -24 dBFS (quiet, very clean)
    // 0 VU (tape operating level) ≈ -18 dBFS digital standard
    // +3 dB ≈ -15 dBFS (0.17% THD)
    // +6 VU ≈ -12 dBFS (0.38% THD)
    // Clipping territory ≈ -6 dBFS and above

    // Grey/uncolored for levels below -6 VU - stays dim
    if (levelDB < -24.0f)
        return backgroundColour.brighter (0.4f);  // Subtle grey - doesn't draw attention

    // Green for -6 VU to +3 VU (comfortable operating range, clean to nominal)
    if (levelDB < -15.0f)
        return juce::Colour (0xff00cc44);  // Green - good operating level

    // Yellow for +3 VU (getting warm, ~0.17% THD) - starts to light up
    if (levelDB < -12.0f)
        return juce::Colour::fromHSV (0.166f, 0.9f, 0.9f, 1.0f);  // Yellow

    // Orange for +6 dB tape level (~0.38% THD) - getting hot
    if (levelDB < -9.0f)
        return juce::Colour (0xffff8800);  // Orange

    // Red for +9 VU and above (~0.8% THD and higher) - danger zone
    return juce::Colour (0xffff0000);  // Red
}

void TapeMachinePluginSimulatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient gradient (
        backgroundColour.brighter (0.1f), 0.0f, 0.0f,
        backgroundColour.darker (0.2f), 0.0f, static_cast<float> (getHeight()),
        false
    );
    g.setGradientFill (gradient);
    g.fillAll();

    // Decorative border
    g.setColour (accentColour.withAlpha (0.3f));
    g.drawRect (getLocalBounds().reduced (2), 2);

    // Section dividers
    g.setColour (accentColour.withAlpha (0.2f));
    g.drawLine (20.0f, 70.0f, static_cast<float> (getWidth() - 20), 70.0f, 1.0f);

    // Draw PPM meter if bounds are set
    if (!meterBounds.isEmpty())
    {
        // Meter background
        g.setColour (backgroundColour.darker (0.3f));
        g.fillRoundedRectangle (meterBounds, 4.0f);

        // Meter border
        g.setColour (accentColour.withAlpha (0.4f));
        g.drawRoundedRectangle (meterBounds, 4.0f, 2.0f);

        // Meter fill (based on level) - scale from -48dB to -6dB range
        // This covers well below 0 VU (-18dBFS) up to hot digital levels
        const float minDB = -48.0f;
        const float maxDB = -6.0f;
        float normalizedLevel = juce::jmap (meterLevel, minDB, maxDB, 0.0f, 1.0f);
        normalizedLevel = juce::jlimit (0.0f, 1.0f, normalizedLevel);

        if (normalizedLevel > 0.001f)
        {
            g.setColour (getMeterColour (meterLevel));
            auto fillBounds = meterBounds.reduced (4.0f);
            float fillWidth = fillBounds.getWidth() * normalizedLevel;
            fillBounds.setWidth (fillWidth);
            g.fillRoundedRectangle (fillBounds, 2.0f);
        }

        // Draw level marker text
        g.setColour (textColour.withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (meterLevel, 1) + " dB",
                    meterBounds.toNearestInt(),
                    juce::Justification::centred);
    }
}

void TapeMachinePluginSimulatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    const int margin = 20;
    const int controlHeight = 25;
    const int knobSize = 100;

    // Title at top
    titleLabel.setBounds (area.removeFromTop (60).reduced (margin, 10));

    // Main area
    area.removeFromTop (20);  // Spacing after divider
    auto controlArea = area.reduced (margin, 0);

    // Machine mode and tape formula selectors (same row)
    auto selectorRow = controlArea.removeFromTop (controlHeight + 10);
    machineModeLabel.setBounds (selectorRow.removeFromLeft (65));
    machineModeCombo.setBounds (selectorRow.removeFromLeft (130));
    selectorRow.removeFromLeft (20);  // Spacing between selectors
    tapeFormulaLabel.setBounds (selectorRow.removeFromLeft (40));
    tapeFormulaCombo.setBounds (selectorRow.removeFromLeft (90));

    controlArea.removeFromTop (15);  // Spacing

    // Knobs area - side by side
    auto knobsRow = controlArea.removeFromTop (knobSize + 30);

    // Input Trim knob (left side)
    auto inputKnobArea = knobsRow.removeFromLeft (knobsRow.getWidth() / 2);
    inputTrimLabel.setBounds (inputKnobArea.removeFromTop (20).withSizeKeepingCentre (100, 20));
    inputTrimSlider.setBounds (inputKnobArea.withSizeKeepingCentre (knobSize, knobSize));

    // Output Trim knob (right side)
    auto outputKnobArea = knobsRow;
    outputTrimLabel.setBounds (outputKnobArea.removeFromTop (20).withSizeKeepingCentre (100, 20));
    outputTrimSlider.setBounds (outputKnobArea.withSizeKeepingCentre (knobSize, knobSize));

    controlArea.removeFromTop (15);  // Spacing

    // PPM Meter (horizontal bar)
    auto meterArea = controlArea.removeFromTop (40);
    meterBounds = meterArea.reduced (10, 5).toFloat();
}

void TapeMachinePluginSimulatorAudioProcessorEditor::timerCallback()
{
    // Get actual level from processor
    float currentLevel = audioProcessor.getCurrentLevelDB();

    // PPM-style ballistics: 10ms integration time (attack), 2s return time (release)
    // At 30 fps (33.3ms per frame):
    // Attack: reach 99% in ~10ms → coefficient ≈ 1.0 (instant attack)
    // Release: reach 50% in ~2s → 60 frames → coefficient ≈ 0.988
    if (currentLevel > meterLevel)
        meterLevel = currentLevel;  // Instant attack (10ms integration)
    else
        meterLevel = meterLevel * 0.988f + currentLevel * (1.0f - 0.988f);  // 2s return time

    repaint();
}
