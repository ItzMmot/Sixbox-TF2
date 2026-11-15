#include "../SDK/SDK.h"

#ifndef TEXTMODE
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif

MAKE_SIGNATURE(CTFHudPlayerClass_UpdateModelPanel, "client.dll", "40 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B F9 0F 84 ? ? ? ? 4C 89 74 24", 0x0);

#ifndef TEXTMODE
MAKE_SIGNATURE(CTFPlayerModelPanel_AddCarriedItem, "client.dll", "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F1 48 8B FA B9 58 01 00 00", 0x0);

namespace
{
	void* s_pHudClassPanel = nullptr;
	bool s_bBuildingHudClassModel = false;
    std::array<uint64_t,32> s_seenHudItems{};
    size_t s_seenHudCount=0;
}
#endif

MAKE_HOOK(CTFHudPlayerClass_UpdateModelPanel, S::CTFHudPlayerClass_UpdateModelPanel(), void,
	void* rcx)
{
#ifndef TEXTMODE
	DEBUG_RETURN(CTFHudPlayerClass_UpdateModelPanel, rcx);
	s_pHudClassPanel = rcx;
	const bool bPrevious = s_bBuildingHudClassModel;
    const auto previousItems=s_seenHudItems;const auto previousCount=s_seenHudCount;
    s_seenHudItems={};s_seenHudCount=0;
	s_bBuildingHudClassModel = true;
	CALL_ORIGINAL(rcx);
	s_bBuildingHudClassModel = bPrevious;
    s_seenHudItems=previousItems;s_seenHudCount=previousCount;
#endif
}

#ifndef TEXTMODE
MAKE_HOOK(CTFPlayerModelPanel_AddCarriedItem, S::CTFPlayerModelPanel_AddCarriedItem(), int,
	void* rcx, CEconItemView* pItem)
{
	DEBUG_RETURN(CTFPlayerModelPanel_AddCarriedItem, rcx, pItem);
    if(s_bBuildingHudClassModel&&pItem)
    {
        const uint64_t id=pItem->UUID();
        if(F::SkinChanger.IsReplacedWearableItem(id))return -1;
        if(id&&id!=~uint64_t(0))
        {
            if(std::find(s_seenHudItems.begin(),s_seenHudItems.begin()+s_seenHudCount,id)!=s_seenHudItems.begin()+s_seenHudCount)return -1;
            if(s_seenHudCount<s_seenHudItems.size())s_seenHudItems[s_seenHudCount++]=id;
        }
    }
	return CALL_ORIGINAL(rcx, pItem);
}

void RefreshInventoryHudModel()
{
	if (s_pHudClassPanel)
		S::CTFHudPlayerClass_UpdateModelPanel.Call<void>(s_pHudClassPanel);
}

void ResetInventoryHudModel()
{
	s_pHudClassPanel = nullptr;
	s_bBuildingHudClassModel = false;
    s_seenHudItems={};s_seenHudCount=0;
}
#endif
