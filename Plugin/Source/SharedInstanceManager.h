#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include <cstring>

namespace TapeMachine
{

/**
 * SharedInstanceManager - Inter-plugin communication via memory-mapped files
 *
 * Allows Master mode instances to see and control all Tracks mode instances.
 * Uses platform-native memory-mapped files for cross-plugin communication.
 *
 * Architecture:
 * - Each plugin instance registers on construction, unregisters on destruction
 * - Tracks instances publish their level and receive parameter updates
 * - Master instances poll for active Tracks and can send param changes
 *
 * Thread Safety:
 * - Audio thread only calls updateLevel() - uses atomic store
 * - Message thread handles registration, getTracksInstances(), setTrackParams()
 */
class SharedInstanceManager
{
public:
    static constexpr int MAX_INSTANCES = 32;
    static constexpr uint32_t MAGIC_NUMBER = 0x54415045;  // "TAPE"
    static constexpr uint32_t VERSION = 1;
    static constexpr int HEARTBEAT_INTERVAL_MS = 100;
    static constexpr int STALE_THRESHOLD_MS = 500;

    // Data structure for each instance slot (128 bytes total)
    struct alignas(8) InstanceSlot
    {
        std::atomic<int64_t> instanceId { 0 };
        std::atomic<int8_t> mode { 0 };           // 0=Master, 1=Tracks
        std::atomic<int8_t> active { 0 };
        char trackName[32] { 0 };
        std::atomic<float> levelDB { -96.0f };
        // Default normalized for 0dB (gain=1.0) with skew 0.5: ((1.0-0.25)/3.75)^0.5 ≈ 0.447
        std::atomic<float> driveNormalized { 0.447214f };    // 0.0-1.0
        std::atomic<float> volumeNormalized { 0.447214f };   // 0.0-1.0
        std::atomic<int64_t> heartbeat { 0 };
        std::atomic<int8_t> hasParamUpdate { 0 };       // Flag for pending param change from Master
        std::atomic<float> pendingDrive { 0.447214f };
        std::atomic<float> pendingVolume { 0.447214f };
        uint8_t reserved[42] { 0 };
    };

    // Header structure (64 bytes)
    struct alignas(8) SharedHeader
    {
        std::atomic<uint32_t> magic { 0 };
        std::atomic<uint32_t> version { 0 };
        std::atomic<int32_t> instanceCount { 0 };
        uint8_t reserved[52] { 0 };
    };

    // Simplified view of instance data for UI consumption
    struct InstanceInfo
    {
        int64_t instanceId;
        int mode;
        juce::String trackName;
        float levelDB;
        float driveNormalized;
        float volumeNormalized;
        bool isActive;
    };

    SharedInstanceManager();
    ~SharedInstanceManager();

    // Registration (call from constructor/destructor)
    int64_t registerInstance(int mode, const juce::String& trackName = "");
    void unregisterInstance();

    // Mode changes
    void setMode(int newMode);
    void setTrackName(const juce::String& name);

    // Updates from this instance (called frequently)
    void updateLevel(float levelDB);
    void updateParams(float driveNorm, float volumeNorm);
    void updateHeartbeat();

    // Reading all instances (called from UI thread in Master mode)
    std::vector<InstanceInfo> getTracksInstances();

    // Control from Master (sets pending params on target Tracks instance)
    void setTrackParams(int64_t targetId, float driveNorm, float volumeNorm);

    // Check if this instance has pending param updates from Master
    bool checkForParamUpdates(float& outDrive, float& outVolume);

    // Get this instance's ID
    int64_t getInstanceId() const { return myInstanceId; }

    // Check if shared memory is working
    bool isConnected() const { return sharedMemory != nullptr; }

private:
    void* sharedMemory = nullptr;
    size_t sharedMemorySize = 0;
    int64_t myInstanceId = 0;
    int mySlotIndex = -1;

    SharedHeader* getHeader();
    InstanceSlot* getSlot(int index);
    int findFreeSlot();
    int64_t generateInstanceId();
    int64_t getCurrentTimeMs();

    bool openOrCreateSharedMemory();
    void closeSharedMemory();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharedInstanceManager)
};

} // namespace TapeMachine
