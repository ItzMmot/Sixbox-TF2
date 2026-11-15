#include "../../Configs/Configs.h"
#include "SkinChanger.h"
#include "SkinData.h"
#include "InventoryWeaponPolicy.h"
#include "GoldKillPolicy.h"
#include "WeaponModelPolicy.h"
#include "CrateLootData.h"
#include "../../Misc/AutoItem/AutoItem.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <cmath>
#include <format>
#include <random>
#include <iterator>
#include <fstream>
#include <filesystem>

#ifndef TEXTMODE

namespace
{
    struct GoldWeapon
    {
        int weaponID = -1;
        GoldKillPolicy::Kind kind = GoldKillPolicy::Kind::None;
    };
    std::array<GoldWeapon, MAX_WEAPONS> s_goldWeapons = {};
    GoldKillPolicy::Deaths s_goldDeaths;
    struct PendingGoldRagdoll
    {
        CHandle<CTFRagdoll> entity;
        void(__fastcall* create)(void*) = nullptr;
        float queued = 0.f;
    };
    std::array<PendingGoldRagdoll, 129> s_goldPending = {};

    int RagdollUserID(CTFRagdoll* ragdoll)
    {
        player_info_t info = {};
        return I::EngineClient->GetPlayerInfo(ragdoll->m_iPlayerIndex(), &info) ? info.userID : 0;
    }

    void FinishRagdoll(CTFRagdoll* ragdoll, void(__fastcall* create)(void*), bool gold)
    {
        if (Vars::Visuals::Removals::Ragdolls.Value && !SDK::CleanScreenshot()) return;
        const int effects = Vars::Visuals::Effects::RagdollEffects.Value;
        if (!SDK::CleanScreenshot() && (gold || effects))
        {
            using namespace Vars::Visuals::Effects::RagdollEffectsEnum;
            ragdoll->m_bGib() = false;
            ragdoll->m_bBurning() = !gold && (effects & Burning);
            ragdoll->m_bElectrocuted() = !gold && (effects & Electrocuted);
            ragdoll->m_bBecomeAsh() = !gold && (effects & Ash);
            ragdoll->m_bDissolving() = !gold && (effects & Dissolve);
            ragdoll->m_bGoldRagdoll() = gold || (effects & Gold);
            ragdoll->m_bIceRagdoll() = !gold && (effects & Ice);
        }
        if (gold && !SDK::CleanScreenshot())
            ragdoll->m_iDamageCustom() = TF_DMG_CUSTOM_GOLD_WRENCH;
        create(ragdoll);
    }
}

MAKE_SIGNATURE(CEconItemSchema_GetAttributeDefinition, "client.dll", "85 D2 78 ? 3B 91 E8 02 00 00 7D ? 48 8B 81 D8 02 00 00 8B D2 48 8B 04 D0 C3 33 C0 C3", 0x0);
MAKE_SIGNATURE(CAttributeList_SetRuntimeAttributeValue, "client.dll", "48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44", 0x0);
MAKE_SIGNATURE(CEconItemView_Constructor, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 48 8D 05 ? ? ? ? 66 C7 41 10 FF FF 48 89 01 48 8B F9 48 8D 05", 0x0);
MAKE_SIGNATURE(CEconItemView_Assign, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 0F B7 42 48 48 8B DA 66 89 41 48 48 8B F9 8B 42 4C 89 41 4C 8B 42 50 89 41 50 8B 6A", 0x0);
MAKE_SIGNATURE(CEconItemView_GetSOCData, "client.dll", "40 57 48 83 EC 20 48 8B 41 78 48 8B F9 48 85 C0 0F 85 A3 00 00 00 48 8B 0D ? ? ? ? 48 89 5C 24 30 48 8B 01 FF 90 D0", 0x0);
MAKE_SIGNATURE(CEconItem_Constructor, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 48 83 C1 08 E8 ? ? ? ? 48 8B 5C 24 30 48 8D 05 ? ? ? ? 33 C9 48 89 07", 0x0);
MAKE_SIGNATURE(CTFPlayerInventory_AddEconItem, "client.dll", "40 53 48 83 EC 30 0F B6 5C 24 60 88 5C 24 20 E8 ? ? ? ? 84 C0 74 20 84 DB 74 14", 0x0);
MAKE_SIGNATURE(CPlayerInventory_RemoveItem, "client.dll", "40 53 56 57 48 83 EC 30 44 8B 41 70 33 DB 48 8B F9 8B F3 45 85 C0 0F 8E", 0x0);
MAKE_SIGNATURE(CTFPlayerInventory_GetMaxItems, "client.dll", "40 53 48 83 EC ? 48 8B 89 ? ? ? ? BB", 0x0);
MAKE_SIGNATURE(CCrateLootListWrapper_InitSeriesCall, "client.dll", "E8 ? ? ? ? 84 C0 75 1E 48 8B D7 48 8D 4C 24 68 E8 ? ? ? ? 84 C0 75 0D 48 8B D7 48 8D 4C 24 68 E8", 0x0);
MAKE_SIGNATURE(CCrateLootListWrapper_InitName, "client.dll", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 8B 05 ? ? ? ? 48 8B FA 48 8B F1 A8 01 75 26", 0x0);
MAKE_SIGNATURE(CCrateLootListWrapper_InitLineItems, "client.dll", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B FA 48 8B D9 E8 ? ? ? ? 8B 70 08 39 35 ? ? ? ? 74", 0x0);

MAKE_SIGNATURE(Inventory_WorldModelCacheField, "client.dll", "8B 87 ? ? ? ? 85 C0 75 ? 48 8B 05 ? ? ? ? 48 8B CF 48 8B 18 48 8B 07 FF 90 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B D0 FF 53 10 89 87", 0x0);

MAKE_SIGNATURE(CTFWearable_Constructor, "client.dll", "40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? C6 83 A0 0E 00 00 00 48 8D 05", 0x0);
MAKE_SIGNATURE(MemAlloc_Zero, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 48 8B 05 ? ? ? ? 48 8B F9 48 8B D7 48 8B 08 48 8B 01 FF 50 08 4C 8B C7 33 D2 48 8B C8 48 8B D8 E8", 0x0);
MAKE_SIGNATURE(CEconEntity_InitializeAttributes, "client.dll", "48 89 5C 24 08 57 48 83 EC 40 48 8B D9 48 81 C1 38 0C 00 00 48 8B D3 48 8B 01 FF 50 20 83 BB 94 0C 00 00 01 74 1E 48 8D", 0x0);
MAKE_SIGNATURE(CEconItemView_GetPlayerDisplayModel, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 0F B7 59 48 41 8B E8 48 63 F2 4C 8B F1 E8", 0x0);
MAKE_SIGNATURE(Inventory_CBaseEntity_SetModel, "client.dll", "40 57 48 83 EC 20 48 8B F9 48 85 D2 74 2F 48 8B 0D ? ? ? ? 48 89 5C 24 30 48 8B 01 FF 50 10 8B D0", 0x0);
MAKE_SIGNATURE(CBaseEntity_SetOwnerEntity, "client.dll", "40 53 48 83 EC 20 48 8B D9 48 85 D2 74 17 48 8B 02 48 8B CA FF 50 10 8B 10 89 93 64 07 00 00 48 83 C4 20 5B C3 C7 81 64 07 00 00 FF FF FF FF", 0x0);
MAKE_SIGNATURE(CUtlVector_SetCountHandle, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 48 63 DA 48 03 D9 49 63 F8 8B 43 10 3B C7 7D 12 44 8B C7 8B D0 44 2B C0 48 8B CB E8 ? ? ? ? EB 05 7E 03 89 7B 10 39 7B 08 7D 2D 83 7B 0C 00 7C 27 48 8B 03 48 8B CF 48 C1 E1 02", 0x0);

#endif

namespace
{
	uint64_t s_uItemCatalogueRevision = 0;
	template <class T, size_t N>
	bool Contains(const T(&aList)[N], int iValue)
	{
		return std::find(std::begin(aList), std::end(aList), iValue) != std::end(aList);
	}

	int RedirectIndex(int nWeaponIndex)
	{
		switch (nWeaponIndex)
		{
		case Soldier_m_RocketLauncher: return Soldier_m_RocketLauncherR;
		case Scout_m_Scattergun: return Scout_m_ScattergunR;
		case Pyro_m_FlameThrower: return Pyro_m_FlameThrowerR;
		case Demoman_m_GrenadeLauncher: return Demoman_m_GrenadeLauncherR;
		case Demoman_s_StickybombLauncher: return Demoman_s_StickybombLauncherR;
		case Heavy_m_Minigun: return Heavy_m_MinigunR;
		case Engi_t_Wrench: return Engi_t_WrenchR;
		case Medic_s_MediGun: return Medic_s_MediGunR;
		case Sniper_m_SniperRifle: return Sniper_m_SniperRifleR;
		case Sniper_s_SMG: return Sniper_s_SMGR;
		case Spy_t_Knife: return Spy_t_KnifeR;
		case Spy_m_Revolver: return Spy_m_RevolverR;
		case Scout_s_ScoutsPistol:
		case Engi_s_EngineersPistol: return Engi_s_PistolR;
		case Soldier_s_SoldiersShotgun:
		case Pyro_s_PyrosShotgun:
		case Heavy_s_HeavysShotgun:
		case Engi_m_EngineersShotgun: return Soldier_s_ShotgunR;
		case Heavy_t_Fists: return Heavy_t_FistsR;
		case Scout_t_Bat: return Scout_t_BatR;
		case Soldier_t_Shovel: return Soldier_t_ShovelR;
		case Pyro_t_FireAxe: return Pyro_t_FireAxeR;
		case Demoman_t_Bottle: return Demoman_t_BottleR;
		case Medic_t_Bonesaw: return Medic_t_BonesawR;
		case Sniper_t_Kukri: return Sniper_t_KukriR;
		default: return nWeaponIndex;
		}
	}

	int BotkillerOffset(int iKey)
	{
		switch (iKey)
		{
		case Sniper_m_SniperRifle:
		case Sniper_m_SniperRifleR: return 0;
		case Heavy_m_Minigun:
		case Heavy_m_MinigunR: return 1;
		case Spy_t_Knife:
		case Spy_t_KnifeR: return 2;
		case Engi_t_Wrench:
		case Engi_t_WrenchR: return 3;
		case Medic_s_MediGun:
		case Medic_s_MediGunR: return 4;
		case Demoman_s_StickybombLauncher:
		case Demoman_s_StickybombLauncherR: return 5;
		case Pyro_m_FlameThrower:
		case Pyro_m_FlameThrowerR: return 6;
		case Scout_m_Scattergun:
		case Scout_m_ScattergunR: return 7;
		case Soldier_m_RocketLauncher:
		case Soldier_m_RocketLauncherR: return 8;
		default: return -1;
		}
	}

	int BotkillerTargetDef(int iKey, int iHead)
	{
		static constexpr int aBases[] = { 792, 801, 881, 890, 899, 908, 957, 966 };
		if (iHead < 1 || iHead > int(std::size(aBases)))
			return 0;

		const int iOffset = BotkillerOffset(iKey);
		if (iOffset < 0)
			return 0;

		return aBases[iHead - 1] + iOffset;
	}

	const SkinData::Kit_t* FindKit(int iKit)
	{
		for (const auto& tKit : SkinData::kKits)
		{
			if (tKit.iId == iKit)
				return &tKit;
		}
		return nullptr;
	}

	const SkinData::PaintBase_t* FindPaintBase(int iDef)
	{
		for (const auto& tBase : SkinData::kPaintBases)
		{
			if (tBase.iDef == iDef)
				return &tBase;
		}
		return nullptr;
	}

	std::array<uint16_t, 65536> s_aSchemaWeaponTypes = {};
	std::array<bool, 65536> s_aSharedMelee = {};

	int StaticWeaponType(int iDef)
	{
		switch (iDef)
		{
		case 160: case 294: return 209;
		case 161: return 210;
		case 169: return 197;
		case 298: return 202;
		case 452: return 190;
		case 466: return 192;
		case 513: return 205;
		case 638: case 727: return 194;
		case 851: return 201;
		case 30666: return 209;
		}
		if (const auto* pBase = FindPaintBase(iDef))
			return RedirectIndex(pBase->iBaseDef);
		return RedirectIndex(iDef);
	}

	bool BuildWeaponTypes(const std::unordered_map<std::string, uint16_t>& mNames, const std::vector<std::pair<uint16_t, std::string>>& vBases)
	{
		s_aSchemaWeaponTypes.fill(0);
		for (const auto& [sName, iDef] : mNames)
			s_aSchemaWeaponTypes[iDef] = uint16_t(StaticWeaponType(iDef));
		for (int iPass = 0; iPass < 8; iPass++)
		{
			bool bChanged = false;
			for (const auto& [iDef, sBase] : vBases)
			{
				const auto it = mNames.find(sBase);
				if (it == mNames.end())
					continue;
				const uint16_t iBase = s_aSchemaWeaponTypes[it->second];
				if (iBase && s_aSchemaWeaponTypes[iDef] != iBase)
				{
					s_aSchemaWeaponTypes[iDef] = iBase;
					bChanged = true;
				}
			}
			if (!bChanged)
				break;
		}
		return std::any_of(s_aSchemaWeaponTypes.begin(), s_aSchemaWeaponTypes.end(), [](uint16_t iType) { return iType != 0; });
	}

	int WeaponType(int iDef)
	{
		if (iDef >= 0 && iDef < int(s_aSchemaWeaponTypes.size()) && s_aSchemaWeaponTypes[iDef])
			return s_aSchemaWeaponTypes[iDef];
		return StaticWeaponType(iDef);
	}

	int StockWeaponForSlot(int iClass, int iSlot)
    {
        return InventoryWeaponPolicy::Stock(iClass, iSlot);
    }

    int LoadoutWeaponType(int iDef, int iClass, int iSlot)
    {
        const bool shared = iDef == Misc_t_Saxxy || iDef == Misc_t_GoldFryingPan || iDef == Misc_t_FryingPan || (iDef >= 0 && iDef < 65536 && s_aSharedMelee[iDef]);
        const int resolved = InventoryWeaponPolicy::Resolve(iDef, shared, iClass, iSlot);
        return resolved < 0 ? -1 : WeaponType(resolved);
    }

	int TemplateDef(const SkinData::Kit_t& tKit, int iBaseDef)
	{
		switch (tKit.iGroup)
		{
		case SkinData::KitGeneric: return Contains(SkinData::kGenericDefs, iBaseDef) ? iBaseDef : 0;
		case SkinData::KitDragonSlayer: return Contains(SkinData::kDragonSlayerDefs, iBaseDef) ? iBaseDef : 0;
		case SkinData::KitMk1:
			for (const auto& tWeapon : SkinData::kMk1Weapons)
			{
				if (tWeapon.iKit == tKit.iId && tWeapon.iBaseDef == iBaseDef)
					return tWeapon.iPaintDef;
			}
			return 0;
		default: return 0;
		}
	}

	struct PaintPlan_t
	{
		bool bListed = false;
		bool bSupported = false;
		int iTargetDef = 0;
	};

	PaintPlan_t PlanPaint(int iKey, const SkinData::Kit_t& tKit, bool bAllowUnsupported)
	{
		PaintPlan_t tPlan = {};
		const auto pBase = FindPaintBase(iKey);
		tPlan.iTargetDef = pBase ? TemplateDef(tKit, pBase->iBaseDef) : 0;
		tPlan.bSupported = tPlan.iTargetDef && pBase->iKind == SkinData::BasePlain;
		tPlan.bListed = tPlan.bSupported || bAllowUnsupported;
		return tPlan;
	}

	PaintPlan_t PlanPaint(int iKey, int iKit, bool bAllowUnsupported)
	{
		const auto pKit = FindKit(iKit);
		return pKit ? PlanPaint(iKey, *pKit, bAllowUnsupported) : PaintPlan_t{};
	}

	unsigned HashSkin(int iKey, const Skin_t& tSkin)
	{
		const int aFields[] = { iKey, tSkin.iPaintKit, tSkin.iWear, tSkin.iSeed, tSkin.iQuality, tSkin.iStatTrak, tSkin.iBotkiller, tSkin.bAustralium, tSkin.bFestive, tSkin.iKillstreak, tSkin.iSheen, tSkin.iUnusual, tSkin.iKillEffect, tSkin.iPaintColor };
		unsigned uHash = 2166136261u;
		for (int iField : aFields)
		{
			uHash ^= unsigned(iField);
			uHash *= 16777619u;
		}
		return uHash;
	}

	unsigned ConfigHash(const std::unordered_map<int, Skin_t>& mSkins, bool bAllowUnsupported)
	{
		unsigned uHash = bAllowUnsupported ? 0x9E3779B9u : 0x85EBCA6Bu;
		for (const auto& [iKey, tSkin] : mSkins)
		{
			if (!tSkin.Empty())
				uHash += HashSkin(iKey, tSkin);
		}
		return uHash ? uHash : 1;
	}

#ifndef TEXTMODE
	constexpr int kAttrPaintkit = 834;
	constexpr int kAttrWear = 725;
	constexpr int kAttrSeedLo = 866;
	constexpr int kAttrSeedHi = 867;
	constexpr int kAttrInspect = 731;
	constexpr int kAttrUnusualStatic = 370;
	constexpr int kAttrFestive = 2053;
	constexpr int kAttrAustralium = 2027;
	constexpr int kAttrLootRarity = 2022;
	constexpr int kAttrStyleOverride = 542;
	constexpr int kAttrKillstreakTier = 2025;
	constexpr int kAttrKillstreakSheen = 2014;
	constexpr int kAttrKillEffect = 2013;
	constexpr int kAttrItemTint = 142;
	constexpr int kAttrItemTint2 = 261;
	constexpr int kAttrKillEater = 214;
	constexpr int kAttrKillEaterType = 292;
	constexpr int kAttrAttachedParticle = 134;
	constexpr int kAttrTauntParticle = 2041;
	constexpr size_t kItemViewAttributeList = 0xF0;

	constexpr float kWear[] = { 0.2f, 0.4f, 0.6f, 0.8f, 1.f };

	constexpr int kQualityStrange = 11;

	int UnusualParticle(int iUnusual)
	{
		switch (iUnusual)
		{
		case WeaponUnusualHot: return 701;
		case WeaponUnusualIsotope: return 702;
		case WeaponUnusualCool: return 703;
		case WeaponUnusualEnergyOrb: return 704;
		default: return 0;
		}
	}

	std::unordered_map<int, void*> s_mAttrDefs;

	void* ResolveUncachedAttrDef(int iIndex)
	{
		void* pDef = nullptr;
		__try
		{
			if (auto pSchema = CEconItemSchema::GetInstance())
				pDef = S::CEconItemSchema_GetAttributeDefinition.Call<void*>(pSchema, iIndex);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			pDef = nullptr;
		}
		return pDef;
	}

	void* ResolveAttrDef(int iIndex)
	{
		if (const auto it = s_mAttrDefs.find(iIndex); it != s_mAttrDefs.end())
			return it->second;
		void* pDef = ResolveUncachedAttrDef(iIndex);
		if (pDef)
			s_mAttrDefs[iIndex] = pDef;
		return pDef;
	}

	class CAttributeList
	{
	public:
		void SetAttribute(int iIndex, float flValue)
		{
			const auto pDef = ResolveAttrDef(iIndex);
			if (!pDef)
				return;

			S::CAttributeList_SetRuntimeAttributeValue.Call<void>(this, pDef, flValue);
		}

		void SetInt(int iIndex, int iValue)
		{
			SetAttribute(iIndex, std::bit_cast<float>(iValue));
		}

		float GetValue(uint16_t iIndex, float flDefault = -1.f) const
		{
			auto* pThis = reinterpret_cast<const uint8_t*>(this);
			auto* pData = *reinterpret_cast<const uint8_t* const*>(pThis + 8);
			const int iCount = *reinterpret_cast<const int*>(pThis + 24);
			for (int i = 0; pData && i < iCount; i++)
			{
				if (*reinterpret_cast<const uint16_t*>(pData + size_t(i) * 24 + 8) == iIndex)
					return *reinterpret_cast<const float*>(pData + size_t(i) * 24 + 12);
			}
			return flDefault;
		}

		void RemoveAttribute(uint16_t iIndex)
		{
			auto* pData = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(this) + 8);
			auto& iCount = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(this) + 24);
			for (int i = 0; pData && i < iCount; i++)
			{
				if (*reinterpret_cast<uint16_t*>(pData + size_t(i) * 24 + 8) != iIndex)
					continue;
				std::memmove(pData + size_t(i) * 24, pData + size_t(i + 1) * 24, size_t(iCount - i - 1) * 24);
				iCount--;
				return;
			}
		}
	};

	int ResolveEconOffset(const char* sProp)
	{
		const char* aTables[] = { "CTFWeaponBase", "CBaseCombatWeapon", "CEconEntity" };
		for (const char* sTable : aTables)
		{
			if (const int nOffset = U::NetVars.GetNetVar(sTable, sProp))
				return nOffset;
		}
		return 0;
	}

	int CachedEconOffset(int& iOffset, const char* sProp)
	{
		if (!iOffset)
			iOffset = ResolveEconOffset(sProp);
		return iOffset;
	}

	CAttributeList* AttributeList(CTFWeaponBase* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = 0;
		if (!CachedEconOffset(nOffset, "m_AttributeList"))
			return nullptr;
		return reinterpret_cast<CAttributeList*>(uintptr_t(pWeapon) + nOffset);
	}

	uint16_t* DefIndex(CBaseEntity* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = 0;
		if (!CachedEconOffset(nOffset, "m_iItemDefinitionIndex"))
			return nullptr;
		return reinterpret_cast<uint16_t*>(uintptr_t(pWeapon) + nOffset);
	}

	int* EntityQuality(CBaseEntity* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = 0;
		if (!CachedEconOffset(nOffset, "m_iEntityQuality"))
			return nullptr;
		return reinterpret_cast<int*>(uintptr_t(pWeapon) + nOffset);
	}

	int ApplySkin(CTFWeaponBase* pWeapon, int iKey, const Skin_t& tSkin, bool bAllowUnsupported)
	{
		int iTarget = iKey;

		auto pList = AttributeList(pWeapon);
		if (!pList)
			return iTarget;

		if (tSkin.iPaintKit >= 0)
		{
			const auto tPlan = PlanPaint(iKey, tSkin.iPaintKit, bAllowUnsupported);
			if (tPlan.bListed)
			{
				if (tPlan.iTargetDef)
					iTarget = tPlan.iTargetDef;

				pList->SetInt(kAttrPaintkit, tSkin.iPaintKit);
				pList->SetAttribute(kAttrWear, kWear[std::clamp(tSkin.iWear, 0, int(std::size(kWear)) - 1)]);
				pList->SetAttribute(kAttrInspect, 1.f);
				pList->SetInt(kAttrSeedLo, tSkin.iSeed);
				pList->SetInt(kAttrSeedHi, 0);
			}
		}

		if (const int iBotkiller = BotkillerTargetDef(iKey, tSkin.iBotkiller))
			iTarget = iBotkiller;

		if (tSkin.bAustralium)
		{
			if (CSkinChanger::IsGoldenPan(iKey))
				iTarget = Misc_t_GoldFryingPan;
			else if (CSkinChanger::CanAustralium(iKey))
			{
				pList->SetInt(kAttrAustralium, 1);
				pList->SetInt(kAttrLootRarity, 1);
				pList->SetAttribute(kAttrStyleOverride, 1.f);
			}
		}

		if (tSkin.bFestive)
			pList->SetAttribute(kAttrFestive, 1.f);

		if (tSkin.iKillstreak)
			pList->SetAttribute(kAttrKillstreakTier, float(tSkin.iKillstreak));

		int iSheen = tSkin.iSheen;
		if (!iSheen && tSkin.iKillstreak > 1)
			iSheen = 1;
		if (iSheen)
			pList->SetAttribute(kAttrKillstreakSheen, float(iSheen));

		if (const int iUnusual = UnusualParticle(tSkin.iUnusual))
			pList->SetAttribute(kAttrUnusualStatic, float(iUnusual));
		if (tSkin.iKillEffect > 0)
			pList->SetAttribute(kAttrKillEffect, (float)tSkin.iKillEffect);
		if (tSkin.iPaintColor >= 0)
			pList->SetAttribute(kAttrItemTint, (float)tSkin.iPaintColor);

		if (tSkin.iStatTrak >= 0 || tSkin.iQuality >= 0)
		{
			const int iQuality = tSkin.iStatTrak >= 0 ? kQualityStrange : tSkin.iQuality;
			if (auto pQuality = EntityQuality(pWeapon))
			{
				if (*pQuality != iQuality)
					*pQuality = iQuality;
			}
			if (iQuality == kQualityStrange)
			{
				pList->SetInt(kAttrKillEater, std::max(tSkin.iStatTrak, 0));
				pList->SetAttribute(kAttrKillEaterType, 0.f);
			}
		}

		return iTarget;
	}
#endif
}

int CSkinChanger::Key(int iDefIndex)
{
	return RedirectIndex(iDefIndex);
}

const char* CSkinChanger::WeaponName(int iKey)
{
	for (const auto& tName : SkinData::kWeaponNames)
	{
		if (tName.iDef == iKey)
			return tName.sName;
	}
	return nullptr;
}

bool CSkinChanger::CanAustralium(int iKey)
{
	return Contains(SkinData::kAustraliumDefs, iKey) || IsGoldenPan(iKey);
}

bool CSkinChanger::IsGoldenPan(int iKey)
{
	return iKey == Misc_t_FryingPan;
}

bool CSkinChanger::CanFestive(int iKey)
{
	return Contains(SkinData::kFestiveDefs, iKey);
}

bool CSkinChanger::CanPaint(int iKey, bool bAllowUnsupported)
{
	if (bAllowUnsupported)
		return true;
	const auto pBase = FindPaintBase(iKey);
	return pBase && pBase->iKind == SkinData::BasePlain;
}

bool CSkinChanger::CanBotkiller(int iKey)
{
	return BotkillerOffset(iKey) >= 0;
}

void CSkinChanger::GetKits(int iKey, bool bAllowUnsupported, int iCurrent, std::vector<std::string>& vNames, std::vector<int>& vIds)
{
	vNames = { "None" };
	vIds = { -1 };
	if (iKey < 0)
		return;

	for (int iPass = 0; iPass < 2; iPass++)
	{
		for (const auto& tKit : SkinData::kKits)
		{
			if ((tKit.iGroup == SkinData::KitMk1) != (iPass == 1))
				continue;

			const auto tPlan = PlanPaint(iKey, tKit, bAllowUnsupported);
			if (!tPlan.bListed && tKit.iId != iCurrent)
				continue;

			std::string sName = tKit.sName;
			if (!tPlan.bListed)
				sName += " (unsupported, enable option)";
			else if (!tPlan.iTargetDef)
				sName += " (won't render)";
			vNames.push_back(std::move(sName));
			vIds.push_back(tKit.iId);
		}
	}
}

HeldWeapon_t CSkinChanger::GetHeldWeapon()
{
	std::lock_guard tLock(m_tMutex);
	return m_tHeld;
}

Skin_t CSkinChanger::Get(int iKey)
{
	std::lock_guard tLock(m_tMutex);
	auto it = m_mSkins.find(iKey);
	return it != m_mSkins.end() ? it->second : Skin_t{};
}

void CSkinChanger::Set(int iKey, const Skin_t& tSkin)
{
	if (iKey < 0)
		return;

	std::lock_guard tLock(m_tMutex);
	if (tSkin.Empty())
		m_mSkins.erase(iKey);
	else
		m_mSkins[iKey] = tSkin;
}

void CSkinChanger::Clear()
{
	std::lock_guard tLock(m_tMutex);
	m_mSkins.clear();
}

std::unordered_map<int, Skin_t> CSkinChanger::GetSkins()
{
	std::lock_guard tLock(m_tMutex);
	return m_mSkins;
}

void CSkinChanger::SetSkins(std::unordered_map<int, Skin_t> mSkins)
{
	std::lock_guard tLock(m_tMutex);
	m_mSkins = std::move(mSkins);
}

SkinProfile_t CSkinChanger::GetProfile()
{
    std::lock_guard lock(m_tMutex);
    return {m_mSkins,m_vBackpack,m_mLoadout};
}

void CSkinChanger::SetProfile(SkinProfile_t profile)
{
    std::lock_guard lock(m_tMutex);
    m_mSkins.swap(profile.skins);
    m_vBackpack.swap(profile.backpack);
    m_mLoadout.swap(profile.loadout);
    m_uNextSerial=1;
    for(const auto& item:m_vBackpack)
        if(item.uSerial<0xFFFFFFu) m_uNextSerial=(std::max)(m_uNextSerial,item.uSerial+1);
    m_bInventoryLoaded=false;
}

#ifndef TEXTMODE

void CSkinChanger::ScheduleRefresh()
{
	m_bRefreshPending = true;
	m_flDirtyTime = I::GlobalVars ? I::GlobalVars->realtime : 0.f;
}

int CSkinChanger::OriginalDef(CBaseEntity* pWeapon, int iHandle, int iCurrent) const
{
	auto it = m_mTracked.find(iHandle);
	if (it != m_mTracked.end() && it->second.pEntity == pWeapon && it->second.iApplied == iCurrent)
		return it->second.iOriginal;
	return iCurrent;
}

void CSkinChanger::RunFrame()
{
	std::unique_lock tLock(m_tMutex);

	if (!Vars::Visuals::SkinChanger::Enabled.Value)
	{
		if (m_bWasEnabled)
		{
			m_bWasEnabled = false;
			m_uLastHash = 0;
			m_mTracked.clear();
			ScheduleRefresh();
		}
		return;
	}

	m_bWasEnabled = true;

	const bool bFullPacket = I::ClientState && I::ClientState->m_nDeltaTick == -1;

	const bool bAllowUnsupported = Vars::Visuals::SkinChanger::AllowUnsupported.Value;
	bool bConfigChanged = false;
	if (const unsigned uHash = ConfigHash(m_mSkins, bAllowUnsupported); uHash != m_uLastHash)
	{
		bConfigChanged = true;
		m_uLastHash = uHash;
		if (!bFullPacket)
			ScheduleRefresh();
	}

	CTFPlayer* pLocal = nullptr;
	if (I::EngineClient && I::ClientEntityList)
	{
		const int iLocal = I::EngineClient->GetLocalPlayer();
		if (auto pEntity = iLocal > 0 ? I::ClientEntityList->GetClientEntity(iLocal) : nullptr)
			pLocal = pEntity->As<CTFPlayer>();
	}
	if (!pLocal || !pLocal->IsPlayer())
	{
		m_mTracked.clear();
		return;
	}

	std::unordered_map<int, Tracked_t> mTracked;
	std::array<CTFWeaponBase*, MAX_WEAPONS> aRefreshParticles = {};
	size_t uRefreshCount = 0;
	auto& vWeapons = pLocal->m_hMyWeapons();
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		const auto& hWeapon = vWeapons[i];
		if (!hWeapon.IsValid())
			continue;
		auto pWeapon = hWeapon.Get();
		if (!pWeapon)
			continue;
		auto pDef = DefIndex(pWeapon);
		if (!pDef)
			continue;

		const int iHandle = hWeapon.ToInt();
		if (bConfigChanged || !m_mTracked.contains(iHandle))
			aRefreshParticles[uRefreshCount++] = pWeapon;
		const int iOriginal = OriginalDef(pWeapon, iHandle, *pDef);
		const int iKey = Key(iOriginal);

		int iTarget = iOriginal;
		if (auto it = m_mSkins.find(iKey); it != m_mSkins.end() && !it->second.Empty())
			iTarget = ApplySkin(pWeapon, iKey, it->second, bAllowUnsupported);

		if (*pDef != iTarget)
			*pDef = uint16_t(iTarget);
		mTracked[iHandle] = { pWeapon, iOriginal, iTarget };
	}
	m_mTracked = std::move(mTracked);
	tLock.unlock();
	for (size_t i = 0; i < uRefreshCount; i++)
		S::CEconEntity_InitializeAttributes.Call<void>(aRefreshParticles[i]);
}

void CSkinChanger::Apply()
{
	{
		std::lock_guard tLock(m_tMutex);
		const bool bFullPacket = I::ClientState && I::ClientState->m_nDeltaTick == -1;
		if (bFullPacket)
		{
			m_bRefreshPending = false;
			for (auto& uKey : m_aWearableViewKey)
				uKey = 0;
		}
	}

	RunFrame();
}

void CSkinChanger::Service()
{
	bool bInGame = false;
	{
		std::lock_guard tLock(m_tMutex);
		if (!I::EngineClient || !I::ClientState || !I::GlobalVars
			|| !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo())
		{
			m_bRefreshPending = false;
		}
		else
		{
			bInGame = true;
			const float flNow = I::GlobalVars->realtime;
			if (m_bRefreshPending && flNow - m_flDirtyTime >= 0.35f && flNow - m_flLastRefresh >= 1.f)
			{
				m_bRefreshPending = false;
				if (I::ClientState->m_nDeltaTick != -1)
				{
					m_flLastRefresh = flNow;
					I::ClientState->ForceFullUpdate();
				}
			}
		}
	}

	FlushGoldRagdolls();
	SyncInventory();
	RunFrame();
	UpdateWeaponVisuals(bInGame ? H::Entities.GetLocal() : nullptr);
	UpdateWearables(bInGame ? H::Entities.GetLocal() : nullptr);
    CacheGoldWeapons(bInGame ? H::Entities.GetLocal() : nullptr);
}

void CSkinChanger::RequestFullUpdate()
{
	std::lock_guard tLock(m_tMutex);
	m_bRefreshPending = true;
	m_flDirtyTime = -1.f;
}

namespace
{
	template <class T>
	T& Field(void* pBase, size_t uOffset)
	{
		return *reinterpret_cast<T*>(uintptr_t(pBase) + uOffset);
	}

	constexpr int kInventoryOwner = 0x08;
	constexpr int kInventoryItemsData = 0x60;
	constexpr int kInventoryItemsCount = 0x70;
	constexpr int kInventoryGotItems = 0x94;
	constexpr int kInventorySOCache = 0x98;
	constexpr int kInventoryLoadout = 0x828;
	constexpr int kInventoryLoadoutChanged = 0xEB0;
	constexpr int kLoadoutSlots = 19;
	constexpr int kLoadoutMaxClass = 9;
	constexpr size_t kEconItemViewSize = SIZE_OF_ITEMVIEW;
	constexpr size_t kEconItemSize = 0x80;

	constexpr int kViewDefIndex = 0x48;
	constexpr int kViewQuality = 0x4C;
	constexpr int kViewItemIDHi = 0x60;
	constexpr int kViewItemIDLo = 0x64;
	constexpr int kViewInventoryPos = 0x6C;
	constexpr int kViewNonSOCItem = 0x78;
	constexpr int kViewNonSOCItemID = 0x80;
	constexpr int kViewTeamNumber = 0xD0;
	constexpr int kViewInitialized = 0xD4;

	constexpr int kItemID = 0x48;
	constexpr int kItemAccountID = 0x50;
	constexpr int kItemInventoryPos = 0x54;
	constexpr int kItemDefIndex = 0x58;
	constexpr int kItemLevel = 0x5A;
	constexpr int kItemQuality = 0x5B;
	constexpr int kItemFlags = 0x5C;
	constexpr int kItemOrigin = 0x5D;
	constexpr int kItemDirtyBits = 0x5F;
	constexpr int kItemEquipClass = 0x60;
	constexpr int kItemEquipSlot = 0x62;
	constexpr uint8_t kItemHasEquipInstance = 0x2;

	constexpr uint64_t kClientItemMagic = 0x554E49ull;
	constexpr int kClientItemMagicShift = 40;
	constexpr uint32_t kClientSerialMask = 0xFFFFFFu;
	constexpr uint32_t kPositionPlaced = 0x80000000u;
	constexpr uint32_t kPositionUnacknowledged = 0x40000000u;
	constexpr int kQualityUnique = 6;
	constexpr int kQualityUnusual = 5;

	uint64_t ClientItemID(uint32_t uSerial, uint16_t iDef)
	{
		return (kClientItemMagic << kClientItemMagicShift) | (uint64_t(uSerial & kClientSerialMask) << 16) | iDef;
	}

	uint16_t ClientItemDef(uint64_t uItemID)
	{
		return uint16_t(uItemID & 0xFFFF);
	}

	uint32_t ClientItemSerial(uint64_t uItemID)
	{
		return uint32_t((uItemID >> 16) & kClientSerialMask);
	}

	CEconItemView* ConstructItemView(void* pMemory)
	{
		if (!S::CEconItemView_Constructor())
			return nullptr;
		return S::CEconItemView_Constructor.Call<CEconItemView*>(pMemory);
	}

	void DestroyItemView(void* pView)
	{
		using Destructor_t = void*(__fastcall*)(void*, uint32_t);
		(*reinterpret_cast<Destructor_t**>(pView))[0](pView, 0u);
	}

	void AssignItemView(CEconItemView* pDst, const CEconItemView* pSrc)
	{
		if (S::CEconItemView_Assign())
			S::CEconItemView_Assign.Call<void*>(pDst, pSrc);
	}

	void* GetSOCData(void* pView)
	{
		return S::CEconItemView_GetSOCData() ? S::CEconItemView_GetSOCData.Call<void*>(pView) : nullptr;
	}

	CEconItem* CreateEconItem()
	{
		if (!S::CEconItem_Constructor() || !S::MemAlloc_Zero())
			return nullptr;

		auto* pMemory = S::MemAlloc_Zero.Call<void*>(kEconItemSize);
		return pMemory ? S::CEconItem_Constructor.Call<CEconItem*>(pMemory) : nullptr;
	}

	void DestroyEconItem(void* pItem)
	{
		if (!pItem)
			return;

		using Destructor_t = void*(__fastcall*)(void*, uint32_t);
		(*reinterpret_cast<Destructor_t**>(pItem))[0](pItem, 1u);
	}

	bool AddEconItem(CTFPlayerInventory* pInventory, CEconItem* pItem)
	{
		if (!S::CTFPlayerInventory_AddEconItem())
			return false;
		return S::CTFPlayerInventory_AddEconItem.Call<bool>(pInventory, pItem, false, false, false);
	}

	void RemoveItem(CTFPlayerInventory* pInventory, uint64_t uID)
	{
		if (S::CPlayerInventory_RemoveItem())
			S::CPlayerInventory_RemoveItem.Call<void>(pInventory, uID);
	}

	int MaxItems(CTFPlayerInventory* pInventory)
	{
		return S::CTFPlayerInventory_GetMaxItems() ? S::CTFPlayerInventory_GetMaxItems.Call<int>(pInventory) : 0;
	}

	uint64_t ViewItemID(const CEconItemView* pView)
	{
		const auto* pBytes = reinterpret_cast<const uint8_t*>(pView);
		const uint32_t uHi = *reinterpret_cast<const uint32_t*>(pBytes + kViewItemIDHi);
		const uint32_t uLo = *reinterpret_cast<const uint32_t*>(pBytes + kViewItemIDLo);
		return (uint64_t(uHi) << 32) | uLo;
	}

	CEconItemView* FindItemView(CTFPlayerInventory* pInventory, uint64_t uID)
	{
		if (!pInventory)
			return nullptr;

		auto* pData = Field<uint8_t*>(pInventory, kInventoryItemsData);
		const int iCount = Field<int>(pInventory, kInventoryItemsCount);
		if (!pData)
			return nullptr;

		for (int i = 0; i < iCount; i++)
		{
			auto* pView = reinterpret_cast<CEconItemView*>(pData + size_t(i) * kEconItemViewSize);
			if (ViewItemID(pView) == uID)
				return pView;
		}
		return nullptr;
	}

	uint32_t PositionToken(uint32_t uPosition)
	{
		return uPosition ? (uPosition | kPositionPlaced) : 0;
	}

	void EquipItem(void* pItem, int iClass, int iSlot)
	{
		if (!pItem)
			return;

		Field<uint8_t>(pItem, kItemDirtyBits) |= kItemHasEquipInstance;
		Field<uint16_t>(pItem, kItemEquipClass) = uint16_t(iClass);
		Field<uint16_t>(pItem, kItemEquipSlot) = uint16_t(iSlot);
	}

	void UnequipItem(void* pItem)
	{
		if (!pItem)
			return;

		Field<uint8_t>(pItem, kItemDirtyBits) &= uint8_t(~kItemHasEquipInstance);
		Field<uint16_t>(pItem, kItemEquipClass) = 0;
		Field<uint16_t>(pItem, kItemEquipSlot) = 0xFFFF;
	}

	bool IsWearableDef(uint16_t iDef);

	void PopulateCrateAttributes(CEconItemView* pView);

	void ApplyUnusual(CEconItemView* pView, void* pItem, int iUnusual)
	{
		const uint16_t iDef = Field<uint16_t>(pView, kViewDefIndex);
		if (!IsWearableDef(iDef))
			iUnusual = 0;
		const int iQuality = iUnusual > 0 ? kQualityUnusual : kQualityUnique;
		if (pItem)
			Field<uint8_t>(pItem, kItemQuality) = uint8_t(iQuality);
		Field<int>(pView, kViewQuality) = iQuality;

		auto pList = reinterpret_cast<CAttributeList*>(uintptr_t(pView) + kItemViewAttributeList);
		if (iUnusual > 0)
			pList->SetAttribute(kAttrAttachedParticle, float(iUnusual));
		else
			pList->RemoveAttribute(kAttrAttachedParticle);

		PopulateCrateAttributes(pView);
	}

	void ApplyPaintAttributes(CEconItemView* pView, int iColor, int iColor2)
	{
		auto* pList = reinterpret_cast<CAttributeList*>(uintptr_t(pView) + kItemViewAttributeList);
		if (iColor >= 0)
			pList->SetAttribute(kAttrItemTint, float(iColor));
		else
			pList->RemoveAttribute(kAttrItemTint);
		if (iColor2 >= 0)
			pList->SetAttribute(kAttrItemTint2, float(iColor2));
		else
			pList->RemoveAttribute(kAttrItemTint2);

		Field<uint16_t>(pView, 0xA0) = 0;
		auto*& pDescription = Field<void*>(pView, 0xD8);
		if (pDescription)
		{
			using Delete_t = void(__fastcall*)(void*, unsigned);
			(*reinterpret_cast<Delete_t**>(pDescription))[0](pDescription, 1);
			pDescription = nullptr;
		}
	}
}

CTFPlayerInventory* CSkinChanger::GetLocalInventory() const
{
	auto* pManager = I::TFInventoryManager();
	return pManager ? pManager->GetLocalInventory() : nullptr;
}

namespace
{
	constexpr int kEconEntityAttrManager = 0xC38;
	constexpr int kAttrContainerItem = 144;
	constexpr int kEconEntityClientside = 0xE21;
	constexpr int kMaxWearables = 8;
	constexpr int kRenderGroupOpaqueEntity = 2;
	constexpr int kRenderGroupViewModelOpaque = 10;
	constexpr float kClientThinkAlways = -1293.f;
	constexpr size_t kTFWearableSize = 0xEE0;

	constexpr int kLoadoutHead = LOADOUT_POSITION_HEAD;
	constexpr int kLoadoutMisc = LOADOUT_POSITION_MISC;
	constexpr int kLoadoutMisc2 = LOADOUT_POSITION_MISC2;
    static_assert(kLoadoutHead==7&&kLoadoutMisc==8&&kLoadoutMisc2==10);

	struct ServerWeaponChoice_t
	{
		uint64_t uClientID = 0;
		uint64_t uOwnedID = 0;
		int iItemCount = -1;
	};
	std::array<ServerWeaponChoice_t, 10 * kLoadoutSlots> s_aServerWeaponChoices = {};

	struct WeaponVisual_t
	{
		CHandle<CTFWeaponBase> hEntity;
		alignas(16) uint8_t aOriginal[SIZE_OF_ITEMVIEW] = {};
		uint64_t uClientID = 0;
		int iStyle = -1;
        int iWorldModel = 0, iViewModel = 0;
        uint64_t uNextModelAttempt = 0;
		uint64_t uNextExtraAttempt = 0;
		CHandle<CEconWearable> hExtraWorld, hExtraVM;
		EHANDLE hOriginalExtraWorld, hOriginalExtraVM;
		bool bConstructed = false;
	};
	std::array<WeaponVisual_t, MAX_WEAPONS> s_aWeaponVisuals = {};

    CEconItemView* WearableItemView(CEconWearable* entity)
    {
        if(!entity)return nullptr;
        static int offset=-1;
        if(offset<0)
        {
            // Resolve this entity family from its own receive table.
            const int definition=U::NetVars.GetNetVar("CTFWearable","m_iItemDefinitionIndex");
            offset=WearableLoadoutPolicy::ItemViewOffset(definition,int(kTFWearableSize),SIZE_OF_ITEMVIEW);
        }
        return offset>=0?reinterpret_cast<CEconItemView*>(uintptr_t(entity)+offset):nullptr;
    }

	bool ResizeWearables(CTFPlayer* pPlayer, int iCount)
	{
		if (!pPlayer || !S::CUtlVector_SetCountHandle())
			return false;

		static const int nOffset = U::NetVars.GetNetVar("CBasePlayer", "m_szLastPlaceName") + 56;
		if (nOffset <= 0)
			return false;

		S::CUtlVector_SetCountHandle.Call<void>(pPlayer, nOffset, iCount);
		return pPlayer->m_hMyWearables().Count() == iCount;
	}

	bool IsWearableDef(uint16_t iDef)
	{
		for (const auto& tItem : CSkinChanger::GetItems())
		{
			if (tItem.iDef == iDef)
				return tItem.sClass == "tf_wearable" || tItem.sClass.starts_with("tf_wearable_");
		}
		return false;
	}

	const ItemInfo_t* FindItemInfo(uint16_t iDef)
	{
		static std::unordered_map<uint16_t, const ItemInfo_t*> s_mLookup = {};
		static uint64_t s_uLookupRevision = 0;
		static size_t s_uSize = 0;
		static const ItemInfo_t* s_pData = nullptr;

		const auto& vItems = CSkinChanger::GetItems();
		if (s_uLookupRevision != s_uItemCatalogueRevision || s_uSize != vItems.size() || s_pData != vItems.data())
		{
			s_mLookup.clear();
			for (const auto& tItem : vItems)
				s_mLookup[tItem.iDef] = &tItem;
			s_uSize = vItems.size();
			s_pData = vItems.data();
			s_uLookupRevision = s_uItemCatalogueRevision;
		}

		auto it = s_mLookup.find(iDef);
		return it != s_mLookup.end() ? it->second : nullptr;
	}
}

namespace
{
	constexpr uint32_t kMsgUnlockCrate = 1007;
	constexpr uint32_t kMsgPaintItem = 1009;
	constexpr uint32_t kMsgUnlockCrateSize = 46;
	constexpr int kMessageHeader = 8;
	constexpr int kMessagePayload = 16;
	constexpr int kMessageSize = 24;
	constexpr int kOriginFoundInCrate = 8;
	constexpr uint64_t kCrateOpenDelay = 600;
	constexpr uint64_t kAwaitPositionTimeout = 20000;
	constexpr double kRareChance = 0.01;

	constexpr int kViewEconItemInterface = 0x18;
	constexpr int kListenerData = 64;
	constexpr int kListenerCount = 80;
	constexpr int kCacheOwner = 56;
	constexpr int kEventSourceCreated = 4;

	bool ReadMessage(void* pMessage, uint32_t& uType, uint32_t& uSize, uint64_t& uKey, uint64_t& uCrate)
	{
		__try
		{
			auto* pHeader = Field<uint8_t*>(pMessage, kMessageHeader);
			auto* pPayload = Field<uint8_t*>(pMessage, kMessagePayload);
			if (!pHeader || !pPayload)
				return false;

			uType = *reinterpret_cast<uint32_t*>(pHeader);
			uSize = Field<uint32_t>(pMessage, kMessageSize);
			if ((uType != kMsgUnlockCrate && uType != kMsgPaintItem) || uSize != kMsgUnlockCrateSize)
				return false;

			uKey = *reinterpret_cast<uint64_t*>(pPayload);
			uCrate = *reinterpret_cast<uint64_t*>(pPayload + 8);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	struct CrateLootList_t
	{
		void* pList = nullptr;
		uint32_t uAuditData = 0;
		bool bOwned = false;

		~CrateLootList_t()
		{
			if (pList && bOwned)
			{
				using Delete_t = void(__fastcall*)(void*, unsigned);
				(*reinterpret_cast<Delete_t**>(pList))[0](pList, 1);
			}
		}
	};
	static_assert(sizeof(CrateLootList_t) == 16);

	class LootItemIterator
	{
		std::vector<uint16_t>& m_vItems;

	public:
		explicit LootItemIterator(std::vector<uint16_t>& vItems) : m_vItems(vItems) {}
		virtual ~LootItemIterator() = default;
		virtual void OnIterate(uint16_t iDef) { m_vItems.push_back(iDef); }
	};

	struct LocalCrateProfile_t
	{
		int iSeries = 0;
		uint16_t iKeyDef = 0;
		uint16_t iAltKeyDef = 0;
		bool bShuffle = false;
		std::vector<uint16_t> vItems;
	};
	std::unordered_map<uint16_t, LocalCrateProfile_t> s_mCrateProfiles;
	std::unordered_map<int, std::vector<uint16_t>> s_mCrateSeries;

	KeyValues* CrateFirstKey(KeyValues* pKey) { return pKey ? Field<KeyValues*>(pKey, 48) : nullptr; }
	KeyValues* CrateNextKey(KeyValues* pKey) { return pKey ? Field<KeyValues*>(pKey, 40) : nullptr; }

	int DefinitionInt(KeyValues* pDefinition, const char* sAttribute, int iDefault = 0)
	{
		if (!pDefinition)
			return iDefault;
		const std::string sStatic = std::string("static_attrs/") + sAttribute;
		if (pDefinition->FindKey(sStatic.c_str()))
			return pDefinition->GetInt(sStatic.c_str(), iDefault);
		const std::string sDynamic = std::string("attributes/") + sAttribute + "/value";
		return pDefinition->GetInt(sDynamic.c_str(), iDefault);
	}

	class CrateSchemaReader
	{
		KeyValues* m_pLists;
		KeyValues* m_pCollections;
		const std::unordered_map<std::string, uint16_t>& m_mNames;
		std::array<KeyValues*, 64> m_aPath = {};
		size_t m_uVisits = 0;

		void Visit(KeyValues* pNode, std::vector<uint16_t>& vItems, size_t uDepth)
		{
			if (!pNode || uDepth >= m_aPath.size() || ++m_uVisits > 65536 || vItems.size() >= 8192
				|| std::find(m_aPath.begin(), m_aPath.begin() + uDepth, pNode) != m_aPath.begin() + uDepth)
				return;
			m_aPath[uDepth] = pNode;
			for (auto* pKey = CrateFirstKey(pNode); pKey && vItems.size() < 8192 && ++m_uVisits <= 65536; pKey = CrateNextKey(pKey))
			{
				const char* sName = pKey->GetName();
				if (!strcmp(sName, "loot_list_collection"))
				{
					if (m_pCollections)
						Visit(m_pCollections->FindKey(pKey->GetString(nullptr)), vItems, uDepth + 1);
					continue;
				}
				const auto it = m_mNames.find(sName);
				if (it != m_mNames.end())
				{
					if (pKey->GetFloat(nullptr, 1.f) > 0.f && std::find(vItems.begin(), vItems.end(), it->second) == vItems.end())
						vItems.push_back(it->second);
					continue;
				}
				if (auto* pList = m_pLists ? m_pLists->FindKey(sName) : nullptr)
				{
					if (pKey->GetFloat(nullptr, 1.f) > 0.f)
						Visit(pList, vItems, uDepth + 1);
					continue;
				}
				if (!strcmp(sName, "items") || !strcmp(sName, "common") || !strcmp(sName, "uncommon")
					|| !strcmp(sName, "rare") || !strcmp(sName, "mythical") || !strcmp(sName, "legendary") || !strcmp(sName, "ancient"))
					Visit(pKey, vItems, uDepth + 1);
			}
		}

	public:
		CrateSchemaReader(KeyValues* pRoot, const std::unordered_map<std::string, uint16_t>& mNames)
			: m_pLists(pRoot ? pRoot->FindKey("client_loot_lists") : nullptr),
			m_pCollections(pRoot ? pRoot->FindKey("item_collections") : nullptr), m_mNames(mNames) {}

		std::vector<uint16_t> Read(const char* sName)
		{
			std::vector<uint16_t> vItems;
			m_uVisits = 0;
			Visit(m_pLists ? m_pLists->FindKey(sName) : nullptr, vItems, 0);
			return vItems;
		}
	};

	KeyValues* LoadCrateSchema()
	{
		if (!I::FileSystem)
			return nullptr;
		const auto hFile = I::FileSystem->Open("scripts/items/items_game.txt", "rb", "GAME");
		if (!hFile)
			return nullptr;
		const unsigned uSize = I::FileSystem->Size(hFile);
		if (!uSize || uSize > 32 * 1024 * 1024)
		{
			I::FileSystem->Close(hFile);
			return nullptr;
		}
		std::vector<char> vText(size_t(uSize) + 1, '\0');
		const int iRead = I::FileSystem->Read(vText.data(), int(uSize), hFile);
		I::FileSystem->Close(hFile);
		if (iRead != int(uSize))
			return nullptr;
		auto* pRoot = new KeyValues("items_game");
		if (!pRoot->LoadFromBuffer("scripts/items/items_game.txt", vText.data()))
		{
			pRoot->DeleteThis();
			return nullptr;
		}
		return pRoot;
	}

	using CrateSeriesMap_t = std::unordered_map<int, std::vector<uint16_t>>;

	CrateSeriesMap_t ReadCrateCache(std::istream& tInput, const std::unordered_map<uint16_t, KeyValues*>& mDefinitions)
	{
		CrateSeriesMap_t mResult;
		std::string sMagic;
		int iVersion = 0;
		if (!(tInput >> sMagic >> iVersion) || (sMagic != "SIXBOX_CRATE_LOOT_CACHE" && sMagic != "UNIBOX_CRATE_LOOT_CACHE") || iVersion != 1)
			return mResult;
		int iSeries = 0, iCount = 0;
		while (tInput >> std::ws && tInput.peek() != std::char_traits<char>::eof())
		{
			if (!(tInput >> iSeries >> iCount))
				return {};
			if (iSeries <= 0 || iSeries > 65535 || iCount <= 0 || iCount > 8192 || mResult.size() >= 4096)
				return {};
			std::vector<uint16_t> vItems;
			for (int i = 0; i < iCount; i++)
			{
				int iDef = 0;
				if (!(tInput >> iDef) || iDef <= 0 || iDef > 65535)
					return {};
				if (mDefinitions.contains(uint16_t(iDef)))
					vItems.push_back(uint16_t(iDef));
			}
			std::sort(vItems.begin(), vItems.end());
			vItems.erase(std::unique(vItems.begin(), vItems.end()), vItems.end());
			if (!vItems.empty())
				mResult[iSeries] = std::move(vItems);
		}
		return tInput.eof() ? mResult : CrateSeriesMap_t{};
	}

	CrateSeriesMap_t LoadCrateCache(const std::unordered_map<uint16_t, KeyValues*>& mDefinitions)
	{
		try
		{
			const auto tPath = std::filesystem::path(F::Configs.m_sConfigPath) / "Core" / "crate_loot_cache.txt";
			std::error_code tError;
			if (std::filesystem::file_size(tPath, tError) > 4 * 1024 * 1024 || tError)
				return {};
			std::ifstream tFile(tPath);
			return ReadCrateCache(tFile, mDefinitions);
		}
		catch (...) { return {}; }
	}

	void SaveCrateCache()
	{
		try
		{
			const auto tDirectory = std::filesystem::path(F::Configs.m_sConfigPath) / "Core";
			std::error_code tError;
			std::filesystem::create_directories(tDirectory, tError);
			if (tError)
				return;
			const auto tPath = tDirectory / "crate_loot_cache.txt";
			const auto tTemp = tDirectory / "crate_loot_cache.tmp";
			std::ofstream tFile(tTemp, std::ios::trunc);
			tFile << "SIXBOX_CRATE_LOOT_CACHE 1\n";
			for (const auto& [iSeries, vItems] : s_mCrateSeries)
			{
				if (iSeries <= 0 || vItems.empty())
					continue;
				tFile << iSeries << ' ' << vItems.size();
				for (uint16_t iDef : vItems)
					tFile << ' ' << iDef;
				tFile << '\n';
			}
			tFile.close();
			if (tFile)
				MoveFileExW(tTemp.c_str(), tPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		}
		catch (...) {}
	}

	void BuildCrateProfiles(const std::unordered_map<std::string, uint16_t>& mNames, const std::unordered_map<uint16_t, KeyValues*>& mDefinitions)
	{
		s_mCrateProfiles.clear();
		s_mCrateSeries = LoadCrateCache(mDefinitions);
		KeyValuesAD tSchema(LoadCrateSchema());
		CrateSchemaReader tReader(tSchema, mNames);
		if (auto* pRevolving = tSchema ? tSchema->FindKey("revolving_loot_lists") : nullptr)
		{
			for (auto* pKey = CrateFirstKey(pRevolving); pKey; pKey = CrateNextKey(pKey))
			{
				const int iSeries = pKey->GetInt(nullptr);
				if (iSeries > 0)
				{
					auto vCurrent = tReader.Read(pKey->GetName());
					if (!vCurrent.empty())
						s_mCrateSeries[iSeries] = std::move(vCurrent);
				}
			}
		}
		for (const auto& [iDef, pDefinition] : mDefinitions)
		{
			if (strcmp(pDefinition->GetString("item_class"), "supply_crate"))
				continue;
			auto& tProfile = s_mCrateProfiles[iDef];
			tProfile.iSeries = DefinitionInt(pDefinition, "set supply crate series");
			if (!tProfile.iSeries)
				tProfile.iSeries = CrateLootData::DefaultSeries(iDef);
			tProfile.iKeyDef = uint16_t(DefinitionInt(pDefinition, "decoded by itemdefindex"));
			tProfile.bShuffle = DefinitionInt(pDefinition, "can shuffle crate contents") != 0;
			if (tProfile.iSeries)
			{
				auto& vSeries = s_mCrateSeries[tProfile.iSeries];
				if (vSeries.empty())
				{
					const auto vRetired = CrateLootData::RetiredContents(tProfile.iSeries);
					vSeries.assign(vRetired.begin(), vRetired.end());
				}
				tProfile.vItems = vSeries;
			}
			if (tProfile.vItems.empty())
				tProfile.vItems = tReader.Read(pDefinition->GetString("static_attrs/loot list name"));
			if (iDef == 5761 && tProfile.vItems.empty())
				tProfile.vItems = tReader.Read("summer_2014_limited_86");
			if (tProfile.bShuffle)
			{
				const int iMin = DefinitionInt(pDefinition, "shuffle crate item def min"), iMax = DefinitionInt(pDefinition, "shuffle crate item def max");
				if (iMin > 0 && iMax >= iMin && iMax <= 65535)
				{
					for (const auto& [sName, iItem] : mNames)
						if (iItem >= iMin && iItem <= iMax)
							tProfile.vItems.push_back(iItem);
				}
				else if (iDef == 5737 || iDef == 5738)
				{
					const auto vStockpile = CrateLootData::StockpileContents();
					tProfile.vItems.assign(vStockpile.begin(), vStockpile.end());
				}
			}
			if (!tProfile.iKeyDef && *pDefinition->GetString("tool/restriction"))
			{
				const char* sRestriction = pDefinition->GetString("tool/restriction");
				for (const auto& [iKeyDef, pKey] : mDefinitions)
				{
					if (!strcmp(pKey->GetString("tool/type"), "decoder_ring") && !strcmp(pKey->GetString("tool/restriction"), sRestriction))
					{
						tProfile.iKeyDef = iKeyDef;
						break;
					}
				}
				if (!tProfile.iKeyDef)
				{
					tProfile.iKeyDef = CrateLootData::RetiredKey(iDef);
					if (tProfile.iKeyDef)
						tProfile.iAltKeyDef = 5021;
					else
						tProfile.iKeyDef = 5021;
				}
			}
			std::sort(tProfile.vItems.begin(), tProfile.vItems.end());
			tProfile.vItems.erase(std::unique(tProfile.vItems.begin(), tProfile.vItems.end()), tProfile.vItems.end());
			std::erase_if(tProfile.vItems, [&](uint16_t iItem) { return !mDefinitions.contains(iItem); });
			if (tProfile.iSeries > 0 && !tProfile.vItems.empty())
				s_mCrateSeries[tProfile.iSeries] = tProfile.vItems;
		}
		if (tSchema)
			SaveCrateCache();
	}

	void PopulateCrateAttributes(CEconItemView* pView)
	{
		const auto it = s_mCrateProfiles.find(Field<uint16_t>(pView, kViewDefIndex));
		if (it == s_mCrateProfiles.end())
			return;
		const auto& tProfile = it->second;
		auto* pList = reinterpret_cast<CAttributeList*>(uintptr_t(pView) + kItemViewAttributeList);
		if (tProfile.iSeries > 0 && pList->GetValue(187, 0.f) <= 0.f)
			pList->SetAttribute(187, float(tProfile.iSeries));
		if (tProfile.vItems.empty() || std::bit_cast<uint32_t>(pList->GetValue(2037, 0.f)) != 0u)
			return;
		uint32_t uSeed = uint32_t(ViewItemID(pView) ^ (ViewItemID(pView) >> 32));
		const size_t uCount = std::min<size_t>(4, tProfile.vItems.size());
		std::array<size_t, 4> aChosen = {};
		for (size_t i = 0; i < uCount; i++)
		{
			uSeed = uSeed * 1664525u + 1013904223u;
			size_t uIndex = uSeed % tProfile.vItems.size();
			while (std::find(aChosen.begin(), aChosen.begin() + i, uIndex) != aChosen.begin() + i)
				uIndex = (uIndex + 1) % tProfile.vItems.size();
			aChosen[i] = uIndex;
			pList->SetInt(2037 + int(i), tProfile.vItems[uIndex]);
		}
	}


	bool CollectCrateLoot(CEconItemView* pView, std::vector<uint16_t>& vItems)
	{
		CrateLootList_t tLoot;
		auto* pInterface = reinterpret_cast<uint8_t*>(pView) + kViewEconItemInterface;
		if (!reinterpret_cast<bool(__fastcall*)(void*, void*)>(U::Memory.RelToAbs(S::CCrateLootListWrapper_InitSeriesCall(), 1))(&tLoot, pInterface)
			&& !S::CCrateLootListWrapper_InitName.Call<bool>(&tLoot, pInterface)
			&& !S::CCrateLootListWrapper_InitLineItems.Call<bool>(&tLoot, pInterface))
			return false;
		if (!tLoot.pList)
			return false;

		LootItemIterator tIterator(vItems);
		using Enumerate_t = void(__fastcall*)(void*, LootItemIterator*);
		(*reinterpret_cast<Enumerate_t**>(tLoot.pList))[5](tLoot.pList, &tIterator);
		return !vItems.empty();
	}

	int RandomUnusualEffect()
	{
		static std::vector<int> s_vEffects = {};
		if (s_vEffects.empty() && I::Localize)
		{
			for (int i = 1; i <= 3242; i++)
			{
				const char* sName = I::Localize->FindAsUTF8(std::format("#Attrib_Particle{}", i).c_str());
				if (!sName || !sName[0])
					continue;

				const std::string sEffect = sName;
				if (sEffect == "Invalid Particle" || sEffect.starts_with("Particle ") || sEffect.find("Attrib_Particle") != std::string::npos)
					continue;
				s_vEffects.push_back(i);
			}
		}
		if (s_vEffects.empty())
			return 0;

		static std::mt19937 s_tRandom{ std::random_device{}() };
		return s_vEffects[std::uniform_int_distribution<size_t>(0, s_vEffects.size() - 1)(s_tRandom)];
	}

	void NotifyCreated(CTFPlayerInventory* pInventory, void* pItem)
	{
		auto* pCache = Field<uint8_t*>(pInventory, kInventorySOCache);
		if (!pCache)
			return;

		auto** ppListeners = Field<void**>(pCache, kListenerData);
		const int iCount = Field<int>(pCache, kListenerCount);
		if (!ppListeners || iCount <= 0)
			return;

		std::vector<void*> vListeners(ppListeners, ppListeners + iCount);
		using Created_t = void(__fastcall*)(void*, void*, void*, int);
		for (void* pListener : vListeners)
		{
			if (pListener)
				(*reinterpret_cast<Created_t**>(pListener))[0](pListener, pCache + kCacheOwner, pItem, kEventSourceCreated);
		}
	}
}

bool CSkinChanger::OnSendMessage(void* pMessage)
{
	if (!pMessage || !ClientInventoryActive())
		return false;

	uint32_t uType = 0, uSize = 0;
	uint64_t uKey = 0, uCrate = 0;
	if (!ReadMessage(pMessage, uType, uSize, uKey, uCrate))
		return false;
	if (uType == kMsgUnlockCrate && !IsClientItem(uKey) && !IsClientItem(uCrate))
		return false;
	if (uType == kMsgPaintItem && !IsClientItem(uKey) && !IsClientItem(uCrate))
		return false;

	std::lock_guard tLock(m_tMutex);
	if (uType == kMsgPaintItem)
	{
		if (std::none_of(m_vPendingPaints.begin(), m_vPendingPaints.end(), [uKey, uCrate](const PendingCrate_t& tPaint) { return tPaint.uKey == uKey && tPaint.uCrate == uCrate; }))
			m_vPendingPaints.push_back({ uKey, uCrate, GetTickCount64() });
		return true;
	}
	if (std::none_of(m_vPendingCrates.begin(), m_vPendingCrates.end(), [uCrate](const PendingCrate_t& tCrate) { return tCrate.uCrate == uCrate; }))
		m_vPendingCrates.push_back({ uKey, uCrate, GetTickCount64() });
	return true;
}

void CSkinChanger::ProcessCrates(CTFPlayerInventory* pInventory)
{
	const uint64_t uNow = GetTickCount64();
	for (;;)
	{
		PendingCrate_t tPaint;
		{
			std::lock_guard tLock(m_tMutex);
			if (m_vPendingPaints.empty())
				break;
			tPaint = m_vPendingPaints.front();
			m_vPendingPaints.erase(m_vPendingPaints.begin());
		}
		if (!ApplyLocalPaint(pInventory, tPaint))
			SDK::Output("Inventory changer", "Local painting requires a client paint can and a paintable client item. Both items were kept.", {}, OUTPUT_CONSOLE | OUTPUT_TOAST);
	}
	for (;;)
	{
		PendingCrate_t tCrate;
		{
			std::lock_guard tLock(m_tMutex);
			const auto it = std::find_if(m_vPendingCrates.begin(), m_vPendingCrates.end(), [uNow](const PendingCrate_t& tPending) { return uNow - tPending.uTick >= kCrateOpenDelay; });
			if (it == m_vPendingCrates.end())
				break;
			tCrate = *it;
			m_vPendingCrates.erase(it);
		}
		if (!OpenCrate(pInventory, tCrate))
			SDK::Output("Inventory changer", "Cannot open this crate locally: check its contents, matching local key and free backpack space. The crate and key were kept.", {}, OUTPUT_CONSOLE | OUTPUT_TOAST);
	}
}

bool CSkinChanger::QueueLocalCrate(uint16_t iDef)
{
	const auto itProfile = s_mCrateProfiles.find(iDef);
	if (itProfile == s_mCrateProfiles.end() || itProfile->second.vItems.empty())
	{
		SDK::Output("Inventory changer", "No verified contents are available for this crate. Items were kept.", {}, OUTPUT_CONSOLE | OUTPUT_TOAST);
		return false;
	}
	std::lock_guard tLock(m_tMutex);
	const auto itCrate = std::find_if(m_vBackpack.rbegin(), m_vBackpack.rend(), [iDef](const BackpackItem_t& tItem) { return tItem.iDef == iDef; });
	if (itCrate == m_vBackpack.rend())
		return false;
	uint64_t uKey = ~0ull;
	if (itProfile->second.iKeyDef)
	{
		const auto itKey = std::find_if(m_vBackpack.begin(), m_vBackpack.end(), [&](const BackpackItem_t& tItem) { return tItem.iDef == itProfile->second.iKeyDef || (itProfile->second.iAltKeyDef && tItem.iDef == itProfile->second.iAltKeyDef); });
		if (itKey == m_vBackpack.end())
		{
			const auto* pKeyInfo = FindItemInfo(itProfile->second.iKeyDef);
			SDK::Output("Inventory changer", std::format("Add a local {} first.", pKeyInfo ? pKeyInfo->sName : "key for this crate").c_str(), {}, OUTPUT_CONSOLE | OUTPUT_TOAST);
			return false;
		}
		uKey = ClientItemID(itKey->uSerial, itKey->iDef);
	}
	const uint64_t uCrate = ClientItemID(itCrate->uSerial, itCrate->iDef);
	if (std::none_of(m_vPendingCrates.begin(), m_vPendingCrates.end(), [uCrate](const PendingCrate_t& tItem) { return tItem.uCrate == uCrate; }))
		m_vPendingCrates.push_back({ uKey, uCrate, GetTickCount64() });
	return true;
}

bool CSkinChanger::ApplyLocalPaint(CTFPlayerInventory* pInventory, const PendingCrate_t& tPaint)
{
	if (!IsClientItem(tPaint.uKey) || !IsClientItem(tPaint.uCrate) || tPaint.uKey == tPaint.uCrate)
		return false;
	auto* pPaintView = FindItemView(pInventory, tPaint.uKey);
	auto* pItemView = FindItemView(pInventory, tPaint.uCrate);
	if (!pPaintView || !pItemView)
		return false;
	const auto* pPaint = FindItemInfo(Field<uint16_t>(pPaintView, kViewDefIndex));
	const auto* pTarget = FindItemInfo(Field<uint16_t>(pItemView, kViewDefIndex));
	if (!pPaint || pPaint->iPaintColor < 0 || !pTarget || !pTarget->bPaintable)
		return false;
	{
		std::lock_guard tLock(m_tMutex);
		const uint32_t uSerial = ClientItemSerial(tPaint.uCrate);
		const auto it = std::find_if(m_vBackpack.begin(), m_vBackpack.end(), [uSerial](const BackpackItem_t& tItem) { return tItem.uSerial == uSerial; });
		if (it == m_vBackpack.end())
			return false;
		it->iPaintColor = pPaint->iPaintColor;
		it->iPaintColor2 = pPaint->iPaintColor2;
	}
	ApplyPaintAttributes(pItemView, pPaint->iPaintColor, pPaint->iPaintColor2);
	ApplyLocalDelete(tPaint.uKey);
	SDK::Output("Inventory changer", "Paint applied to the local item.", {}, OUTPUT_CONSOLE | OUTPUT_TOAST);
	return true;
}

bool CSkinChanger::OpenCrate(CTFPlayerInventory* pInventory, const PendingCrate_t& tCrate)
{
	auto* pSchema = CEconItemSchema::GetInstance();
	auto* pCrateView = FindItemView(pInventory, tCrate.uCrate);
	if (!pSchema || !pCrateView)
		return false;

	if (!IsClientItem(tCrate.uCrate) || (tCrate.uKey && tCrate.uKey != ~0ull && !IsClientItem(tCrate.uKey)))
		return false;
	if (tCrate.uKey && tCrate.uKey != ~0ull && !FindItemView(pInventory, tCrate.uKey))
		return false;
	std::vector<uint16_t> vCommon;
	const auto itProfile = s_mCrateProfiles.find(Field<uint16_t>(pCrateView, kViewDefIndex));
	if (itProfile != s_mCrateProfiles.end())
	{
		const auto& tProfile = itProfile->second;
		auto* pKeyView = tCrate.uKey && tCrate.uKey != ~0ull ? FindItemView(pInventory, tCrate.uKey) : nullptr;
		const uint16_t iKeyDef = pKeyView ? Field<uint16_t>(pKeyView, kViewDefIndex) : 0;
		if ((tProfile.iKeyDef && (!iKeyDef || (iKeyDef != tProfile.iKeyDef && iKeyDef != tProfile.iAltKeyDef)))
			|| (!tProfile.iKeyDef && pKeyView) || tCrate.uKey == tCrate.uCrate)
			return false;
	}

	auto* pAttributes = reinterpret_cast<CAttributeList*>(uintptr_t(pCrateView) + kItemViewAttributeList);
	if (itProfile != s_mCrateProfiles.end() && !itProfile->second.bShuffle)
	{
		const int iSeries = int(pAttributes->GetValue(187, float(itProfile->second.iSeries)));
		if (const auto it = s_mCrateSeries.find(iSeries); it != s_mCrateSeries.end())
			vCommon = it->second;
		if (vCommon.empty() && iSeries == itProfile->second.iSeries)
			vCommon = itProfile->second.vItems;
	}
	if (vCommon.empty() && !CollectCrateLoot(pCrateView, vCommon))
		return false;

	auto fFilter = [](std::vector<uint16_t>& vDefs)
	{
		std::vector<uint16_t> vAvailable = {};
		for (uint16_t iDef : vDefs)
		{
			if (FindItemInfo(iDef) && std::find(vAvailable.begin(), vAvailable.end(), iDef) == vAvailable.end())
				vAvailable.push_back(iDef);
		}
		vDefs = std::move(vAvailable);
	};
	fFilter(vCommon);
	if (vCommon.empty())
		return false;

	static std::mt19937 s_tRandom{ std::random_device{}() };
	const uint16_t iDef = vCommon[std::uniform_int_distribution<size_t>(0, vCommon.size() - 1)(s_tRandom)];
	const bool bRare = IsWearableDef(iDef) && std::uniform_real_distribution<double>(0.0, 1.0)(s_tRandom) < kRareChance;
	const int iUnusual = bRare && IsWearableDef(iDef) ? RandomUnusualEffect() : 0;

	uint32_t uAccountID = Field<uint32_t>(pInventory, kInventoryOwner);
	if (!uAccountID && I::SteamUser)
		uAccountID = I::SteamUser->GetSteamID().GetAccountID();

	auto* pItem = CreateEconItem();
	if (!pItem)
		return false;

	uint32_t uSerial = 0;
	{
		std::lock_guard tLock(m_tMutex);
		uSerial = NextSerial();
	}
	if (!uSerial)
	{
		DestroyEconItem(pItem);
		return false;
	}

	const uint64_t uID = ClientItemID(uSerial, iDef);
	Field<uint64_t>(pItem, kItemID) = uID;
	Field<uint32_t>(pItem, kItemAccountID) = uAccountID;
	Field<uint32_t>(pItem, kItemInventoryPos) = 0;
	Field<uint16_t>(pItem, kItemDefIndex) = iDef;
	Field<uint8_t>(pItem, kItemLevel) = 1;
	Field<uint8_t>(pItem, kItemQuality) = uint8_t(iUnusual > 0 ? kQualityUnusual : kQualityUnique);
	Field<uint8_t>(pItem, kItemFlags) = 0;
	Field<uint8_t>(pItem, kItemOrigin) = uint8_t(kOriginFoundInCrate);

	{
		std::lock_guard tLock(m_tMutex);
		m_mClientItemsByID[uID] = pItem;
		m_vBackpack.push_back({ uSerial, iDef, iUnusual });
		m_mAwaitingPosition[uID] = GetTickCount64();
	}

	NotifyCreated(pInventory, pItem);
	auto* pView = FindItemView(pInventory, uID);
	if (!pView)
	{
		ApplyLocalDelete(uID);
		return false;
	}
	{
		Field<void*>(pView, kViewNonSOCItem) = pItem;
		Field<uint64_t>(pView, kViewNonSOCItemID) = uID;
		ApplyUnusual(pView, pItem, iUnusual);
		m_mAppliedUnusual[uID] = iUnusual;
	}

	ApplyLocalDelete(tCrate.uCrate);
	if (tCrate.uKey && tCrate.uKey != ~0ull)
		ApplyLocalDelete(tCrate.uKey);
	return true;
}

bool CSkinChanger::ClientInventoryActive()
{
	std::lock_guard tLock(m_tMutex);
	return !m_vBackpack.empty();
}

bool CSkinChanger::IsClientItem(uint64_t uItemID)
{
	return (uItemID >> kClientItemMagicShift) == kClientItemMagic;
}

void* CSkinChanger::GetClientSOCData(uint64_t uItemID)
{
	std::lock_guard tLock(m_tMutex);
	auto it = m_mClientItemsByID.find(uItemID);
	return it != m_mClientItemsByID.end() ? it->second : nullptr;
}

uint64_t CSkinChanger::FindOwnedWeapon(uint64_t uClientItemID, int iClass, int iSlot)
{
	auto* pInventory = GetLocalInventory();
	auto* pSelected = FindItemView(pInventory, uClientItemID);
	if (!pSelected || !IsClientItem(uClientItemID))
		return 0;
	const uint16_t iSelectedDef = Field<uint16_t>(pSelected, kViewDefIndex);
	const auto* pInfo = FindItemInfo(iSelectedDef);
	if (!pInfo || !InventoryWeaponPolicy::IsWeapon(pInfo->sClass))
		return 0;

	const int iType = LoadoutWeaponType(iSelectedDef, iClass, iSlot);
	if (iType < 0)
		return 0;
	const bool bReskin = iSelectedDef != iType;
	auto* pData = Field<uint8_t*>(pInventory, kInventoryItemsData);
	const int iCount = Field<int>(pInventory, kInventoryItemsCount);
	uint64_t uExact = 0, uEquivalent = 0;
    uint64_t uPanChoice = 0;
    int iPanRank = 2;
    const auto* pPanInfo = iSelectedDef == Misc_t_GoldFryingPan ? FindItemInfo(Misc_t_FryingPan) : nullptr;
    const bool bPanAllowed = InventoryWeaponPolicy::PanAllowed(iClass, pPanInfo ? pPanInfo->uClassMask : 0);
	for (int i = 0; pData && i < iCount; i++)
	{
		auto* pView = reinterpret_cast<CEconItemView*>(pData + size_t(i) * kEconItemViewSize);
		const uint64_t uID = ViewItemID(pView);
		if (!uID || uID == ~0ull || IsClientItem(uID))
			continue;
		const uint16_t iDef = Field<uint16_t>(pView, kViewDefIndex);
        const int rank = InventoryWeaponPolicy::OwnedRank(iSelectedDef, iDef,
            iDef == iType || RedirectIndex(iDef) == iType, bPanAllowed);
        if (rank >= 0 && (rank < iPanRank || (rank == iPanRank && (!uPanChoice || uID < uPanChoice))))
        {
            iPanRank = rank;
            uPanChoice = uID;
        }
		if (iDef == iSelectedDef && (!uExact || uID < uExact))
			uExact = uID;
		if ((iDef == iType || RedirectIndex(iDef) == iType) && (!uEquivalent || uID < uEquivalent))
			uEquivalent = uID;
	}
    if (iSelectedDef == Misc_t_GoldFryingPan) return uPanChoice;
	return bReskin ? uEquivalent : (uExact ? uExact : uEquivalent);
}

std::map<int, uint64_t> CSkinChanger::GetLoadout()
{
	std::lock_guard tLock(m_tMutex);
	return m_mLoadout;
}

void CSkinChanger::SetLoadout(std::map<int, uint64_t> mLoadout)
{
	std::lock_guard tLock(m_tMutex);
	m_mLoadout = std::move(mLoadout);
}

void CSkinChanger::ApplyLocalLoadout(uint64_t uItemID, int iClass, int iSlot)
{
	auto* pInventory = GetLocalInventory();
	if (!pInventory || iClass < 1 || iClass > kLoadoutMaxClass || iSlot < 0 || iSlot >= kLoadoutSlots)
		return;

	const int iIndex = iClass * kLoadoutSlots + iSlot;
	if (iClass <= 9 && iSlot <= 2)
		s_aServerWeaponChoices[iIndex] = {};
	const uint64_t uNew = uItemID == ~0ull ? 0 : uItemID;
	auto& uSlot = Field<uint64_t>(pInventory, kInventoryLoadout + size_t(iIndex) * sizeof(uint64_t));
	const uint64_t uPrevious = uSlot;
	uSlot = uNew;
	Field<bool>(pInventory, kInventoryLoadoutChanged + iClass) = true;

	if (uNew)
	{
		if (auto* pView = FindItemView(pInventory, uNew))
			EquipItem(GetSOCData(pView), iClass, iSlot);
	}
	if (uPrevious && uPrevious != uNew)
	{
		if (auto* pView = FindItemView(pInventory, uPrevious))
			UnequipItem(GetSOCData(pView));
	}

	std::lock_guard tLock(m_tMutex);
	if (uPrevious && !IsClientItem(uPrevious))
		m_mOriginalLoadout[iIndex] = uPrevious;
	m_mLoadout[iIndex] = uNew;
}

void CSkinChanger::ApplyLocalPosition(uint64_t uItemID, uint32_t uPosition)
{
	auto* pInventory = GetLocalInventory();
	auto* pView = FindItemView(pInventory, uItemID);
	if (!pView)
		return;

	const uint32_t uToken = PositionToken(uPosition);
	Field<uint32_t>(pView, kViewInventoryPos) = uToken;
	if (auto* pItem = GetSOCData(pView))
		Field<uint32_t>(pItem, kItemInventoryPos) = uToken;
}

void CSkinChanger::RemoveClientItem(CTFPlayerInventory* pInventory, uint64_t uItemID)
{
	if (pInventory && FindItemView(pInventory, uItemID))
		RemoveItem(pInventory, uItemID);

	std::lock_guard tLock(m_tMutex);
	if (auto it = m_mClientItemsByID.find(uItemID); it != m_mClientItemsByID.end())
	{
		m_vGraveyard.push_back(it->second);
		m_mClientItemsByID.erase(it);
	}
	m_mAppliedUnusual.erase(uItemID);
	m_mAwaitingPosition.erase(uItemID);
	std::erase_if(m_mLoadout, [uItemID](const auto& tPair) { return tPair.second == uItemID; });
}

void CSkinChanger::ApplyLocalDelete(uint64_t uItemID)
{
	auto* pInventory = GetLocalInventory();
	if (!IsClientItem(uItemID))
	{
		if (pInventory && FindItemView(pInventory, uItemID))
			RemoveItem(pInventory, uItemID);

		std::lock_guard tLock(m_tMutex);
		std::erase_if(m_mLoadout, [uItemID](const auto& tPair) { return tPair.second == uItemID; });
		return;
	}

	{
		std::lock_guard tLock(m_tMutex);
		const uint32_t uSerial = ClientItemSerial(uItemID);
		std::erase_if(m_vBackpack, [uSerial](const BackpackItem_t& tItem) { return tItem.uSerial == uSerial; });
	}
	RemoveClientItem(pInventory, uItemID);
}

void CSkinChanger::ClearClientItems()
{
	auto* pInventory = GetLocalInventory();

	std::vector<uint64_t> vIDs = {};
	{
		std::lock_guard tLock(m_tMutex);
		for (const auto& [uID, pItem] : m_mClientItemsByID)
			vIDs.push_back(uID);
	}

	for (uint64_t uID : vIDs)
	{
		if (pInventory && FindItemView(pInventory, uID))
			RemoveItem(pInventory, uID);
	}

	std::lock_guard tLock(m_tMutex);
	for (auto& [uID, pItem] : m_mClientItemsByID)
		m_vGraveyard.push_back(pItem);
	m_mClientItemsByID.clear();
	m_mAppliedUnusual.clear();
	m_mAwaitingPosition.clear();
	m_vPendingCrates.clear();
	m_vPendingPaints.clear();
	m_tFailedItems.clear();
	m_mInventoryCounts.clear();
}

void CSkinChanger::ReapplyLoadout(CTFPlayerInventory* pInventory)
{
	std::map<int, uint64_t> mLoadout = {};
	{
		std::lock_guard tLock(m_tMutex);
		mLoadout = m_mLoadout;
	}

	for (const auto& [iIndex, uItemID] : mLoadout)
	{
		const int iClass = iIndex / kLoadoutSlots, iSlot = iIndex % kLoadoutSlots;
		if (iClass < 1 || iClass > kLoadoutMaxClass)
			continue;

		auto& uSlot = Field<uint64_t>(pInventory, kInventoryLoadout + size_t(iIndex) * sizeof(uint64_t));
		const bool bWeaponSlot = iSlot >= 0 && iSlot <= 2;
		if (bWeaponSlot && IsClientItem(uItemID))
		{
			auto& tChoice = s_aServerWeaponChoices[iIndex];
			const int iCount = Field<int>(pInventory, kInventoryItemsCount);
			if (tChoice.uClientID != uItemID || tChoice.iItemCount != iCount)
			{
				const uint64_t uPreviousOwned = tChoice.uOwnedID;
				const uint64_t uPreviousClient = tChoice.uClientID;
				tChoice = { uItemID, FindOwnedWeapon(uItemID, iClass, iSlot), iCount };
				if (!tChoice.uOwnedID)
				{
					const int iStock = StockWeaponForSlot(iClass, iSlot);
					auto* pSelected = FindItemView(pInventory, uItemID);
					if (iStock >= 0 && pSelected && WeaponType(iStock) == LoadoutWeaponType(Field<uint16_t>(pSelected, kViewDefIndex), iClass, iSlot))
						tChoice.uOwnedID = ~0ull;
				}
				if (tChoice.uOwnedID && (tChoice.uOwnedID != uPreviousOwned || uPreviousClient != uItemID))
					EquipOwnedInventoryWeapon(pInventory, tChoice.uOwnedID, iClass, iSlot);
				else if (!tChoice.uOwnedID && uPreviousClient != uItemID)
					SDK::Output("Inventory changer", "No owned weapon of this type was found. The server loadout was kept.", {}, OUTPUT_CONSOLE | OUTPUT_TOAST);
			}
		}
		else if (bWeaponSlot)
			s_aServerWeaponChoices[iIndex] = {};
		if (uSlot == uItemID)
			continue;

		auto* pView = uItemID ? FindItemView(pInventory, uItemID) : nullptr;
		if (uItemID && !pView)
			continue;

		if (uSlot && !IsClientItem(uSlot))
		{
			std::lock_guard tLock(m_tMutex);
			m_mOriginalLoadout[iIndex] = uSlot;
		}
		uSlot = uItemID;
		Field<bool>(pInventory, kInventoryLoadoutChanged + iClass) = true;
		if (pView)
			EquipItem(GetSOCData(pView), iClass, iSlot);
	}
}

void CSkinChanger::SyncInventory()
{
	const uint64_t uNow = GetTickCount64();
	if (uNow - m_uLastSync < 250)
		return;
	m_uLastSync = uNow;

	auto* pInventory = GetLocalInventory();
	if (!pInventory || !Field<bool>(pInventory, kInventoryGotItems) || !Field<void*>(pInventory, kInventorySOCache))
		return;

	ProcessCrates(pInventory);

	auto* pData = Field<uint8_t*>(pInventory, kInventoryItemsData);
	const int iCount = Field<int>(pInventory, kInventoryItemsCount);
	const int iMax = std::max(MaxItems(pInventory), 0);
	if (!pData)
		return;

	std::unordered_set<uint64_t> tPresent = {};
	std::unordered_map<uint16_t, int> mDefCount = {};
	std::vector<bool> vUsed(size_t(iMax) + 1, false);
	tPresent.reserve(size_t(iCount));
	for (int i = 0; i < iCount; i++)
	{
		auto* pView = reinterpret_cast<CEconItemView*>(pData + size_t(i) * kEconItemViewSize);
		tPresent.insert(ViewItemID(pView));
		if (const uint16_t iDef = Field<uint16_t>(pView, kViewDefIndex))
			mDefCount[iDef]++;

		const uint32_t uToken = Field<uint32_t>(pView, kViewInventoryPos);
		if (uToken && !(uToken & kPositionUnacknowledged))
		{
			if (const size_t uPosition = uToken & 0xFFFF; uPosition <= size_t(iMax))
				vUsed[uPosition] = true;
		}
	}

	std::vector<BackpackItem_t> vWanted = {};
	std::unordered_set<uint64_t> tWantedIDs = {};
	std::vector<uint64_t> vRemove = {};
	{
		std::lock_guard tLock(m_tMutex);
		vWanted = m_vBackpack;
		for (const auto& tItem : vWanted)
			tWantedIDs.insert(ClientItemID(tItem.uSerial, tItem.iDef));
		for (const auto& [uID, pItem] : m_mClientItemsByID)
		{
			if (!tWantedIDs.contains(uID))
				vRemove.push_back(uID);
		}
		for (auto it = m_tFailedItems.begin(); it != m_tFailedItems.end();)
			it = tWantedIDs.contains(*it) ? std::next(it) : m_tFailedItems.erase(it);
	}

	for (uint64_t uID : vRemove)
	{
		if (tPresent.erase(uID))
		{
			if (--mDefCount[ClientItemDef(uID)] <= 0)
				mDefCount.erase(ClientItemDef(uID));
		}
		RemoveClientItem(pInventory, uID);
	}

	uint32_t uAccountID = Field<uint32_t>(pInventory, kInventoryOwner);
	if (!uAccountID && I::SteamUser)
		uAccountID = I::SteamUser->GetSteamID().GetAccountID();

	uint32_t uNextPosition = 1;
	auto fFreePosition = [&]() -> uint32_t
	{
		while (uNextPosition <= uint32_t(iMax) && vUsed[uNextPosition])
			uNextPosition++;
		return uNextPosition <= uint32_t(iMax) ? uNextPosition : 0;
	};
	for (const auto& tItem : vWanted)
	{
		if (!uAccountID)
			break;

		const uint16_t iDef = tItem.iDef;
		const uint64_t uID = ClientItemID(tItem.uSerial, iDef);
		const int iUnusual = std::max(tItem.iUnusual, 0);

		void* pItem = GetClientSOCData(uID);
		if (tPresent.contains(uID))
		{
			const auto itApplied = m_mAppliedUnusual.find(uID);
			auto* pExistingView = FindItemView(pInventory, uID);
			auto* pAttributes = pExistingView ? reinterpret_cast<CAttributeList*>(uintptr_t(pExistingView) + kItemViewAttributeList) : nullptr;
			const bool bStyleChanged = pAttributes && int(pAttributes->GetValue(kAttrStyleOverride)) != tItem.iStyle;
			const bool bPaintChanged = pAttributes && (int(pAttributes->GetValue(kAttrItemTint)) != tItem.iPaintColor || int(pAttributes->GetValue(kAttrItemTint2)) != tItem.iPaintColor2);
			if (pItem && ((itApplied != m_mAppliedUnusual.end() ? itApplied->second : 0) != iUnusual || bPaintChanged || bStyleChanged))
			{
				if (auto* pView = FindItemView(pInventory, uID))
				{
					ApplyUnusual(pView, pItem, iUnusual);
					ApplyPaintAttributes(pView, tItem.iPaintColor, tItem.iPaintColor2);
		reinterpret_cast<CAttributeList*>(uintptr_t(pView) + kItemViewAttributeList)->SetAttribute(kAttrStyleOverride, float(tItem.iStyle));
					m_mAppliedUnusual[uID] = iUnusual;
				}
			}
			continue;
		}
		if (m_tFailedItems.contains(uID))
			continue;

		const uint32_t uPosition = fFreePosition();
		if (!uPosition)
			break;

		const bool bCreated = !pItem;
		if (bCreated)
		{
			pItem = CreateEconItem();
			if (!pItem)
			{
				m_tFailedItems.insert(uID);
				continue;
			}

			Field<uint64_t>(pItem, kItemID) = uID;
			Field<uint16_t>(pItem, kItemDefIndex) = iDef;
			Field<uint8_t>(pItem, kItemLevel) = 1;
			Field<uint8_t>(pItem, kItemFlags) = 0;
			Field<uint8_t>(pItem, kItemOrigin) = 2;
		}
		Field<uint32_t>(pItem, kItemAccountID) = uAccountID;
		Field<uint32_t>(pItem, kItemInventoryPos) = PositionToken(uPosition);
		Field<uint8_t>(pItem, kItemQuality) = uint8_t(iUnusual > 0 ? kQualityUnusual : kQualityUnique);

		{
			std::lock_guard tLock(m_tMutex);
			m_mClientItemsByID[uID] = pItem;
		}

		AddEconItem(pInventory, static_cast<CEconItem*>(pItem));
		auto* pView = FindItemView(pInventory, uID);
		if (!pView)
		{
			std::lock_guard tLock(m_tMutex);
			m_mClientItemsByID.erase(uID);
			DestroyEconItem(pItem);
			m_tFailedItems.insert(uID);
			continue;
		}

		Field<void*>(pView, kViewNonSOCItem) = pItem;
		Field<uint64_t>(pView, kViewNonSOCItemID) = uID;
		ApplyUnusual(pView, pItem, iUnusual);
		ApplyPaintAttributes(pView, tItem.iPaintColor, tItem.iPaintColor2);
		reinterpret_cast<CAttributeList*>(uintptr_t(pView) + kItemViewAttributeList)->SetAttribute(kAttrStyleOverride, float(tItem.iStyle));
		m_mAppliedUnusual[uID] = iUnusual;

		vUsed[uPosition] = true;
		tPresent.insert(uID);
		mDefCount[iDef]++;
	}

	std::vector<std::pair<uint64_t, uint64_t>> vAwaiting = {};
	{
		std::lock_guard tLock(m_tMutex);
		vAwaiting.assign(m_mAwaitingPosition.begin(), m_mAwaitingPosition.end());
	}
	for (const auto& [uID, uTick] : vAwaiting)
	{
		if (uNow - uTick < kAwaitPositionTimeout)
			continue;

		bool bDone = !tPresent.contains(uID);
		if (!bDone)
		{
			auto* pView = FindItemView(pInventory, uID);
			const uint32_t uToken = pView ? Field<uint32_t>(pView, kViewInventoryPos) : 0;
			bDone = !pView || (uToken && !(uToken & kPositionUnacknowledged));
			if (!bDone)
			{
				if (const uint32_t uPosition = fFreePosition())
				{
					Field<uint32_t>(pView, kViewInventoryPos) = PositionToken(uPosition);
					if (auto* pItem = GetSOCData(pView))
						Field<uint32_t>(pItem, kItemInventoryPos) = PositionToken(uPosition);
					vUsed[uPosition] = true;
					bDone = true;
				}
			}
		}
		if (bDone)
		{
			std::lock_guard tLock(m_tMutex);
			m_mAwaitingPosition.erase(uID);
		}
	}

	ReapplyLoadout(pInventory);

	std::lock_guard tLock(m_tMutex);
	m_mInventoryCounts = std::move(mDefCount);
	m_bInventoryLoaded = true;
}

namespace
{
	bool IsReadableMemory(const void* pAddress, size_t uSize);

	uintptr_t WearableVMConstructor()
	{
		static uintptr_t uConstructor = 0;
		static bool bResolved = false;
		if (bResolved)
			return uConstructor;
		bResolved = true;
		if (uConstructor || !I::Client)
			return uConstructor;
		for (auto* pClass = I::Client->GetAllClasses(); pClass; pClass = pClass->m_pNext)
		{
			if (strcmp(pClass->GetName(), "CTFWearableVM") || !pClass->m_pCreateFn)
				continue;
			auto* pFactory = reinterpret_cast<uint8_t*>(pClass->m_pCreateFn);
			if (!IsReadableMemory(pFactory, 80))
				return 0;
			for (int i = 0; i <= 64; i++)
			{
				if (!memcmp(pFactory + i, "\x48\x8B\xC8\xE8", 4)
					&& !memcmp(pFactory + i + 8, "\x48\x8B\xD8\x48\x85\xC0", 6))
				{
					uConstructor = U::Memory.RelToAbs(uintptr_t(pFactory + i + 3), 1);
					return uConstructor;
				}
			}
		}
		return 0;
	}

	CEconWearable* CreateWeaponExtra(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, uint16_t iDef, const std::string& sModel, bool bViewModel)
	{
		if (sModel.empty() || !I::ModelInfoClient || !S::Inventory_CBaseEntity_SetModel())
			return nullptr;
		const uintptr_t uConstructor = bViewModel ? WearableVMConstructor() : S::CTFWearable_Constructor();
		if (!uConstructor || (bViewModel && !pLocal->m_hViewModel().Get()))
			return nullptr;
		static const int iAssociatedOffset = U::NetVars.GetNetVar("CTFWearable", "m_hWeaponAssociatedWith");
		if (iAssociatedOffset <= 0)
			return nullptr;
		auto* pMemory = S::MemAlloc_Zero.Call<void*>(kTFWearableSize);
		if (!pMemory)
			return nullptr;
		auto* pWearable = reinterpret_cast<CEconWearable*(__fastcall*)(void*)>(uConstructor)(pMemory);
		Field<uint16_t>(pWearable, kEconEntityAttrManager + kAttrContainerItem + kViewDefIndex) = iDef;
		Field<uint8_t>(pWearable, kEconEntityClientside) = 1;
		S::CBaseEntity_SetOwnerEntity.Call<void>(pWearable, pLocal);
		Field<EHANDLE>(pWearable, iAssociatedOffset).Set(pWeapon);
		if (!pWearable->InitializeAsClientEntity(sModel.c_str(), bViewModel ? kRenderGroupViewModelOpaque : kRenderGroupOpaqueEntity))
		{
			pWearable->ReleaseClientEntity();
			return nullptr;
		}
		pWearable->Spawn();
		pWearable->Equip(pLocal);
		I::ModelInfoClient->RegisterDynamicModel(sModel.c_str(), true);
		S::Inventory_CBaseEntity_SetModel.Call<bool>(pWearable, sModel.c_str());
		pWearable->GetClientNetworkable()->OnDataChanged(DATA_UPDATE_CREATED);
		pWearable->SetNextClientThink(kClientThinkAlways);
		return pWearable;
	}

	void UpdateWeaponExtras(WeaponVisual_t& tVisual, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, uint16_t iDef)
	{
		const auto* pInfo = FindItemInfo(iDef);
		if (!pInfo)
			return;
		const uint64_t uNow = GetTickCount64();
		const bool bTryCreate = uNow >= tVisual.uNextExtraAttempt;
		if (bTryCreate)
			tVisual.uNextExtraAttempt = uNow + 1000;
		if (bTryCreate && !tVisual.hExtraWorld.Get() && !pInfo->sExtraModel.empty())
		{
			if (auto* pExtra = CreateWeaponExtra(pLocal, pWeapon, iDef, pInfo->sExtraModel, false))
				tVisual.hExtraWorld.Set(pExtra);
		}
		if (bTryCreate && !tVisual.hExtraVM.Get() && !pInfo->sExtraModelVM.empty())
		{
			if (auto* pExtra = CreateWeaponExtra(pLocal, pWeapon, iDef, pInfo->sExtraModelVM, true))
				tVisual.hExtraVM.Set(pExtra);
		}
		if (tVisual.hExtraWorld.Get())
			pWeapon->m_hExtraWearable().Set(tVisual.hExtraWorld.Get());
		if (tVisual.hExtraVM.Get())
			pWeapon->m_hExtraWearableViewModel().Set(tVisual.hExtraVM.Get());
	}

    int WorldModelCacheOffset()
    {
        static const int offset = []
        {
            const auto address = S::Inventory_WorldModelCacheField();
            return address && IsReadableMemory(reinterpret_cast<void*>(address), 51)
                ? WeaponModelPolicy::WorldCacheOffset({reinterpret_cast<const uint8_t*>(address), 51}) : 0;
        }();
        return offset;
    }

    void ApplyActiveViewModel(CTFWeaponBase* weapon, int modelIndex)
    {
        auto* owner = weapon->GetOwnerEntity();
        auto* local = H::Entities.GetLocal();
        if (!local || owner != local || local->m_hActiveWeapon().Get() != weapon
            || local->InCond(TF_COND_TAUNTING) || modelIndex <= 0) return;
        auto* viewModel = local->m_hViewModel().Get();
        if (!viewModel || viewModel->m_nModelIndex() == modelIndex) return;
        auto* model = I::ModelInfoClient->GetModel(modelIndex);
        const char* path = model ? I::ModelInfoClient->GetModelName(model) : nullptr;
        if (path && *path)
        {
            S::Inventory_CBaseEntity_SetModel.Call<bool>(viewModel, path);
            viewModel->As<CBaseAnimating>()->InvalidateBoneCache();
            weapon->UpdateAllViewmodelAddons();
        }
    }

    void RefreshWeaponModels(CTFWeaponBase* weapon)
    {
        if (!I::ModelInfoClient || !S::Inventory_CBaseEntity_SetModel()) return;
        auto* owner = weapon->GetOwnerEntity();
        if (!owner || !owner->IsPlayer()) return;
        auto* player = owner->As<CTFPlayer>();
        if (player->InCond(TF_COND_TAUNTING)) return;
        // Native GetWorldModel honors model_world overrides (e.g. Hot Hand),
        // unlike GetPlayerDisplayModel used by cosmetics. Slot 320 is verified
        // beside GetViewModel in the current x64 C_TFShovel vtable.
        const char* world = U::Memory.CallVirtual<320, const char*>(weapon);
        if (world && *world)
        {
            I::ModelInfoClient->RegisterDynamicModel(world, true);
            if (const int offset = WorldModelCacheOffset()) Field<int>(weapon, offset) = 0;
            weapon->GetWorldModelIndex(); // Recompute the engine cache using the selected econ item.
            S::Inventory_CBaseEntity_SetModel.Call<bool>(weapon, world);
        }
        // All-class c_models need class hands instead of the stock v_shovel/v_bat.
        // This is the native UpdateHands operation, also used when restoring an item.
        const char* hands = weapon->GetWeaponViewModel(0);
        if (hands && *hands)
        {
            I::ModelInfoClient->RegisterDynamicModel(hands, true);
            const int index = I::ModelInfoClient->GetModelIndex(hands);
            if (index > 0)
            {
                weapon->m_iViewModelIndex() = index;
                ApplyActiveViewModel(weapon, index);
            }
        }
    }

    void KeepWeaponModels(WeaponVisual_t& visual, CTFWeaponBase* weapon)
    {
        auto* owner = weapon->GetOwnerEntity();
        if (!owner || !owner->IsPlayer() || owner->As<CTFPlayer>()->InCond(TF_COND_TAUNTING)) return;
        if ((visual.iWorldModel <= 0 || visual.iViewModel <= 0) && GetTickCount64() >= visual.uNextModelAttempt)
        {
            visual.uNextModelAttempt = GetTickCount64() + 1000;
            RefreshWeaponModels(weapon);
            weapon->GetClientNetworkable()->OnDataChanged(DATA_UPDATE_DATATABLE_CHANGED);
            weapon->UpdateAllViewmodelAddons();
            visual.iWorldModel = weapon->m_iWorldModelIndex();
            visual.iViewModel = weapon->m_iViewModelIndex();
        }
        // Network/prediction can restore server indices without changing the client item ID.
        if (visual.iWorldModel > 0)
        {
            weapon->m_iWorldModelIndex() = visual.iWorldModel;
            if (const int offset = WorldModelCacheOffset()) Field<int>(weapon, offset) = visual.iWorldModel;
        }
        if (visual.iViewModel > 0)
        {
            weapon->m_iViewModelIndex() = visual.iViewModel;
            ApplyActiveViewModel(weapon, visual.iViewModel);
        }
    }

	void RefreshWeaponVisual(CTFWeaponBase* pWeapon)
	{
		auto& hAttachment = Field<CHandle<CBaseAnimating>>(pWeapon, 0xE50);
		if (auto* pAttachment = hAttachment.Get())
		{
			U::Memory.CallVirtual<1, void>(uintptr_t(pAttachment) + 0x10);
		}
		hAttachment.Term();
		S::CEconEntity_InitializeAttributes.Call<void>(pWeapon);
        RefreshWeaponModels(pWeapon);
		pWeapon->GetClientNetworkable()->OnDataChanged(DATA_UPDATE_DATATABLE_CHANGED);
		pWeapon->UpdateAttachmentModels();
		pWeapon->UpdateAllViewmodelAddons();
		pWeapon->InvalidateBoneCache();
	}

	void RestoreExtraWearable(CHandle<CEconWearable>* pHandle)
	{
		if (!pHandle->IsValid())
			return;
		if (auto* pExtra = pHandle->Get())
		{
			if (auto* pOwner = pExtra->GetOwnerEntity(); pOwner && pOwner->IsPlayer())
				pExtra->UnEquip(reinterpret_cast<CBasePlayer*>(pOwner));
			pExtra->ReleaseClientEntity();
		}
		pHandle->Term();
	}

	void ResetWeaponVisual(WeaponVisual_t& tVisual)
	{
		auto* pWeapon = tVisual.hEntity.Get();
		if (pWeapon)
		{
			if (pWeapon->m_hExtraWearableViewModel().ToInt() == tVisual.hExtraVM.ToInt())
				pWeapon->m_hExtraWearableViewModel() = tVisual.hOriginalExtraVM;
			if (pWeapon->m_hExtraWearable().ToInt() == tVisual.hExtraWorld.ToInt())
				pWeapon->m_hExtraWearable() = tVisual.hOriginalExtraWorld;
		}
		RestoreExtraWearable(&tVisual.hExtraVM);
		RestoreExtraWearable(&tVisual.hExtraWorld);
		tVisual.iStyle = -1;
        tVisual.iWorldModel = tVisual.iViewModel = 0;
        tVisual.uNextModelAttempt = 0;
		tVisual.uNextExtraAttempt = 0;
		if (auto* pWeapon = tVisual.bConstructed ? tVisual.hEntity.Get() : nullptr)
		{
			auto* pView = reinterpret_cast<CEconItemView*>(uintptr_t(pWeapon) + kEconEntityAttrManager + kAttrContainerItem);
			if (ViewItemID(pView) == tVisual.uClientID)
			{
				AssignItemView(pView, reinterpret_cast<CEconItemView*>(tVisual.aOriginal));
			}
            // The server may already have restored the econ view, while our model
            // cache still contains the client item. Rebuild in either case.
            RefreshWeaponVisual(pWeapon);
		}
		if (tVisual.bConstructed)
			DestroyItemView(tVisual.aOriginal);
		tVisual.hEntity.Term();
		tVisual.uClientID = 0;
		tVisual.bConstructed = false;
		tVisual.hOriginalExtraWorld.Term();
		tVisual.hOriginalExtraVM.Term();
	}
}

void CSkinChanger::RestoreWeaponVisuals()
{
	for (auto& tVisual : s_aWeaponVisuals)
	{
		if (tVisual.hEntity.IsValid())
		{
			std::lock_guard tLock(m_tMutex);
			m_mTracked.erase(tVisual.hEntity.ToInt());
		}
		ResetWeaponVisual(tVisual);
	}
}

void CSkinChanger::PrepareWeaponDeploy(CTFWeaponBase* weapon)
{
    auto* entity = I::EngineClient && I::ClientEntityList
        ? I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer()) : nullptr;
    auto* local = entity ? entity->As<CTFPlayer>() : nullptr;
    if (!weapon || !local || !local->IsPlayer() || weapon->GetOwnerEntity() != local || G::Unload
        || !Vars::Visuals::SkinChanger::Enabled.Value || SDK::CleanScreenshot()) return;
    // Deploy selects hand activities and emits draw sounds before FRAME_RENDER_START.
    // Preparing only at render time leaves the server weapon's animation/sound selected.
    static bool preparing = false;
    if (preparing) return;
    preparing = true;
    UpdateWeaponVisuals(local);
    preparing = false;
}

void CSkinChanger::UpdateWeaponVisuals(CTFPlayer* pLocal)
{
	if (!Vars::Visuals::SkinChanger::Enabled.Value || !pLocal || !pLocal->IsAlive())
	{
		RestoreWeaponVisuals();
		return;
	}
	const int iClass = pLocal->m_iClass();
	if (iClass < 1 || iClass > 9)
	{
		RestoreWeaponVisuals();
		return;
	}
	auto* pInventory = GetLocalInventory();
	if (!pInventory)
	{
		RestoreWeaponVisuals();
		return;
	}
	std::array<uint64_t, 3> aSelected = {};
	{
		std::lock_guard tLock(m_tMutex);
		for (int iSlot = 0; iSlot < 3; iSlot++)
		{
			const auto it = m_mLoadout.find(iClass * kLoadoutSlots + iSlot);
			if (it != m_mLoadout.end() && IsClientItem(it->second))
				aSelected[iSlot] = it->second;
		}
	}
	auto& vWeapons = pLocal->m_hMyWeapons();
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto& tVisual = s_aWeaponVisuals[i];
		auto* pWeapon = vWeapons[i].Get();
		const int iSlot = pWeapon ? pWeapon->GetSlot() : -1;
		const uint64_t uSelected = iSlot >= 0 && iSlot < 3 ? aSelected[iSlot] : 0;
		auto* pSelected = uSelected ? FindItemView(pInventory, uSelected) : nullptr;
		auto* pView = pWeapon ? reinterpret_cast<CEconItemView*>(uintptr_t(pWeapon) + kEconEntityAttrManager + kAttrContainerItem) : nullptr;
		const bool bConstructed = tVisual.bConstructed && tVisual.hEntity.Get() == pWeapon;
		if (tVisual.bConstructed && !bConstructed)
		{
			{
				std::lock_guard tLock(m_tMutex);
				m_mTracked.erase(tVisual.hEntity.ToInt());
			}
			ResetWeaponVisual(tVisual);
		}
		if (bConstructed && (tVisual.uClientID != uSelected || !pView || ViewItemID(pView) != tVisual.uClientID
			|| !pSelected || ViewItemID(pSelected) != uSelected
			|| int(reinterpret_cast<CAttributeList*>(uintptr_t(pSelected) + kItemViewAttributeList)->GetValue(kAttrStyleOverride)) != tVisual.iStyle))
		{
			{
				std::lock_guard tLock(m_tMutex);
				m_mTracked.erase(tVisual.hEntity.ToInt());
			}
			ResetWeaponVisual(tVisual);
		}
		if (!pWeapon || !pView || !pSelected)
			continue;
		const uint16_t iSelectedDef = Field<uint16_t>(pSelected, kViewDefIndex);
		const uint16_t iOriginalDef = Field<uint16_t>(pView, kViewDefIndex);
		if (LoadoutWeaponType(iSelectedDef, iClass, iSlot) < 0
            || LoadoutWeaponType(iSelectedDef, iClass, iSlot) != LoadoutWeaponType(iOriginalDef, iClass, iSlot))
			continue;
		if (tVisual.bConstructed && ViewItemID(pView) == uSelected)
		{
            KeepWeaponModels(tVisual, pWeapon);
			UpdateWeaponExtras(tVisual, pLocal, pWeapon, iSelectedDef);
			continue;
		}
		if (!ConstructItemView(tVisual.aOriginal))
			continue;
		tVisual.bConstructed = true;
		AssignItemView(reinterpret_cast<CEconItemView*>(tVisual.aOriginal), pView);
		tVisual.hEntity.Set(pWeapon);
		tVisual.uClientID = uSelected;
		tVisual.iStyle = int(reinterpret_cast<CAttributeList*>(uintptr_t(pSelected) + kItemViewAttributeList)->GetValue(kAttrStyleOverride));
		tVisual.hOriginalExtraWorld = pWeapon->m_hExtraWearable();
		tVisual.hOriginalExtraVM = pWeapon->m_hExtraWearableViewModel();
		AssignItemView(pView, pSelected);
		RefreshWeaponVisual(pWeapon);
        tVisual.iWorldModel = pLocal->InCond(TF_COND_TAUNTING) ? 0 : pWeapon->m_iWorldModelIndex();
        tVisual.iViewModel = pLocal->InCond(TF_COND_TAUNTING) ? 0 : pWeapon->m_iViewModelIndex();
		UpdateWeaponExtras(tVisual, pLocal, pWeapon, iSelectedDef);
	}
}

CEconItemView* CSkinChanger::GetWearableView(int iClass, int iSlot, int iTeam, int iDef, int iUnusual, int iPaintColor, int iPaintColor2, int iStyle)
{
	if (iSlot < 0 || iSlot >= 19 || iDef <= 0)
		return nullptr;

	const uint64_t uKey = (uint64_t(uint32_t(iClass) & 0xF) << 44) | (uint64_t(uint32_t(iTeam) & 0xF) << 40)
		| (uint64_t(uint32_t(iUnusual) & 0xFFFF) << 24) | uint64_t(uint32_t(iDef) & 0xFFFF);
	auto& tView = m_aWearableViews[iSlot];
	auto* pView = reinterpret_cast<CEconItemView*>(tView.aData);
	auto* pCachedAttributes = reinterpret_cast<CAttributeList*>(tView.aData + kItemViewAttributeList);
	if (m_aWearableViewBuilt[iSlot] && m_aWearableViewKey[iSlot] == uKey && Field<uint16_t>(pView, kViewDefIndex) == uint16_t(iDef)
		&& int(pCachedAttributes->GetValue(kAttrItemTint)) == iPaintColor && int(pCachedAttributes->GetValue(kAttrItemTint2)) == iPaintColor2
		&& int(pCachedAttributes->GetValue(kAttrStyleOverride)) == iStyle)
		return pView;

	if (m_aWearableViewBuilt[iSlot])
	{
		DestroyItemView(pView);
		m_aWearableViewBuilt[iSlot] = false;
	}
	if (!ConstructItemView(tView.aData))
		return nullptr;
	m_aWearableViewBuilt[iSlot] = true;

	Field<uint16_t>(pView, kViewDefIndex) = uint16_t(iDef);
	Field<int>(pView, kViewTeamNumber) = iTeam;
	Field<bool>(pView, kViewInitialized) = true;

	const uint32_t uHigh = 0xFFFF0000u | uint32_t(iClass);
	const uint32_t uLow = 0x80000000u | (uint32_t(iDef) << 8) | uint32_t(iSlot);
	auto pMirror = reinterpret_cast<uint32_t*>(tView.aData + 88);
	pMirror[0] = uLow;
	pMirror[1] = uHigh;
	auto pID = reinterpret_cast<uint32_t*>(tView.aData + 96);
	pID[0] = uHigh;
	pID[1] = uLow;

	ApplyUnusual(pView, nullptr, iUnusual);
	ApplyPaintAttributes(pView, iPaintColor, iPaintColor2);
	reinterpret_cast<CAttributeList*>(uintptr_t(pView) + kItemViewAttributeList)->SetAttribute(kAttrStyleOverride, float(iStyle));
	m_aWearableViewKey[iSlot] = uKey;
	return pView;
}

CEconWearable* CSkinChanger::CreateWearable(CTFPlayer* pLocal, CEconItemView* pView, int iClass, int iTeam)
{
	if (!pLocal || !pView || !S::CTFWearable_Constructor()
		|| !S::CEconItemView_GetPlayerDisplayModel() || !S::CBaseEntity_SetOwnerEntity() || !S::MemAlloc_Zero())
		return nullptr;

	const char* sModel = S::CEconItemView_GetPlayerDisplayModel.Call<const char*>(pView, iClass, iTeam);
	if (!sModel || !sModel[0] || !I::ModelInfoClient)
		return nullptr;

	auto* pWearable = S::CTFWearable_Constructor.Call<CEconWearable*>(S::MemAlloc_Zero.Call<void*>(kTFWearableSize));
	if (!pWearable)
		return nullptr;

    auto* entityView=WearableItemView(pWearable);
    if(!entityView){pWearable->ReleaseClientEntity();return nullptr;}
    AssignItemView(entityView,pView);
    // CEconEntity::InitializeAsClientEntity sets its own client-side flag.
    // No duplicate write into a hard-coded private field is needed.
	S::CBaseEntity_SetOwnerEntity.Call<void>(pWearable, pLocal);

	if (!pWearable->InitializeAsClientEntity(sModel, kRenderGroupOpaqueEntity))
	{
		pWearable->ReleaseClientEntity();
		return nullptr;
	}

	pWearable->Spawn();
	pWearable->Equip(pLocal);
	pWearable->GetClientNetworkable()->OnDataChanged(DATA_UPDATE_CREATED);
	pWearable->SetNextClientThink(kClientThinkAlways);
	return pWearable;
}

bool CSkinChanger::AppendWearable(CTFPlayer* pLocal, CEconWearable* pWearable)
{
	if (!pLocal || !pWearable)
		return false;

	const int iHandle = pWearable->GetRefEHandle().ToInt();
	auto& tVector = pLocal->m_hMyWearables();
	for (int i = 0; i < tVector.Count(); i++)
	{
		if (tVector[i].ToInt() == iHandle)
			return true;
	}

	if (tVector.Count() >= kMaxWearables)
		return false;

	const int iIndex = tVector.Count();
	if (!ResizeWearables(pLocal, iIndex + 1))
		return false;

	tVector[iIndex] = CHandle<CEconWearable>(pWearable);
	return true;
}

void CSkinChanger::RemoveWearable(CTFPlayer* pLocal, Wearable_t& tWearable)
{
	auto* pEntity = tWearable.hEntity.Get();
	tWearable.hEntity.Term();
    tWearable.uItemID=0;
	tWearable.iDef = -1;
	tWearable.iClass = -1;
	tWearable.iTeam = -1;
	tWearable.iUnusual = -1;
	tWearable.iPaintColor = -1;
	tWearable.iPaintColor2 = -1;
	tWearable.iStyle = -1;
	if (!pEntity)
		return;

	if (pLocal)
	{
		const int iHandle = pEntity->GetRefEHandle().ToInt();
		auto& tVector = pLocal->m_hMyWearables();
		for (int i = tVector.Count() - 1; i >= 0; i--)
		{
			if (tVector[i].ToInt() != iHandle)
				continue;

			for (int j = i; j < tVector.Count() - 1; j++)
				tVector[j] = tVector[j + 1];

			ResizeWearables(pLocal, tVector.Count() - 1);
			break;
		}

		pEntity->UnEquip(pLocal);
	}

	pEntity->ReleaseClientEntity();
}

void CSkinChanger::ResetWearables(CTFPlayer* pLocal)
{
    auto* owner=m_hWearableOwner.Get();if(!owner)owner=pLocal;
    bool changed=false;
    // Free client slots and remove their providers before restoring server items.
    for(auto& wearable:m_aWearables)
    {changed|=wearable.hEntity.Get()!=nullptr;RemoveWearable(owner,wearable);}
    for(const auto& hidden:m_aHiddenWearables)changed|=hidden.hEntity.Get()!=nullptr;
    RestoreHiddenWearables();
    m_tWearableSelection={};m_iWearableClass=m_iWearableTeam=0;
    if(changed&&owner)
    {owner->UpdateWearables();owner->SetBodygroupsDirty();RefreshInventoryHudModel();}
    m_hWearableOwner.Term();
}

void CSkinChanger::ClearWearables()
{
    ResetWearables(m_hWearableOwner.Get());
    for(auto& key:m_aWearableViewKey)key=0;
}

void CSkinChanger::RestoreHiddenWearables()
{
    for(auto& hidden:m_aHiddenWearables)
    {
        auto* entity=hidden.hEntity.Get();auto* owner=hidden.hOwner.Get();
        if(!entity){hidden={};continue;}
        // Never reparent an entity the server has already given to someone else.
        if(owner&&(!entity->GetOwnerEntity()||entity->GetOwnerEntity()==owner))
        {
            if(!AppendWearable(owner,entity))continue;
            entity->Equip(owner);
            entity->GetClientNetworkable()->OnDataChanged(DATA_UPDATE_DATATABLE_CHANGED);
        }
        if(hidden.bWasHidden)entity->m_fEffects()|=EF_NODRAW;
        else entity->m_fEffects()&=~EF_NODRAW;
        hidden={};
    }
}

bool CSkinChanger::HideReplacedWearables(CTFPlayer* local,int /*playerClass*/)
{
    bool changed=false;
    auto detachHandle=[&](int handle)
    {
        auto& list=local->m_hMyWearables();
        std::array<CHandle<CEconWearable>,32> kept{};
        if(list.Count()>int(kept.size()))
        {SDK::Output("Inventory changer","Wearable list exceeds the reconciliation limit.",WARNING_COLOR);return;}
        int count=0;
        for(int i=0;i<list.Count();++i)if(list[i].ToInt()!=handle)kept[count++]=list[i];
        if(count==list.Count())return;
        if(!ResizeWearables(local,count))
        {SDK::Output("Inventory changer","Cannot resize the local wearable list; will retry.",WARNING_COLOR);return;}
        for(int i=0;i<count;++i)list[i]=kept[i];
        changed=true;
    };
    // Network updates can reattach a suppressed entity without changing its ID.
    for(auto& hidden:m_aHiddenWearables)
    {
        auto* entity=hidden.hEntity.Get();
        if(!entity){hidden={};continue;}
        if(hidden.hOwner.Get()!=local)continue;
        if(auto* owner=entity->GetOwnerEntity();owner&&owner!=local)
        {
            if(!hidden.bWasHidden)entity->m_fEffects()&=~EF_NODRAW;
            hidden={};continue;
        }
        if(entity->GetOwnerEntity()==local){entity->UnEquip(local);changed=true;}
        entity->m_fEffects()|=EF_NODRAW;
        detachHandle(entity->GetRefEHandle().ToInt());
    }
    auto& list=local->m_hMyWearables();
    for(int i=list.Count()-1;i>=0;--i)
    {
        auto* entity=list[i].Get();if(!entity)continue;
        bool ours=false;
        for(const auto& wearable:m_aWearables)if(wearable.hEntity.Get()==entity){ours=true;break;}
        if(ours||entity->IsWearableVM())continue;
        if(auto* owner=entity->GetOwnerEntity();owner&&owner!=local)continue;
        auto* view=WearableItemView(entity);
        if(!view)continue;
        const auto* info=FindItemInfo(Field<uint16_t>(view,kViewDefIndex));
        // Gunboats, shields, Razorback, parachutes and weapon attachments stay.
        if(!info||!info->bCosmetic)continue;
        auto free=std::find_if(m_aHiddenWearables.begin(),m_aHiddenWearables.end(),
            [](const HiddenWearable_t& hidden){return !hidden.hEntity.Get();});
        if(free==m_aHiddenWearables.end())
        {
            SDK::Output("Inventory changer","Cannot track another suppressed cosmetic; keeping its entity intact.",WARNING_COLOR);
            break;
        }
        free->hEntity.Set(entity);free->hOwner.Set(local);free->bWasHidden=bool(entity->m_fEffects()&EF_NODRAW);
        entity->UnEquip(local); // Removes attribute provision and invalidates caches.
        entity->m_fEffects()|=EF_NODRAW;
        detachHandle(entity->GetRefEHandle().ToInt());changed=true;
        i=std::min(i,list.Count());
    }
    return changed;
}

bool CSkinChanger::IsReplacedWearableItem(uint64_t itemID)
{
    if(!itemID||!m_tWearableSelection.managed||IsClientItem(itemID)||m_tWearableSelection.Contains(itemID))return false;
    auto* view=FindItemView(GetLocalInventory(),itemID);
    const auto* info=view?FindItemInfo(Field<uint16_t>(view,kViewDefIndex)):nullptr;
    return info&&info->bCosmetic;
}

void CSkinChanger::UpdateWearables(CTFPlayer* pLocal)
{
	if (!Vars::Visuals::SkinChanger::Enabled.Value)
	{
		if (m_hWearableOwner.IsValid() || pLocal)
			ResetWearables(pLocal);
		return;
	}

	if (!pLocal || !pLocal->IsAlive())
	{
		if (m_hWearableOwner.IsValid() || pLocal)
			ResetWearables(pLocal);
		return;
	}

    if(auto* owner=m_hWearableOwner.Get();owner&&(owner!=pLocal||m_iWearableClass!=pLocal->m_iClass()||m_iWearableTeam!=pLocal->m_iTeamNum()))
    {
        ResetWearables(owner);
    }
	const int iClass = pLocal->m_iClass();
	const int iTeam = pLocal->m_iTeamNum();
	if (iClass < 1 || iClass > 9)
	{
		ResetWearables(pLocal);
		return;
	}

	auto* pInventory = GetLocalInventory();
	if (!pInventory)
		return;

    const auto selection=WearableLoadoutPolicy::Resolve(GetLoadout(),iClass,[&](int slot)
    {
        auto* view=pInventory->GetItemInLoadout(iClass,slot);
        return view?ViewItemID(view):uint64_t(0);
    },kLoadoutSlots);
    if(!selection.managed){ResetWearables(pLocal);return;}
    m_hWearableOwner.Set(pLocal);m_iWearableClass=iClass;m_iWearableTeam=iTeam;m_tWearableSelection=selection;
    bool bBodygroupsDirty=HideReplacedWearables(pLocal,iClass);
    // Remove any legacy cosmetic entities accidentally created in action/taunt slots.
    for(int slot=0;slot<19;++slot)if(!WearableLoadoutPolicy::CosmeticSlot(slot)&&m_aWearables[slot].hEntity.Get())
    {RemoveWearable(pLocal,m_aWearables[slot]);bBodygroupsDirty=true;}
    for(std::size_t index=0;index<WearableLoadoutPolicy::Slots.size();++index)
    {
        const int iSlot=WearableLoadoutPolicy::Slots[index];
		auto& tWearable = m_aWearables[iSlot];
		auto* pEntity = tWearable.hEntity.Get();

        const uint64_t selectedID=selection.items[index];
        auto* pLoadoutView=selectedID?FindItemView(pInventory,selectedID):nullptr;
        const bool bClientItem=pLoadoutView&&IsClientItem(selectedID);
        const int iDef=pLoadoutView?pLoadoutView->GetDefinitionIndex():0;
        const auto* info=iDef>0?FindItemInfo(uint16_t(iDef)):nullptr;
        if(!info||!info->bCosmetic)
		{
			if (pEntity)
			{
				RemoveWearable(pLocal, tWearable);
				bBodygroupsDirty = true;
			}
			continue;
		}

		const int iUnusual = bClientItem ? GetItemUnusual(selectedID) : 0;
		auto* pAttributes = reinterpret_cast<CAttributeList*>(uintptr_t(pLoadoutView) + kItemViewAttributeList);
		const int iPaintColor = int(pAttributes->GetValue(kAttrItemTint));
		const int iPaintColor2 = int(pAttributes->GetValue(kAttrItemTint2));
		const int iStyle = std::max(0, int(pAttributes->GetValue(kAttrStyleOverride)));
		if (pEntity)
		{
			if (tWearable.uItemID != selectedID || tWearable.iDef != iDef || tWearable.iClass != iClass || tWearable.iTeam != iTeam || tWearable.iUnusual != iUnusual
				|| tWearable.iPaintColor != iPaintColor || tWearable.iPaintColor2 != iPaintColor2 || tWearable.iStyle != iStyle)
			{
				RemoveWearable(pLocal, tWearable);
				bBodygroupsDirty = true;
			}
			else
			{
				if (pEntity->GetOwnerEntity() != pLocal)
					pEntity->Equip(pLocal);
				AppendWearable(pLocal, pEntity);
				continue;
			}
		}

		auto* pView=bClientItem?GetWearableView(iClass,iSlot,iTeam,iDef,iUnusual,iPaintColor,iPaintColor2,iStyle):pLoadoutView;
		if (!pView)
			continue;

		auto* pCreated = CreateWearable(pLocal, pView, iClass, iTeam);
		if (!pCreated)
			continue;

		if (!AppendWearable(pLocal, pCreated))
		{
			pCreated->UnEquip(pLocal);
			pCreated->ReleaseClientEntity();
			continue;
		}

		tWearable.hEntity.Set(pCreated);
        tWearable.uItemID=selectedID;
		tWearable.iDef = iDef;
		tWearable.iClass = iClass;
		tWearable.iTeam = iTeam;
		tWearable.iUnusual = iUnusual;
		tWearable.iPaintColor = iPaintColor;
		tWearable.iPaintColor2 = iPaintColor2;
		tWearable.iStyle = iStyle;
		bBodygroupsDirty = true;
	}

	if (bBodygroupsDirty)
	{
        pLocal->UpdateWearables();
		pLocal->SetBodygroupsDirty();
		RefreshInventoryHudModel();
	}
}

void CSkinChanger::CacheMenuInfo(CTFWeaponBase* pWeapon)
{
	std::lock_guard tLock(m_tMutex);

	HeldWeapon_t tHeld = {};
	if (pWeapon)
	{
		if (auto pDef = DefIndex(pWeapon))
		{
			int iOriginal = *pDef;
			for (const auto& [iHandle, tTracked] : m_mTracked)
			{
				if (tTracked.pEntity == pWeapon && tTracked.iApplied == *pDef)
				{
					iOriginal = tTracked.iOriginal;
					break;
				}
			}
			tHeld = { true, Key(iOriginal) };
		}
	}

	m_tHeld = tHeld;
}

namespace
{
	size_t ReadableSpan(const void* pAddress)
	{
		if (!pAddress)
			return 0;

		MEMORY_BASIC_INFORMATION tInfo = {};
		if (!VirtualQuery(pAddress, &tInfo, sizeof(tInfo)) || tInfo.State != MEM_COMMIT)
			return 0;
		if (tInfo.Protect & (PAGE_NOACCESS | PAGE_GUARD))
			return 0;

		return uintptr_t(tInfo.BaseAddress) + tInfo.RegionSize - uintptr_t(pAddress);
	}

	bool IsReadableMemory(const void* pAddress, size_t uSize)
	{
		return uSize && ReadableSpan(pAddress) >= uSize;
	}

	std::string ReadString(const char* sValue)
	{
		const size_t uSpan = ReadableSpan(sValue);
		if (!uSpan || !sValue[0])
			return {};

		size_t uLength = 0;
		const size_t uMaxLength = std::min<size_t>(uSpan, 128);
		while (uLength < uMaxLength && sValue[uLength])
			uLength++;
		return std::string(sValue, uLength);
	}

	int ReadDefinitionPaintColor(void* pDef, uint16_t iAttribute)
	{
		auto* pData = Field<uint8_t*>(pDef, 48);
		const int iCount = Field<int>(pDef, 64);
		for (int i = 0; pData && i < iCount; i++)
		{
			auto* pAttribute = pData + size_t(i) * 16;
			if (Field<uint16_t>(pAttribute, 0) != iAttribute)
				continue;
			const float flColor = Field<float>(pAttribute, 8);
			return std::isfinite(flColor) && flColor >= 0.f && flColor <= 16777215.f ? int(flColor) : -1;
		}
		return -1;
	}

	std::string PrettifyItemName(const std::string& sName)
	{
		std::string sReturn = sName;
		if (!sReturn.empty() && sReturn[0] == '#')
			sReturn.erase(0, 1);
		if (sReturn.starts_with("Upgradeable "))
			sReturn.erase(0, 12);
		if (sReturn.starts_with("TF_WEAPON_"))
			sReturn.erase(0, 10);
		else if (sReturn.starts_with("TF_"))
			sReturn.erase(0, 3);
		if (sReturn.starts_with("ItemName_"))
			sReturn.erase(0, 9);
		std::replace(sReturn.begin(), sReturn.end(), '_', ' ');
		return sReturn;
	}

	std::string LocalizeItemName(const std::string& sName)
	{
		if (sName.empty())
			return {};

		if (I::Localize)
		{
			const std::string sToken = sName[0] == '#' ? sName.substr(1) : sName;
			for (const std::string& sLookup : { sName, sToken })
			{
				const char* sLocalized = I::Localize->FindAsUTF8(sLookup.c_str());
				if (sLocalized && sLocalized[0] && sLookup != sLocalized && sToken != sLocalized)
					return sLocalized;
			}
		}

		return PrettifyItemName(sName);
	}

	bool IsUnresolvedItemName(const std::string& sName)
	{
		return !sName.empty() && sName[0] == '#';
	}
}

const std::vector<ItemInfo_t>& CSkinChanger::GetItems()
{
	static std::vector<ItemInfo_t> s_vItems = {};
	static CEconItemSchema* s_pSchema = nullptr;
	static int s_iSchemaCount = -1;
	static bool s_bLocalizeReady = false;
	static int s_iRetries = 0;
	static bool s_bUnresolved = false;

	const auto pSchema = CEconItemSchema::GetInstance();
	const int iCount = pSchema && IsReadableMemory(pSchema, 344) ? pSchema->GetItemDefCount() : 0;
	const bool bLocalizeReady = I::Localize != nullptr;
	if (iCount <= 0)
		return s_vItems;

	const bool bRetry = s_bUnresolved && ++s_iRetries <= 8;
	if (!bRetry && iCount == s_iSchemaCount && pSchema == s_pSchema && bLocalizeReady == s_bLocalizeReady)
		return s_vItems;

	s_pSchema = pSchema;
	s_iSchemaCount = iCount;
	s_bLocalizeReady = bLocalizeReady;
	if (s_iRetries > 8)
		s_iRetries = 0;
	s_bUnresolved = false;
	s_vItems.clear();
	s_vItems.reserve(iCount);

	s_aSharedMelee.fill(false);
	std::unordered_map<uint16_t, bool> mSeen = {};
	std::unordered_map<std::string, uint16_t> mWeaponNames = { { "TF_WEAPON_BAT", 190 } };
	std::vector<std::pair<uint16_t, std::string>> vWeaponBases;
	std::unordered_map<std::string, uint16_t> mItemNames;
	std::unordered_map<uint16_t, KeyValues*> mDefinitions;
	std::unordered_map<uint16_t, int> mToolTargets;
	for (int i = 0; i < iCount; i++)
	{
		const auto pEntry = pSchema->GetItemDefEntry(i);
		if (!IsReadableMemory(pEntry, 24))
			continue;
		if (*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEntry) + 16) < -1)
			continue;

		const auto pDef = *reinterpret_cast<CTFItemDefinition**>(reinterpret_cast<uintptr_t>(pEntry) + 8);
		if (!IsReadableMemory(pDef, 0x188))
			continue;

		const uint16_t iDef = pDef->GetDefinitionIndex();
		if (!iDef || mSeen.contains(iDef))
			continue;

		const std::string sItemName = ReadString(pDef->m_pszItemName());
		const std::string sName = sItemName.empty() ? ReadString(pDef->m_pszName()) : sItemName;

		ItemInfo_t tItem = {};
		tItem.iDef = iDef;
		tItem.sName = sItemName.empty() ? PrettifyItemName(sName) : LocalizeItemName(sName);
		tItem.sClass = ReadString(pDef->m_pszItemClass());
        s_aSharedMelee[iDef] = tItem.sClass == "saxxy";
		tItem.sImage = ReadString(pDef->m_pszImageInventory());
		mItemNames.emplace(ReadString(pDef->m_pszName()), iDef);
		if (tItem.sClass.starts_with("tf_weapon"))
			mWeaponNames.emplace(ReadString(pDef->m_pszName()), iDef);
		if (auto* pRawDefinition = Field<KeyValues*>(pDef, 8); IsReadableMemory(pRawDefinition, sizeof(KeyValues)))
		{
			mDefinitions.emplace(iDef, pRawDefinition);
            if(auto* classes=pRawDefinition->FindKey("used_by_classes"))
                for(auto* entry=Field<KeyValues*>(classes,48);entry;entry=Field<KeyValues*>(entry,40))
                    tItem.uClassMask|=InventoryClasses::Entry(entry->GetName(),classes->GetString(entry->GetName(),""));
			tItem.bPaintable = pRawDefinition->GetBool("capabilities/paintable", false);
			tItem.bHat = !strcmp(pRawDefinition->GetString("item_slot"), "head");
            const std::string slot=pRawDefinition->GetString("item_slot");
            tItem.bCosmetic=(slot=="head"||slot=="misc")&&(tItem.sClass=="tf_wearable"||tItem.sClass.starts_with("tf_wearable_"));
			tItem.sImageOverlay = ReadString(pRawDefinition->GetString("image_inventory_overlay"));
			tItem.sExtraModel = ReadString(pRawDefinition->GetString("extra_wearable"));
			tItem.sExtraModelVM = ReadString(pRawDefinition->GetString("extra_wearable_vm"));
			if (const int iTarget = ReadDefinitionPaintColor(pDef, 2012); iTarget > 0 && iTarget <= 65535)
				mToolTargets[iDef] = iTarget;
			if (const char* sOffset = pRawDefinition->GetString("static_attrs/tool_target_item_icon_offset"); sOffset && *sOffset)
				sscanf_s(sOffset, "%f %f %f", &tItem.flToolIconX, &tItem.flToolIconY, &tItem.flToolIconScale);
			if (auto* pStyles = pRawDefinition->FindKey("visuals/styles"))
			{
				for (auto* pStyle = Field<KeyValues*>(pStyles, 48); pStyle; pStyle = Field<KeyValues*>(pStyle, 40))
				{
					char* pEnd = nullptr;
					const char* sIndex = pStyle->GetName();
					const long iStyle = strtol(sIndex, &pEnd, 10);
					if (!*sIndex || *pEnd || iStyle < 0 || iStyle > 255)
						continue;
					const std::string sStyleName = ReadString(pStyle->GetString("name"));
					tItem.vStyles.emplace_back(int(iStyle), sStyleName.empty() ? std::format("Style {}", iStyle + 1) : LocalizeItemName(sStyleName));
				}
			}

			if (tItem.sClass.starts_with("tf_weapon"))
			{
				const std::string sBase = ReadString(pRawDefinition->GetString("base_item_name"));
				if (!sBase.empty())
					vWeaponBases.emplace_back(iDef, sBase);
			}
			if (!strcmp(pRawDefinition->GetString("tool/type"), "paint_can"))
			{
				tItem.iPaintColor = ReadDefinitionPaintColor(pDef, kAttrItemTint);
				tItem.iPaintColor2 = ReadDefinitionPaintColor(pDef, kAttrItemTint2);
				tItem.sImageOverlay = ReadString(pRawDefinition->GetString("image_inventory_overlay"));
			}
		}
		if (tItem.sName.empty())
			continue;
		if (IsUnresolvedItemName(tItem.sName))
			s_bUnresolved = true;

		mSeen[iDef] = true;
		s_vItems.push_back(std::move(tItem));
	}

	BuildWeaponTypes(mWeaponNames, vWeaponBases);
	BuildCrateProfiles(mItemNames, mDefinitions);
    // Reskins can inherit their base weapon's class restrictions. Schema data
    // wins; never infer a class from localized display names.
    std::unordered_map<uint16_t,uint32_t> classMasks;
    for(const auto& item:s_vItems) if(item.uClassMask) classMasks.emplace(item.iDef,item.uClassMask);
    for(auto& item:s_vItems)
        if(!item.uClassMask&&item.sClass.starts_with("tf_weapon"))
            if(auto found=classMasks.find(uint16_t(WeaponType(item.iDef)));found!=classMasks.end()) item.uClassMask=found->second;

	for (auto& tItem : s_vItems)
	{
		const auto it = mToolTargets.find(tItem.iDef);
		if (it == mToolTargets.end())
			continue;
		const auto pTarget = std::find_if(s_vItems.begin(), s_vItems.end(), [&](const ItemInfo_t& tTarget) { return tTarget.iDef == it->second; });
		if (pTarget != s_vItems.end())
		{
			tItem.sToolTargetImage = pTarget->sImage;
            if(!tItem.uClassMask) tItem.uClassMask=pTarget->uClassMask;
			if (tItem.sName.find(pTarget->sName) == std::string::npos)
				tItem.sName = pTarget->sName + " " + tItem.sName;
		}
	}

	std::sort(s_vItems.begin(), s_vItems.end(), [](const ItemInfo_t& a, const ItemInfo_t& b)
	{
		return a.sName < b.sName;
	});
	s_uItemCatalogueRevision++;
	return s_vItems;
}

uint64_t CSkinChanger::CatalogueRevision() {return s_uItemCatalogueRevision;}

bool CSkinChanger::IsCatalogueLoading()
{
	if (!I::EngineClient || !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo())
		return false;

	const auto pSchema = CEconItemSchema::GetInstance();
	return !pSchema || !IsReadableMemory(pSchema, 344) || pSchema->GetItemDefCount() <= 0 || GetItems().empty();
}

bool CSkinChanger::InBackpack(uint16_t iDef)
{
	std::lock_guard tLock(m_tMutex);
	return std::any_of(m_vBackpack.begin(), m_vBackpack.end(), [iDef](const BackpackItem_t& tItem) { return tItem.iDef == iDef; });
}

uint32_t CSkinChanger::NextSerial()
{
	for (uint32_t i = 0; i < kClientSerialMask; i++)
	{
		const uint32_t uSerial = m_uNextSerial;
		m_uNextSerial = m_uNextSerial >= kClientSerialMask ? 1 : m_uNextSerial + 1;
		if (uSerial && std::none_of(m_vBackpack.begin(), m_vBackpack.end(), [uSerial](const BackpackItem_t& tItem) { return tItem.uSerial == uSerial; }))
			return uSerial;
	}
	return 0;
}

void CSkinChanger::AddToBackpack(uint16_t iDef, int iUnusual, int iStyle)
{
	if (!iDef || iUnusual > 0xFFFF)
		return;

	std::lock_guard tLock(m_tMutex);
	if (const uint32_t uSerial = NextSerial())
		m_vBackpack.push_back({ uSerial, iDef, std::max(iUnusual, 0), -1, -1, std::max(iStyle, 0) });
}

void CSkinChanger::SetBackpackStyle(uint32_t uSerial, int iStyle)
{
	std::lock_guard tLock(m_tMutex);
	for (auto& tItem : m_vBackpack)
	{
		if (tItem.uSerial == uSerial)
		{
			tItem.iStyle = std::max(iStyle, 0);
			break;
		}
	}
}

void CSkinChanger::RemoveFromBackpack(uint16_t iDef)
{
	std::lock_guard tLock(m_tMutex);
	const auto it = std::find_if(m_vBackpack.rbegin(), m_vBackpack.rend(), [iDef](const BackpackItem_t& tItem) { return tItem.iDef == iDef; });
	if (it != m_vBackpack.rend())
		m_vBackpack.erase(std::next(it).base());
}

void CSkinChanger::ClearBackpack()
{
	std::lock_guard tLock(m_tMutex);
	m_vBackpack.clear();
}

std::vector<BackpackItem_t> CSkinChanger::GetBackpack()
{
	std::lock_guard tLock(m_tMutex);
	return m_vBackpack;
}

void CSkinChanger::SetBackpack(std::vector<BackpackItem_t> vBackpack)
{
	std::lock_guard tLock(m_tMutex);
	m_vBackpack = std::move(vBackpack);
	for (const auto& tItem : m_vBackpack)
		m_uNextSerial = std::max(m_uNextSerial, tItem.uSerial + 1);
	if (m_uNextSerial > kClientSerialMask)
		m_uNextSerial = 1;
}

int CSkinChanger::GetItemUnusual(uint64_t uItemID)
{
	std::lock_guard tLock(m_tMutex);
	const uint32_t uSerial = ClientItemSerial(uItemID);
	const auto it = std::find_if(m_vBackpack.begin(), m_vBackpack.end(), [uSerial](const BackpackItem_t& tItem) { return tItem.uSerial == uSerial; });
	return it != m_vBackpack.end() ? it->iUnusual : 0;
}

// Cosmetic only: consume genuine server deaths; never send or manufacture an event.
void CSkinChanger::CacheGoldWeapons(CTFPlayer* local)
{
    s_goldWeapons = {};
    if (!local || !Vars::Visuals::SkinChanger::Enabled.Value || G::Unload) return;
    auto& weapons = local->m_hMyWeapons();
    for (int i = 0; i < MAX_WEAPONS; ++i)
    {
        auto* weapon = weapons[i].Get();
        if (!weapon) continue;
        auto* view = reinterpret_cast<CEconItemView*>(uintptr_t(weapon) + kEconEntityAttrManager + kAttrContainerItem);
        auto* attrs = reinterpret_cast<CAttributeList*>(uintptr_t(view) + kItemViewAttributeList);
        s_goldWeapons[i] = {weapon->GetWeaponID(), GoldKillPolicy::Identify(
            Field<uint16_t>(view, kViewDefIndex), std::bit_cast<uint32_t>(attrs->GetValue(kAttrAustralium, 0.f)) != 0)};
    }
}

void CSkinChanger::GoldKillEvent(IGameEvent* event)
{
    if (!event || G::Unload || !I::GlobalVars) return;
    const auto hash = FNV1A::Hash32(event->GetName());
    if (hash == FNV1A::Hash32Const("game_newmap") || hash == FNV1A::Hash32Const("client_disconnect")
        || hash == FNV1A::Hash32Const("teamplay_round_start"))
    {
        FlushGoldRagdolls(true);
        s_goldDeaths.Clear();
        s_goldWeapons = {};
        return;
    }
    const int victim = I::EngineClient->GetPlayerForUserID(event->GetInt("userid"));
    if (hash == FNV1A::Hash32Const("player_spawn"))
    {
        if (victim > 0 && victim < int(s_goldPending.size()))
        {
            auto& pending = s_goldPending[victim];
            auto* ragdoll = pending.entity.Get();
            auto* create = pending.create;
            pending = {};
            if (ragdoll && create) FinishRagdoll(ragdoll, create, false);
        }
        s_goldDeaths.Spawn(victim);
        return;
    }
    if (hash != FNV1A::Hash32Const("player_death") || victim <= 0) return;
    GoldKillPolicy::Kind kind = GoldKillPolicy::Kind::None;
    const int attacker = I::EngineClient->GetPlayerForUserID(event->GetInt("attacker"));
    // TF_DEATHFLAG_DEADRINGER = 32. Preserve fake deaths and all server custom damage.
    if (Vars::Visuals::SkinChanger::Enabled.Value && !SDK::CleanScreenshot()
        && attacker == I::EngineClient->GetLocalPlayer() && attacker != victim
        && !(event->GetInt("death_flags") & 32))
    {
        const int weaponID = event->GetInt("weaponid", -1);
        for (const auto& weapon : s_goldWeapons)
            if (weapon.weaponID == weaponID) { kind = weapon.kind; break; }
    }
    s_goldDeaths.Record(victim, event->GetInt("userid"), I::GlobalVars->realtime,
        kind != GoldKillPolicy::Kind::None);
    if (const char* icon = GoldKillPolicy::Icon(kind))
    {
        event->SetString("weapon", icon);
        event->SetString("weapon_logclassname", icon);
    }
}

void CSkinChanger::CreateGoldRagdoll(CTFRagdoll* ragdoll, void(__fastcall* create)(void*))
{
    if (!ragdoll || !I::GlobalVars || G::Unload) { create(ragdoll); return; }
    const float now = I::GlobalVars->realtime;
    const int index = ragdoll->m_iPlayerIndex();
    const int userID = RagdollUserID(ragdoll);
    if (ragdoll->m_bFeignDeath() || SDK::CleanScreenshot() || !Vars::Visuals::SkinChanger::Enabled.Value)
    {
        FinishRagdoll(ragdoll, create, false);
        return;
    }
    if (s_goldDeaths.Ready(index, userID, now))
    {
        FinishRagdoll(ragdoll, create, s_goldDeaths.Consume(index, userID, now));
        return;
    }
    // Entity updates can precede player_death in the same packet. Delay initialization
    // briefly, rather than rebuilding live physics or changing material after creation.
    const bool possible = std::any_of(s_goldWeapons.begin(), s_goldWeapons.end(),
        [](const GoldWeapon& w) { return w.kind != GoldKillPolicy::Kind::None; });
    if (possible && index > 0 && index < int(s_goldPending.size()))
    {
        auto& pending = s_goldPending[index];
        if (pending.create && pending.entity.Get() == ragdoll) return;
        if (pending.create && pending.entity.Get()) FinishRagdoll(pending.entity.Get(), pending.create, false);
        pending.entity.Set(ragdoll);
        pending.create = create;
        pending.queued = now;
        return;
    }
    FinishRagdoll(ragdoll, create, false);
}

void CSkinChanger::FlushGoldRagdolls(bool force)
{
    for (auto& pending : s_goldPending)
    {
        if (!pending.create) continue;
        auto* ragdoll = pending.entity.Get();
        if (!ragdoll) { pending = {}; continue; }
        const float now = I::GlobalVars ? I::GlobalVars->realtime : pending.queued + 1.f;
        const int index = ragdoll->m_iPlayerIndex();
        const int userID = RagdollUserID(ragdoll);
        const bool enabled = Vars::Visuals::SkinChanger::Enabled.Value && !SDK::CleanScreenshot() && !G::Unload;
        if (force || !enabled || s_goldDeaths.Ready(index, userID, now)
            || now < pending.queued || now - pending.queued >= 0.1f)
        {
            const bool gold = !force && enabled && !ragdoll->m_bFeignDeath()
                && s_goldDeaths.Consume(index, userID, now);
            auto* create = pending.create;
            pending = {};
            FinishRagdoll(ragdoll, create, gold);
        }
    }
}

void CSkinChanger::OnLevelShutdown()
{
    FlushGoldRagdolls(true);
    s_goldDeaths.Clear();
    s_goldWeapons = {};
	ResetInventoryHudModel();
	RestoreWeaponVisuals();
	ResetWearables(m_hWearableOwner.Get());
}

void CSkinChanger::Unload()
{
    FlushGoldRagdolls(true);
    s_goldDeaths.Clear();
    s_goldWeapons = {};
	RestoreWeaponVisuals();
	ResetWearables(m_hWearableOwner.Get());
	ClearClientItems();
	{
		std::lock_guard tLock(m_tMutex);
		m_bInventoryLoaded = false;
	}
	m_vGraveyard.clear();
	for (int i = 0; i < 19; i++)
	{
		if (m_aWearableViewBuilt[i])
			DestroyItemView(m_aWearableViews[i].aData);
		m_aWearableViewBuilt[i] = false;
		m_aWearableViewKey[i] = 0;
	}
}

std::vector<const ItemInfo_t*> CSkinChanger::GetInventoryItems()
{
	std::vector<uint16_t> vDefs = {};
	{
		std::lock_guard tLock(m_tMutex);
		vDefs.reserve(m_mInventoryCounts.size());
		for (const auto& [iDef, iCount] : m_mInventoryCounts)
			vDefs.push_back(iDef);
	}

	std::vector<const ItemInfo_t*> vItems = {};
	vItems.reserve(vDefs.size());
	for (uint16_t iDef : vDefs)
	{
		if (const ItemInfo_t* pInfo = FindItemInfo(iDef))
			vItems.push_back(pInfo);
	}

	std::sort(vItems.begin(), vItems.end(), [](const ItemInfo_t* a, const ItemInfo_t* b)
	{
		return a->sName != b->sName ? a->sName < b->sName : a->iDef < b->iDef;
	});
	return vItems;
}

bool CSkinChanger::IsInventoryLoading()
{
	std::lock_guard tLock(m_tMutex);
	return I::EngineClient && I::EngineClient->IsInGame() && !I::EngineClient->IsPlayingDemo() && !m_bInventoryLoaded;
}

int CSkinChanger::GetInventoryCount(uint16_t iDef)
{
	std::lock_guard tLock(m_tMutex);
	const auto it = m_mInventoryCounts.find(iDef);
	return it != m_mInventoryCounts.end() ? it->second : 0;
}

#else

void CSkinChanger::ScheduleRefresh() {}
int CSkinChanger::OriginalDef(CBaseEntity*, int, int iCurrent) const { return iCurrent; }
void CSkinChanger::GoldKillEvent(IGameEvent*) {}
void CSkinChanger::CreateGoldRagdoll(CTFRagdoll* ragdoll, void(__fastcall* create)(void*)) { create(ragdoll); }
void CSkinChanger::FlushGoldRagdolls(bool) {}
void CSkinChanger::CacheGoldWeapons(CTFPlayer*) {}
void CSkinChanger::RunFrame() {}
void CSkinChanger::Apply() {}
void CSkinChanger::PrepareWeaponDeploy(CTFWeaponBase*) {}
void CSkinChanger::Service() {}
void CSkinChanger::CacheMenuInfo(CTFWeaponBase*) {}
void CSkinChanger::OnLevelShutdown() {}
void CSkinChanger::Unload() {}
const std::vector<ItemInfo_t>& CSkinChanger::GetItems() { static std::vector<ItemInfo_t> vItems; return vItems; }
bool CSkinChanger::IsCatalogueLoading() { return false; }
uint64_t CSkinChanger::CatalogueRevision() {return 0;}
void CSkinChanger::RequestFullUpdate() {}
void CSkinChanger::AddToBackpack(uint16_t, int, int) {}
void CSkinChanger::SetBackpackStyle(uint32_t, int) {}
bool CSkinChanger::QueueLocalCrate(uint16_t) { return false; }
void CSkinChanger::RemoveFromBackpack(uint16_t) {}
bool CSkinChanger::InBackpack(uint16_t) { return false; }
std::vector<BackpackItem_t> CSkinChanger::GetBackpack() { return {}; }
void CSkinChanger::SetBackpack(std::vector<BackpackItem_t>) {}
int CSkinChanger::GetInventoryCount(uint16_t) { return 0; }
int CSkinChanger::GetItemUnusual(uint64_t) { return 0; }
uint32_t CSkinChanger::NextSerial() { return 0; }
void CSkinChanger::ClearBackpack() {}
std::vector<const ItemInfo_t*> CSkinChanger::GetInventoryItems() { return {}; }
bool CSkinChanger::IsInventoryLoading() { return false; }
bool CSkinChanger::ClientInventoryActive() { return false; }
bool CSkinChanger::IsClientItem(uint64_t) { return false; }
void* CSkinChanger::GetClientSOCData(uint64_t) { return nullptr; }
void CSkinChanger::ApplyLocalLoadout(uint64_t, int, int) {}
void CSkinChanger::ApplyLocalPosition(uint64_t, uint32_t) {}
void CSkinChanger::ApplyLocalDelete(uint64_t) {}
void CSkinChanger::ClearClientItems() {}
bool CSkinChanger::OnSendMessage(void*) { return false; }
void CSkinChanger::ProcessCrates(CTFPlayerInventory*) {}
bool CSkinChanger::OpenCrate(CTFPlayerInventory*, const PendingCrate_t&) { return false; }
void CSkinChanger::SyncInventory() {}
void CSkinChanger::RemoveClientItem(CTFPlayerInventory*, uint64_t) {}
void CSkinChanger::ReapplyLoadout(CTFPlayerInventory*) {}
std::map<int, uint64_t> CSkinChanger::GetLoadout() { return {}; }
void CSkinChanger::SetLoadout(std::map<int, uint64_t>) {}
CTFPlayerInventory* CSkinChanger::GetLocalInventory() const { return nullptr; }
void CSkinChanger::UpdateWearables(CTFPlayer*) {}
void CSkinChanger::UpdateWeaponVisuals(CTFPlayer*) {}
void CSkinChanger::RestoreWeaponVisuals() {}
void CSkinChanger::ResetWearables(CTFPlayer*) {}
void CSkinChanger::RestoreHiddenWearables() {}
bool CSkinChanger::HideReplacedWearables(CTFPlayer*, int) { return false; }
void CSkinChanger::RemoveWearable(CTFPlayer*, Wearable_t&) {}
void CSkinChanger::ClearWearables() {}
CEconItemView* CSkinChanger::GetWearableView(int, int, int, int, int, int, int, int) { return nullptr; }
CEconWearable* CSkinChanger::CreateWearable(CTFPlayer*, CEconItemView*, int, int) { return nullptr; }
bool CSkinChanger::AppendWearable(CTFPlayer*, CEconWearable*) { return false; }
uint64_t CSkinChanger::FindOwnedWeapon(uint64_t, int, int) { return 0; }
bool CSkinChanger::ApplyLocalPaint(CTFPlayerInventory*, const PendingCrate_t&) { return false; }
bool CSkinChanger::IsReplacedWearableItem(uint64_t) { return false; }

#endif
