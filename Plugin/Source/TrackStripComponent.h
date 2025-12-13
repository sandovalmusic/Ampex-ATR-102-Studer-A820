#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SharedInstanceManager.h"

namespace LowTHD
{

/**
 * TrackStripComponent - Individual track control strip for Master mode
 *
 * Displays:
 * - Track name label
 * - Level indicator (colored circle based on signal level)
 * - Drive knob (small rotary)
 * - Volume knob (small rotary)
 *
 * The knobs directly mirror and control the Tracks instance values.
 * When user drags a knob, it sends the value to the Tracks instance.
 * The knob always shows whatever value is in shared memory.
 */
class TrackStripComponent : public juce::Component
{
public:
    static constexpr int STRIP_WIDTH = 60;
    static constexpr int STRIP_HEIGHT = 130;

    TrackStripComponent();
    ~TrackStripComponent() override = default;

    // Update with track data from SharedInstanceManager
    void setTrackInfo(int64_t instanceId, const juce::String& name,
                      float levelDB, float driveNorm, float volumeNorm);

    // Get the instance ID this strip represents
    int64_t getInstanceId() const { return currentInstanceId; }

    // Callback when user adjusts knobs (instanceId, driveNorm, volumeNorm)
    std::function<void(int64_t, float, float)> onParamsChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int64_t currentInstanceId = 0;
    juce::String trackName = "Track";
    float currentLevelDB = -96.0f;

    // Knobs for Drive and Volume
    juce::Slider driveKnob;
    juce::Slider volumeKnob;

    // Label for track name
    juce::Label nameLabel;

    // Colors
    juce::Colour backgroundColour { 0xff2b2b2b };
    juce::Colour accentColour { 0xffcc8844 };
    juce::Colour textColour { 0xffeaeaea };

    // Get meter color based on level
    juce::Colour getLevelColour(float levelDB) const;

    // Knob change handlers - separate for each knob to avoid sending stale values
    void driveKnobValueChanged();
    void volumeKnobValueChanged();

    // Flag to prevent callback loops when setting values programmatically
    bool isUpdatingFromExternal = false;

    // Cache the last known good normalized values from shared memory
    // Used to send the non-changed knob's value when only one knob is adjusted
    float lastKnownDriveNorm = 0.5f;
    float lastKnownVolumeNorm = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackStripComponent)
};

/**
 * TrackStripContainer - Scrollable container for multiple track strips
 *
 * Used in Master mode to display all active Tracks instances.
 * Automatically shows/hides based on whether there are any Tracks instances.
 */
class TrackStripContainer : public juce::Component
{
public:
    TrackStripContainer();
    ~TrackStripContainer() override = default;

    // Update with current tracks data
    void updateTracks(const std::vector<SharedInstanceManager::InstanceInfo>& tracks);

    // Callback when user adjusts any track's params
    std::function<void(int64_t, float, float)> onTrackParamsChanged;

    // Get number of active tracks
    int getTrackCount() const { return static_cast<int>(trackStrips.size()); }

    // Get required width for all tracks
    int getRequiredWidth() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    std::vector<std::unique_ptr<TrackStripComponent>> trackStrips;

    // Viewport for scrolling if many tracks
    juce::Viewport viewport;
    juce::Component stripHolder;

    // Colors
    juce::Colour backgroundColour { 0xff2b2b2b };
    juce::Colour dividerColour { 0xff404040 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackStripContainer)
};

} // namespace LowTHD
