// tests/weapon_model_policy_test.cpp — native field decoding never accepts arbitrary writes.
#include "../SixBox/src/Features/Visuals/SkinChanger/WeaponModelPolicy.h"
#include <array>
#include <cassert>
int main()
{
    using namespace WeaponModelPolicy;
    std::array<uint8_t,51> bytes={};
    bytes[0]=0x8b;bytes[1]=0x87;bytes[6]=0x85;bytes[7]=0xc0;bytes[8]=0x75;
    bytes[45]=0x89;bytes[46]=0x87;
    bytes[2]=bytes[47]=0x5c;bytes[3]=bytes[48]=0x10;
    assert(WorldCacheOffset(bytes)==0x105c);
    assert(WorldCacheOffset(std::span(bytes).first(50))==0);
    bytes[47]=0x60;assert(WorldCacheOffset(bytes)==0);bytes[47]=0x5c;
    bytes[0]=0x90;assert(WorldCacheOffset(bytes)==0);bytes[0]=0x8b;
    bytes[2]=bytes[47]=0x5d;assert(WorldCacheOffset(bytes)==0);
    bytes[2]=bytes[47]=0;bytes[3]=bytes[48]=1;assert(WorldCacheOffset(bytes)==0);
    bytes[3]=bytes[48]=0x50;assert(WorldCacheOffset(bytes)==0);
}
