#include "SharedInstanceManager.h"

#if JUCE_MAC || JUCE_LINUX
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define SHARED_MEMORY_NAME "/lowthd_instances"
#elif JUCE_WINDOWS
    #include <windows.h>
    #define SHARED_MEMORY_NAME "Local\\lowthd_instances"
#endif

namespace LowTHD
{

SharedInstanceManager::SharedInstanceManager()
{
    openOrCreateSharedMemory();
}

SharedInstanceManager::~SharedInstanceManager()
{
    unregisterInstance();
    closeSharedMemory();
}

bool SharedInstanceManager::openOrCreateSharedMemory()
{
    sharedMemorySize = sizeof(SharedHeader) + sizeof(InstanceSlot) * MAX_INSTANCES;

#if JUCE_MAC || JUCE_LINUX
    // Try to open existing shared memory first
    int fd = shm_open(SHARED_MEMORY_NAME, O_RDWR, 0666);
    bool created = false;

    if (fd == -1)
    {
        // Doesn't exist, create it
        fd = shm_open(SHARED_MEMORY_NAME, O_RDWR | O_CREAT, 0666);
        if (fd == -1)
        {
            DBG("SharedInstanceManager: Failed to create shared memory");
            return false;
        }
        created = true;

        // Set size
        if (ftruncate(fd, static_cast<off_t>(sharedMemorySize)) == -1)
        {
            close(fd);
            shm_unlink(SHARED_MEMORY_NAME);
            DBG("SharedInstanceManager: Failed to set shared memory size");
            return false;
        }
    }

    // Map into process address space
    sharedMemory = mmap(nullptr, sharedMemorySize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);  // fd is no longer needed after mmap

    if (sharedMemory == MAP_FAILED)
    {
        sharedMemory = nullptr;
        DBG("SharedInstanceManager: Failed to map shared memory");
        return false;
    }

    // Initialize header if we created it
    if (created)
    {
        auto* header = getHeader();
        header->magic.store(MAGIC_NUMBER);
        header->version.store(VERSION);
        header->instanceCount.store(0);
    }

#elif JUCE_WINDOWS
    // Try to open existing
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEMORY_NAME);
    bool created = false;

    if (hMapFile == NULL)
    {
        // Create new
        hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(sharedMemorySize),
            SHARED_MEMORY_NAME
        );
        if (hMapFile == NULL)
        {
            DBG("SharedInstanceManager: Failed to create shared memory");
            return false;
        }
        created = true;
    }

    sharedMemory = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sharedMemorySize);
    CloseHandle(hMapFile);  // Handle no longer needed after mapping

    if (sharedMemory == NULL)
    {
        DBG("SharedInstanceManager: Failed to map shared memory");
        return false;
    }

    // Initialize header if we created it
    if (created)
    {
        auto* header = getHeader();
        header->magic.store(MAGIC_NUMBER);
        header->version.store(VERSION);
        header->instanceCount.store(0);
    }
#endif

    return true;
}

void SharedInstanceManager::closeSharedMemory()
{
    if (sharedMemory == nullptr)
        return;

#if JUCE_MAC || JUCE_LINUX
    munmap(sharedMemory, sharedMemorySize);
    // Note: We don't unlink here - other instances might still be using it
#elif JUCE_WINDOWS
    UnmapViewOfFile(sharedMemory);
#endif

    sharedMemory = nullptr;
}

SharedInstanceManager::SharedHeader* SharedInstanceManager::getHeader()
{
    return static_cast<SharedHeader*>(sharedMemory);
}

SharedInstanceManager::InstanceSlot* SharedInstanceManager::getSlot(int index)
{
    if (index < 0 || index >= MAX_INSTANCES)
        return nullptr;

    auto* base = static_cast<uint8_t*>(sharedMemory);
    return reinterpret_cast<InstanceSlot*>(base + sizeof(SharedHeader) + sizeof(InstanceSlot) * index);
}

int SharedInstanceManager::findFreeSlot()
{
    int64_t currentTime = getCurrentTimeMs();

    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        auto* slot = getSlot(i);
        if (slot == nullptr)
            continue;

        // Check if slot is unused or stale
        if (slot->instanceId.load() == 0)
            return i;

        // Check for stale instance (heartbeat too old)
        int64_t lastHeartbeat = slot->heartbeat.load();
        if (currentTime - lastHeartbeat > STALE_THRESHOLD_MS)
        {
            // Mark as inactive and claim it
            slot->instanceId.store(0);
            slot->active.store(0);
            return i;
        }
    }

    return -1;  // No free slots
}

int64_t SharedInstanceManager::generateInstanceId()
{
    // Use JUCE's random with system time for uniqueness
    return juce::Random::getSystemRandom().nextInt64() ^ juce::Time::currentTimeMillis();
}

int64_t SharedInstanceManager::getCurrentTimeMs()
{
    return juce::Time::currentTimeMillis();
}

int64_t SharedInstanceManager::registerInstance(int mode, const juce::String& trackName)
{
    if (sharedMemory == nullptr)
        return 0;

    mySlotIndex = findFreeSlot();
    if (mySlotIndex < 0)
    {
        DBG("SharedInstanceManager: No free slots available");
        return 0;
    }

    myInstanceId = generateInstanceId();

    auto* slot = getSlot(mySlotIndex);
    if (slot == nullptr)
        return 0;

    slot->instanceId.store(myInstanceId);
    slot->mode.store(static_cast<int8_t>(mode));
    slot->active.store(1);
    slot->levelDB.store(-96.0f);
    // Default normalized values for 0dB (gain=1.0) with skew 0.5 on range 0.25-4.0
    // Formula: normalized = ((value - start) / (end - start))^skew = ((1.0 - 0.25) / 3.75)^0.5 ≈ 0.447
    constexpr float defaultNormalized = 0.447214f;
    slot->driveNormalized.store(defaultNormalized);
    slot->volumeNormalized.store(defaultNormalized);
    slot->heartbeat.store(getCurrentTimeMs());
    slot->hasParamUpdate.store(0);
    slot->pendingDrive.store(defaultNormalized);
    slot->pendingVolume.store(defaultNormalized);

    // Set track name (truncate if needed)
    std::memset(slot->trackName, 0, sizeof(slot->trackName));
    if (trackName.isNotEmpty())
    {
        auto utf8 = trackName.toRawUTF8();
        std::strncpy(slot->trackName, utf8, sizeof(slot->trackName) - 1);
    }
    else
    {
        // Default name based on slot
        snprintf(slot->trackName, sizeof(slot->trackName), "Track %d", mySlotIndex + 1);
    }

    // Update instance count
    auto* header = getHeader();
    header->instanceCount.fetch_add(1);

    DBG("SharedInstanceManager: Registered instance " << myInstanceId << " in slot " << mySlotIndex << " with mode " << mode);
    return myInstanceId;
}

void SharedInstanceManager::unregisterInstance()
{
    if (sharedMemory == nullptr || mySlotIndex < 0)
        return;

    auto* slot = getSlot(mySlotIndex);
    if (slot != nullptr && slot->instanceId.load() == myInstanceId)
    {
        slot->instanceId.store(0);
        slot->active.store(0);

        auto* header = getHeader();
        header->instanceCount.fetch_sub(1);

        DBG("SharedInstanceManager: Unregistered instance " << myInstanceId);
    }

    myInstanceId = 0;
    mySlotIndex = -1;
}

void SharedInstanceManager::setMode(int newMode)
{
    if (mySlotIndex < 0)
    {
        DBG("SharedInstanceManager::setMode - mySlotIndex < 0, skipping");
        return;
    }

    auto* slot = getSlot(mySlotIndex);
    if (slot != nullptr && slot->instanceId.load() == myInstanceId)
    {
        slot->mode.store(static_cast<int8_t>(newMode));
        DBG("SharedInstanceManager::setMode - instance " << myInstanceId << " mode set to " << newMode);
    }
    else
    {
        DBG("SharedInstanceManager::setMode - slot null or ID mismatch");
    }
}

void SharedInstanceManager::setTrackName(const juce::String& name)
{
    if (mySlotIndex < 0)
        return;

    auto* slot = getSlot(mySlotIndex);
    if (slot != nullptr && slot->instanceId.load() == myInstanceId)
    {
        std::memset(slot->trackName, 0, sizeof(slot->trackName));
        if (name.isNotEmpty())
        {
            auto utf8 = name.toRawUTF8();
            std::strncpy(slot->trackName, utf8, sizeof(slot->trackName) - 1);
        }
    }
}

void SharedInstanceManager::updateLevel(float levelDB)
{
    if (mySlotIndex < 0)
        return;

    auto* slot = getSlot(mySlotIndex);
    if (slot != nullptr)
    {
        slot->levelDB.store(levelDB);
    }
}

void SharedInstanceManager::updateParams(float driveNorm, float volumeNorm)
{
    if (mySlotIndex < 0)
        return;

    auto* slot = getSlot(mySlotIndex);
    if (slot != nullptr)
    {
        slot->driveNormalized.store(driveNorm);
        slot->volumeNormalized.store(volumeNorm);
    }
}

void SharedInstanceManager::updateHeartbeat()
{
    if (mySlotIndex < 0)
        return;

    auto* slot = getSlot(mySlotIndex);
    if (slot != nullptr && slot->instanceId.load() == myInstanceId)
    {
        slot->heartbeat.store(getCurrentTimeMs());
    }
}

std::vector<SharedInstanceManager::InstanceInfo> SharedInstanceManager::getTracksInstances()
{
    std::vector<InstanceInfo> result;

    if (sharedMemory == nullptr)
    {
        DBG("getTracksInstances: sharedMemory is null!");
        return result;
    }

    int64_t currentTime = getCurrentTimeMs();
    int foundSlots = 0;
    int skippedSelf = 0;
    int skippedMode = 0;
    int skippedStale = 0;

    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        auto* slot = getSlot(i);
        if (slot == nullptr)
            continue;

        int64_t instanceId = slot->instanceId.load();
        if (instanceId == 0)
            continue;

        foundSlots++;

        // Skip self
        if (instanceId == myInstanceId)
        {
            skippedSelf++;
            continue;
        }

        // Only include Tracks mode instances
        int8_t mode = slot->mode.load();
        if (mode != 1)
        {
            skippedMode++;
            DBG("getTracksInstances: slot " << i << " instance " << instanceId << " mode=" << (int)mode << " (not Tracks)");
            continue;
        }

        // Check if still active (heartbeat not too old)
        int64_t lastHeartbeat = slot->heartbeat.load();
        bool isActive = (currentTime - lastHeartbeat) <= STALE_THRESHOLD_MS;

        if (!isActive)
        {
            skippedStale++;
            DBG("getTracksInstances: slot " << i << " stale (heartbeat " << (currentTime - lastHeartbeat) << "ms ago)");
            // Mark as inactive
            slot->active.store(0);
            continue;
        }

        InstanceInfo info;
        info.instanceId = instanceId;
        info.mode = mode;
        info.trackName = juce::String(slot->trackName);
        info.levelDB = slot->levelDB.load();
        info.driveNormalized = slot->driveNormalized.load();
        info.volumeNormalized = slot->volumeNormalized.load();
        info.isActive = true;

        result.push_back(info);
    }

    // Only log occasionally to avoid spam
    static int callCount = 0;
    if (++callCount % 150 == 0)  // Log every 5 seconds at 30fps
    {
        DBG("getTracksInstances: found " << foundSlots << " slots, skipped " << skippedSelf << " self, "
            << skippedMode << " wrong mode, " << skippedStale << " stale, returning " << (int)result.size());
    }

    return result;
}

void SharedInstanceManager::setTrackParams(int64_t targetId, float driveNorm, float volumeNorm)
{
    if (sharedMemory == nullptr)
        return;

    // Find the target instance
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        auto* slot = getSlot(i);
        if (slot == nullptr)
            continue;

        if (slot->instanceId.load() == targetId)
        {
            // Set pending parameters
            slot->pendingDrive.store(driveNorm);
            slot->pendingVolume.store(volumeNorm);
            slot->hasParamUpdate.store(1);
            return;
        }
    }
}

bool SharedInstanceManager::checkForParamUpdates(float& outDrive, float& outVolume)
{
    if (mySlotIndex < 0)
        return false;

    auto* slot = getSlot(mySlotIndex);
    if (slot == nullptr)
        return false;

    // Check if there's a pending update
    if (slot->hasParamUpdate.exchange(0) == 1)
    {
        outDrive = slot->pendingDrive.load();
        outVolume = slot->pendingVolume.load();
        return true;
    }

    return false;
}

} // namespace LowTHD
