#include "TrackStripComponent.h"

namespace LowTHD
{

//==============================================================================
// TrackStripComponent
//==============================================================================

TrackStripComponent::TrackStripComponent()
{
    // Track name label
    nameLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, textColour);
    addAndMakeVisible(nameLabel);

    // Drive knob (small rotary)
    // Use same range as main plugin: 0.25-4.0 with skew 0.5 for proper visual matching
    // The shared memory stores normalized values, so we use NormalisableRange
    driveKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    driveKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    driveKnob.setNormalisableRange(juce::NormalisableRange<double>(0.25, 4.0, 0.001, 0.5));
    driveKnob.setValue(1.0, juce::dontSendNotification);  // 1.0 = 0dB default
    driveKnob.setColour(juce::Slider::rotarySliderFillColourId, accentColour);
    driveKnob.setColour(juce::Slider::rotarySliderOutlineColourId, backgroundColour.brighter(0.3f));
    driveKnob.setTooltip("Drive");
    driveKnob.onValueChange = [this]() { driveKnobValueChanged(); };
    addAndMakeVisible(driveKnob);

    // Volume knob (small rotary)
    // Use same range as main plugin: 0.25-4.0 with skew 0.5 for proper visual matching
    volumeKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    volumeKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeKnob.setNormalisableRange(juce::NormalisableRange<double>(0.25, 4.0, 0.001, 0.5));
    volumeKnob.setValue(1.0, juce::dontSendNotification);  // 1.0 = 0dB default
    volumeKnob.setColour(juce::Slider::rotarySliderFillColourId, accentColour.darker(0.2f));
    volumeKnob.setColour(juce::Slider::rotarySliderOutlineColourId, backgroundColour.brighter(0.3f));
    volumeKnob.setTooltip("Volume");
    volumeKnob.onValueChange = [this]() { volumeKnobValueChanged(); };
    addAndMakeVisible(volumeKnob);

    setSize(STRIP_WIDTH, STRIP_HEIGHT);
}

void TrackStripComponent::setTrackInfo(int64_t instanceId, const juce::String& name,
                                        float levelDB, float driveNorm, float volumeNorm)
{
    currentInstanceId = instanceId;
    trackName = name;
    currentLevelDB = levelDB;

    // Cache the normalized values from shared memory - these are the authoritative values
    // Used by callbacks to send the non-changed param when only one knob is adjusted
    lastKnownDriveNorm = driveNorm;
    lastKnownVolumeNorm = volumeNorm;

    // Update label
    nameLabel.setText(name, juce::dontSendNotification);

    // Convert from normalized (0-1) to actual gain values (0.25-4.0)
    // The NormalisableRange on the knobs uses skew factor 0.5 (same as main plugin)
    // normalized 0.5 = linear gain 1.0 = 0dB
    juce::NormalisableRange<float> range(0.25f, 4.0f, 0.001f, 0.5f);
    float driveGain = range.convertFrom0to1(driveNorm);
    float volumeGain = range.convertFrom0to1(volumeNorm);

    // Only update knobs if user is NOT currently dragging them
    // isMouseOverOrDragging() returns true when user is interacting with the slider
    // This prevents the timer-based polling from fighting with user input
    isUpdatingFromExternal = true;
    if (!driveKnob.isMouseOverOrDragging())
        driveKnob.setValue(driveGain, juce::dontSendNotification);
    if (!volumeKnob.isMouseOverOrDragging())
        volumeKnob.setValue(volumeGain, juce::dontSendNotification);
    isUpdatingFromExternal = false;

    repaint();
}

juce::Colour TrackStripComponent::getLevelColour(float levelDB) const
{
    // Same color scheme as main meter
    if (levelDB < -24.0f)
        return backgroundColour.brighter(0.4f);  // Grey
    if (levelDB < -15.0f)
        return juce::Colour(0xff00cc44);  // Green
    if (levelDB < -12.0f)
        return juce::Colour::fromHSV(0.166f, 0.9f, 0.9f, 1.0f);  // Yellow
    if (levelDB < -9.0f)
        return juce::Colour(0xffff8800);  // Orange
    return juce::Colour(0xffff0000);  // Red
}

void TrackStripComponent::driveKnobValueChanged()
{
    // Don't send changes when we're updating from shared memory
    if (isUpdatingFromExternal)
        return;

    // Send the new drive value to the Tracks instance
    // Use cached volumeNorm to avoid sending stale volume values
    if (onParamsChanged && currentInstanceId != 0)
    {
        // Convert from actual gain values (0.25-4.0) to normalized (0-1)
        juce::NormalisableRange<float> range(0.25f, 4.0f, 0.001f, 0.5f);
        float driveGain = static_cast<float>(driveKnob.getValue());
        float driveNorm = range.convertTo0to1(driveGain);

        // Update our cache for drive
        lastKnownDriveNorm = driveNorm;

        // Send drive change with the cached (authoritative) volume value
        onParamsChanged(currentInstanceId, driveNorm, lastKnownVolumeNorm);
    }
}

void TrackStripComponent::volumeKnobValueChanged()
{
    // Don't send changes when we're updating from shared memory
    if (isUpdatingFromExternal)
        return;

    // Send the new volume value to the Tracks instance
    // Use cached driveNorm to avoid sending stale drive values
    if (onParamsChanged && currentInstanceId != 0)
    {
        // Convert from actual gain values (0.25-4.0) to normalized (0-1)
        juce::NormalisableRange<float> range(0.25f, 4.0f, 0.001f, 0.5f);
        float volumeGain = static_cast<float>(volumeKnob.getValue());
        float volumeNorm = range.convertTo0to1(volumeGain);

        // Update our cache for volume
        lastKnownVolumeNorm = volumeNorm;

        // Send volume change with the cached (authoritative) drive value
        onParamsChanged(currentInstanceId, lastKnownDriveNorm, volumeNorm);
    }
}

void TrackStripComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Background
    g.setColour(backgroundColour.darker(0.1f));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    // Border
    g.setColour(accentColour.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    // Level indicator circle (below name label)
    auto indicatorBounds = bounds.removeFromTop(40);
    indicatorBounds.removeFromTop(15);  // Space for name label

    auto circleBounds = indicatorBounds.withSizeKeepingCentre(20, 20);
    g.setColour(getLevelColour(currentLevelDB));
    g.fillEllipse(circleBounds.toFloat());

    // Circle border
    g.setColour(backgroundColour.brighter(0.5f));
    g.drawEllipse(circleBounds.toFloat().reduced(0.5f), 1.0f);

    // Knob labels (tiny)
    g.setColour(textColour.withAlpha(0.7f));
    g.setFont(juce::FontOptions(8.0f));

    auto knobArea = getLocalBounds();
    knobArea.removeFromTop(40);  // Skip indicator area
    auto driveArea = knobArea.removeFromTop(45);
    g.drawText("D", driveArea.removeFromTop(10), juce::Justification::centred);

    auto volumeArea = knobArea.removeFromTop(45);
    g.drawText("V", volumeArea.removeFromTop(10), juce::Justification::centred);
}

void TrackStripComponent::resized()
{
    auto bounds = getLocalBounds();

    // Name label at top
    nameLabel.setBounds(bounds.removeFromTop(15));

    // Skip space for level indicator
    bounds.removeFromTop(25);

    // Drive knob
    auto driveArea = bounds.removeFromTop(45);
    driveArea.removeFromTop(10);  // Label space
    driveKnob.setBounds(driveArea.withSizeKeepingCentre(35, 35));

    // Volume knob
    auto volumeArea = bounds.removeFromTop(45);
    volumeArea.removeFromTop(10);  // Label space
    volumeKnob.setBounds(volumeArea.withSizeKeepingCentre(35, 35));
}

//==============================================================================
// TrackStripContainer
//==============================================================================

TrackStripContainer::TrackStripContainer()
{
    viewport.setViewedComponent(&stripHolder, false);
    viewport.setScrollBarsShown(false, true);  // Horizontal scrollbar only
    addAndMakeVisible(viewport);
}

void TrackStripContainer::updateTracks(const std::vector<SharedInstanceManager::InstanceInfo>& tracks)
{
    // Remove strips for instances that no longer exist
    trackStrips.erase(
        std::remove_if(trackStrips.begin(), trackStrips.end(),
            [&tracks](const std::unique_ptr<TrackStripComponent>& strip)
            {
                auto id = strip->getInstanceId();
                return std::none_of(tracks.begin(), tracks.end(),
                    [id](const SharedInstanceManager::InstanceInfo& info)
                    {
                        return info.instanceId == id;
                    });
            }),
        trackStrips.end()
    );

    // Update existing strips and add new ones
    for (const auto& track : tracks)
    {
        // Find existing strip
        auto it = std::find_if(trackStrips.begin(), trackStrips.end(),
            [&track](const std::unique_ptr<TrackStripComponent>& strip)
            {
                return strip->getInstanceId() == track.instanceId;
            });

        if (it != trackStrips.end())
        {
            // Update existing
            (*it)->setTrackInfo(track.instanceId, track.trackName,
                                track.levelDB, track.driveNormalized, track.volumeNormalized);
        }
        else
        {
            // Add new strip
            auto newStrip = std::make_unique<TrackStripComponent>();
            newStrip->setTrackInfo(track.instanceId, track.trackName,
                                   track.levelDB, track.driveNormalized, track.volumeNormalized);

            // Forward param change callback
            newStrip->onParamsChanged = [this](int64_t id, float drive, float volume)
            {
                if (onTrackParamsChanged)
                    onTrackParamsChanged(id, drive, volume);
            };

            stripHolder.addAndMakeVisible(newStrip.get());
            trackStrips.push_back(std::move(newStrip));
        }
    }

    // Layout strips
    resized();
}

int TrackStripContainer::getRequiredWidth() const
{
    int count = static_cast<int>(trackStrips.size());
    return count * (TrackStripComponent::STRIP_WIDTH + 4) + 10;  // +4 for spacing, +10 for padding
}

void TrackStripContainer::paint(juce::Graphics& g)
{
    // Background
    g.setColour(backgroundColour);
    g.fillAll();

    // Divider on left edge
    g.setColour(dividerColour);
    g.drawLine(0.0f, 0.0f, 0.0f, static_cast<float>(getHeight()), 2.0f);

    // "TRACKS" label if no tracks
    if (trackStrips.empty())
    {
        g.setColour(juce::Colour(0xff808080));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText("No Tracks", getLocalBounds(), juce::Justification::centred);
    }
}

void TrackStripContainer::resized()
{
    auto bounds = getLocalBounds();

    // Layout strips horizontally inside the strip holder
    int x = 5;
    for (auto& strip : trackStrips)
    {
        strip->setBounds(x, 5, TrackStripComponent::STRIP_WIDTH,
                        TrackStripComponent::STRIP_HEIGHT);
        x += TrackStripComponent::STRIP_WIDTH + 4;
    }

    // Set strip holder size
    stripHolder.setSize(std::max(x + 5, bounds.getWidth()),
                        TrackStripComponent::STRIP_HEIGHT + 10);

    // Viewport fills container
    viewport.setBounds(bounds);
}

} // namespace LowTHD
