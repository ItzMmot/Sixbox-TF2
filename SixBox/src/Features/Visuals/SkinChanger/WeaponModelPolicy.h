#pragma once
#include <cstdint>
#include <cstddef>
#include <span>

namespace WeaponModelPolicy
{
    inline int WorldCacheOffset(std::span<const uint8_t> code)
    {
        if (code.size() < 51 || code[0] != 0x8b || code[1] != 0x87
            || code[6] != 0x85 || code[7] != 0xc0 || code[8] != 0x75
            || code[45] != 0x89 || code[46] != 0x87) return 0;
        uint32_t load = 0, store = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            load |= uint32_t(code[2+i]) << (i*8);
            store |= uint32_t(code[47+i]) << (i*8);
        }
        return load == store && load >= 0x400 && load <= 0x4000 && !(load & 3) ? int(load) : 0;
    }
}
