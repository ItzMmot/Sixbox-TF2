#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Editable groups never supply raw pointers to the game/render caches. Publish
// allocates only on an actual menu/config change; Acquire does not allocate.
template<class Group,std::size_t Limit>
class CGroupSnapshots
{
public:
    using Storage=std::vector<Group>;
    using Snapshot=std::shared_ptr<Storage>;
private:
    std::atomic<Snapshot> m_published;
public:
    bool Publish(const Storage& groups)
    {
        const auto count=(std::min)(groups.size(),Limit);
        const auto old=Acquire();
        if((!old&&count==0)||(old&&old->size()==count&&std::equal(old->begin(),old->end(),groups.begin()))) return false;
        m_published.store(std::make_shared<Storage>(groups.begin(),groups.begin()+count),std::memory_order_release);
        return true;
    }
    Snapshot Acquire() const {return m_published.load(std::memory_order_acquire);}
};

// Dropdown entries own their names independently of the editable vector and
// its strings (including short-string storage and long-name reallocations).
template<std::size_t Limit>
class CGroupLabels
{
    std::array<std::string,Limit> m_names;
public:
    template<class Groups> void Refresh(const Groups& groups,std::vector<const char*>& entries)
    {
        const auto count=(std::min)(groups.size(),Limit);
        entries.reserve(Limit);entries.clear();
        for(std::size_t i=0;i<count;++i)
        {
            if(m_names[i]!=groups[i].m_sName) m_names[i]=groups[i].m_sName;
            entries.push_back(m_names[i].c_str());
        }
    }
};
