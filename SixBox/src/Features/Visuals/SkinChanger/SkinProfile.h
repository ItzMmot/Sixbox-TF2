#pragma once
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

struct BackpackItem_t
{
	uint32_t uSerial = 0;
	uint16_t iDef = 0;
	int iUnusual = 0;
	int iPaintColor = -1;
	int iPaintColor2 = -1;
	int iStyle = 0;
};

struct Skin_t
{
	int iPaintKit = -1;
	int iWear = 0;
	int iSeed = 0;
	int iQuality = -1;
	int iStatTrak = -1;
	int iKillEffect = -1;
	int iPaintColor = -1;
	int iBotkiller = 0;
	bool bAustralium = false;
	bool bFestive = false;
	int iKillstreak = 0;
	int iSheen = 0;
	int iUnusual = 0;

	bool Empty() const
	{
		return iPaintKit < 0 && iQuality < 0 && !bAustralium && !bFestive && iKillstreak <= 0 && iSheen <= 0 && iUnusual <= 0 && iBotkiller <= 0 && iStatTrak < 0 && iKillEffect <= 0 && iPaintColor < 0;
	}

	bool operator==(const Skin_t&) const = default;
};

struct SkinProfile_t
{
    std::unordered_map<int,Skin_t> skins;
    std::vector<BackpackItem_t> backpack;
    std::map<int,uint64_t> loadout;
};
