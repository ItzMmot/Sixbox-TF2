#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace WearableLoadoutPolicy
{
    // These are separate slots: action (9) and taunt (11) are not cosmetics.
    constexpr std::array<int,3> Slots={7,8,10};
    inline bool CosmeticSlot(int slot){return slot==7||slot==8||slot==10;}
    inline int ItemViewOffset(int definitionOffset,int entityBytes,int itemViewBytes=344)
    {
        constexpr int definitionInView=0x48;
        if(definitionOffset<definitionInView+0x100||itemViewBytes<1||entityBytes<itemViewBytes)return -1;
        const int view=definitionOffset-definitionInView;
        return view<=entityBytes-itemViewBytes?view:-1;
    }
    struct Selection
    {
        bool managed=false;
        std::array<uint64_t,3> items{};
        bool Contains(uint64_t id) const
        {if(!id)return false;for(auto selected:items)if(selected==id)return true;return false;}
    };
    template<class Loadout> Selection Select(const Loadout& loadout,int playerClass,int stride=19)
    {
        Selection result;
        if(playerClass<1||playerClass>9)return result;
        for(std::size_t i=0;i<Slots.size();++i)
        {
            auto it=loadout.find(playerClass*stride+Slots[i]);
            if(it==loadout.end())continue;
            result.managed=true;
            const uint64_t id=it->second;
            if(id&&id!=~uint64_t(0)&&!result.Contains(id))result.items[i]=id;
        }
        return result;
    }
    template<class Loadout,class CurrentItem> Selection Resolve(const Loadout& loadout,int playerClass,CurrentItem current,int stride=19)
    {
        auto result=Select(loadout,playerClass,stride);
        if(!result.managed)return result;
        for(std::size_t i=0;i<Slots.size();++i)
        {
            if(loadout.find(playerClass*stride+Slots[i])!=loadout.end())continue;
            const uint64_t id=current(Slots[i]);
            if(id&&id!=~uint64_t(0)&&!result.Contains(id))result.items[i]=id;
        }
        return result;
    }

}
