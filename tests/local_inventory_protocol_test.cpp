#include <cassert>
#include <cstdint>
#include <cstring>
#include "../SixBox/src/Features/Visuals/SkinChanger/LocalInventoryProtocol.h"

int main()
{
    using namespace LocalInventoryProtocol;
    // Native AddEconItem may auto-notify for UNKNOWN (0), whereas crate reason
    // 5 belongs to the reveal panel. Keep both modern format bits intact.
    static_assert((CratePosition & 0x40000000u) != 0);
    static_assert((CratePosition & 0x80000000u) != 0);
    static_assert((CratePosition & 0x3fffffffu) == 5);
    Responses queue;
    uint32_t size = 99, type = 0;
    uint8_t packet[24]{};
    assert(!queue.Available(&size) && size == 99);
    assert(queue.Retrieve(&type, packet, sizeof(packet), &size) == 1);
    constexpr uint64_t job = 0x0123456789abcdefull;
    queue.PushUnlock(job);
    queue.PushUnlock(~uint64_t(0));
    assert(queue.Available(&size) && size == sizeof(packet));
    assert(queue.Retrieve(&type, packet, 23, &size) == 2);
    assert(type == 1008 && size == 24 && queue.Available(nullptr));
    assert(queue.Retrieve(&type, nullptr, 24, &size) == 2);
    assert(queue.Retrieve(&type, packet, 24, &size) == 0);
    uint16_t version, index;
    uint64_t target, source;
    uint32_t result;
    std::memcpy(&version, packet, 2);
    std::memcpy(&target, packet + 2, 8);
    std::memcpy(&source, packet + 10, 8);
    std::memcpy(&index, packet + 18, 2);
    std::memcpy(&result, packet + 20, 4);
    assert(version == 1 && target == job && source == ~uint64_t(0));
    assert(index == 0 && result == 1);
    assert(queue.Retrieve(&type, packet, 24, &size) == 0);
    std::memcpy(&target, packet + 2, 8);
    assert(target == ~uint64_t(0) && !queue.Available(&size));
    queue.PushUnlock(job);
    queue.Clear();
    assert(!queue.Available(&size));
}
