#pragma once
#include <mutex>
#include <unordered_set>

// The engine can uncache materials on its loading/render workers while our
// render thread creates or removes them. No engine calls run under this lock:
// reference-count changes can synchronously re-enter the Uncache hook.
template<class Pointer>
class CMaterialRegistry
{
    mutable std::mutex m_mutex;
    std::unordered_set<Pointer> m_materials;
public:
    void Track(Pointer material)
    {
        if(!material) return;
        std::lock_guard lock(m_mutex);
        m_materials.insert(material);
    }
    bool Contains(Pointer material) const
    {
        if(!material) return false;
        std::lock_guard lock(m_mutex);
        return m_materials.contains(material);
    }
    void Erase(Pointer material)
    {
        std::lock_guard lock(m_mutex);
        m_materials.erase(material);
    }
    void Clear()
    {
        std::lock_guard lock(m_mutex);
        m_materials.clear();
    }
};
