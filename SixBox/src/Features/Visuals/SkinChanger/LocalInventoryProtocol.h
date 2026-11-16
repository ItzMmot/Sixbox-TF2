#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>

namespace LocalInventoryProtocol
{
    // Valve's GetUnacknowledgedPositionFor(UNACK_ITEM_FOUND_IN_CRATE).
    // Reason 0 means an ordinary drop and can be acknowledged before the reveal panel sees it.
    constexpr uint32_t CratePosition = 0xC0000005u;
    constexpr uint32_t UnlockResponse = 1008;

    class Responses
    {
        std::deque<std::array<uint8_t, 24>> messages;
    public:
        void Clear() { messages.clear(); }
        void PushUnlock(uint64_t targetJob)
        {
            // Legacy GC wire header: version, target job, source job; then
            // MsgGCStandardResponse_t (int16 response index, uint32 EResult).
            std::array<uint8_t, 24> packet{};
            const uint16_t version = 1;
            const uint64_t noJob = ~uint64_t(0);
            const uint32_t ok = 1;
            std::memcpy(packet.data(), &version, 2);
            std::memcpy(packet.data() + 2, &targetJob, 8);
            std::memcpy(packet.data() + 10, &noJob, 8);
            std::memcpy(packet.data() + 20, &ok, 4);
            messages.push_back(packet);
        }
        bool Available(uint32_t* size) const
        {
            if (messages.empty()) return false;
            if (size) *size = uint32_t(messages.front().size());
            return true;
        }
        // Same result codes as ISteamGameCoordinator::RetrieveMessage.
        int Retrieve(uint32_t* type, void* destination, uint32_t capacity, uint32_t* size)
        {
            if (!Available(size)) return 1;
            if (type) *type = UnlockResponse;
            if (!destination || capacity < messages.front().size()) return 2;
            std::memcpy(destination, messages.front().data(), messages.front().size());
            messages.pop_front();
            return 0;
        }
    };
}
