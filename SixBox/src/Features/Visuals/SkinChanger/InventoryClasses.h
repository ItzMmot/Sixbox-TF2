#pragma once
#include <cstdint>
#include <string_view>

namespace InventoryClasses
{
    inline constexpr uint32_t Classes=(1u<<9)-1,AllClass=1u<<9,Other=1u<<10,All=Classes|AllClass|Other;
    inline constexpr std::string_view Names[]={"scout","soldier","pyro","demoman","heavy","engineer","medic","sniper","spy"};
    inline bool EqualName(std::string_view a,std::string_view b)
    {
        if(a.size()!=b.size()) return false;
        for(std::size_t i=0;i<a.size();++i)
        {
            const char c=a[i]>='A'&&a[i]<='Z'?char(a[i]-'A'+'a'):a[i];
            if(c!=b[i]) return false;
        }
        return true;
    }
    inline uint32_t Entry(std::string_view name,std::string_view value)
    {
        // Values may be bool-like numbers OR slots (primary/secondary/melee).
        if(value.empty()||value=="0"||value.front()=='-'||EqualName(value,"false")) return 0;
        for(unsigned i=0;i<9;++i) if(EqualName(name,Names[i])) return 1u<<i;
        return 0;
    }
    inline bool Matches(uint32_t item,uint32_t selected)
    {
        item&=Classes;selected&=All;
        if(!item) return (selected&Other)!=0;
        return (item&selected&Classes)!=0||(item==Classes&&(selected&AllClass)!=0);
    }
}
