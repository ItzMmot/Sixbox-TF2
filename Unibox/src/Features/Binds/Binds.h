#pragma once
#include "../../SDK/SDK.h"
#include <mutex>

Enum(Bind, Key, Class, WeaponType, ItemSlot, Misc, Group)
namespace BindEnum
{
	Enum(Key, Hold, Toggle, DoubleClick)
	Enum(Class, Scout, Soldier, Pyro, Demoman, Heavy, Engineer, Medic, Sniper, Spy)
	Enum(WeaponType, Hitscan, Projectile, Melee, Throwable)
	//Enum(ItemType, First, Second, Third, Fourth, Fifth, Sixth, Seventh, Eighth, Ninth)
	Enum(Misc, Spectated, SpectatedFirst, SpectatedThird, Zoomed, Aiming)
	Enum(Surface, Any, Ground, Air, Water)
}
Enum(BindVisibility, Always, WhileActive, Hidden)

struct Bind_t
{
	std::string m_sName = "";
	int m_iType = 0;
	int m_iInfo = 0;
	int m_iKey = 0;

	bool m_bEnabled = true;
	int m_iVisibility = BindVisibilityEnum::Always;
	bool m_bNot = false;
	bool m_bActive = false; // effective state (condition && filters)
	bool m_bToggled = false; // persistent toggle/double click state for key binds
	KeyStorage m_tKeyStorage = {};

	int m_iParent = DEFAULT_BIND;

	// extra filters, all must pass for the bind to be active
	int m_iClassMask = 0; // bit per BindEnum::ClassEnum, 0 = any class
	int m_iSurface = BindEnum::SurfaceEnum::Any;
	std::vector<int> m_vItems = {}; // item definition indexes, any equipped one passes, empty = no item filter
	bool m_bFiltersMet = true;

	bool m_bCollapsed = false; // groups, menu only

	bool HasFilters() const { return m_iClassMask || m_iSurface != BindEnum::SurfaceEnum::Any || !m_vItems.empty(); }

	std::vector<BaseVar*> m_vVars = {};
};

class CBinds
{
public:
	void Run();
	void SetVars(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bManage = true);

	bool GetBind(int iID, Bind_t* pBind);

	void AddBind(int iBind, Bind_t& tCond);
	void RemoveBind(int iBind, bool bForce = true);
	int GetParent(int iBind);
	bool HasChildren(int iBind);
	bool WillBeEnabled(int iBind);

	void Move(int i1, int i2);

	static bool CheckFilters(const Bind_t& tBind, CTFPlayer* pLocal);
	static void Describe(const Bind_t& tBind, std::string& sType, std::string& sInfo);
	static std::string DescribeFilters(const Bind_t& tBind);
	static std::string GetItemName(const std::vector<int>& vItems);
	static const std::vector<std::pair<std::string, std::vector<int>>>& GetItemList(); // unique item names -> all definition indexes sharing them

	std::vector<Bind_t> m_vBinds = {};
	bool m_bDisplay = false;
	std::recursive_mutex m_mMutex;
};

ADD_FEATURE(CBinds, Binds);
