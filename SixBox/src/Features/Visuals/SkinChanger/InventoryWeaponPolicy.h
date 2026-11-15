#pragma once
#include <string_view>

namespace InventoryWeaponPolicy
{
    constexpr bool IsWeapon(std::string_view name)
    {
        return name.starts_with("tf_weapon") || name == "saxxy";
    }
    // Engine class order; slots are zero based. A definition index of zero is valid.
    constexpr int Stock(int playerClass, int slot)
    {
        constexpr int stock[10][3] = {
            {-1,-1,-1}, {13,23,0}, {14,16,3}, {18,10,6}, {19,20,1},
            {17,29,8}, {15,11,5}, {21,12,2}, {24,-1,4}, {9,22,7}
        };
        return playerClass >= 1 && playerClass <= 9 && slot >= 0 && slot < 3
            ? stock[playerClass][slot] : -1;
    }
    constexpr unsigned ClassMask(int playerClass)
    {
        constexpr unsigned masks[] = {0,1u,1u<<7,1u<<1,1u<<3,1u<<6,1u<<4,1u<<2,1u<<8,1u<<5};
        return playerClass >= 1 && playerClass <= 9 ? masks[playerClass] : 0;
    }
    constexpr bool PanAllowed(int playerClass, unsigned mask)
    {
        // A missing catalogue mask must not make the normal pan available to Spy/Engineer.
        return mask ? (mask & ClassMask(playerClass)) != 0 : playerClass >= 1 && playerClass <= 7;
    }
    constexpr int OwnedRank(int selected, int candidate, bool stockEquivalent, bool panAllowed)
    {
        if (selected == 1071)
            return candidate == 264 && panAllowed ? 0 : stockEquivalent ? 1 : -1;
        return -1;
    }
    constexpr int Resolve(int definition, bool sharedMelee, int playerClass, int slot)
    {
        return sharedMelee ? (slot == 2 ? Stock(playerClass, slot) : -1) : definition;
    }
}
