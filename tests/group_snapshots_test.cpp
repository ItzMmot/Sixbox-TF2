// tests/group_snapshots_test.cpp — live ESP editing and cached pointer lifetimes.
#include "../SixBox/src/Features/Visuals/Groups/GroupSnapshots.h"
#include <barrier>
#include <cassert>
#include <cstdlib>
#include <new>
#include <thread>
static std::atomic<unsigned> allocations=0;
#if defined(__GNUC__)
#define TEST_NOINLINE __attribute__((noinline))
#else
#define TEST_NOINLINE __declspec(noinline)
#endif
TEST_NOINLINE void* operator new(std::size_t n) {++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
TEST_NOINLINE void operator delete(void* p) noexcept {std::free(p);}
TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
struct Group
{
    std::string m_sName;
    std::vector<std::string> materials;
    bool operator==(const Group&) const = default;
};
int main()
{
    CGroupSnapshots<Group,32> snapshots;
    CGroupLabels<32> labels;
    std::vector<Group> editable{{"short",{"Original"}},{std::string(200,'L'),{"custom"}}};
    std::vector<const char*> names;
    labels.Refresh(editable,names);
    assert(snapshots.Publish(editable));
    auto runtime=snapshots.Acquire(),chamsOwner=runtime;
    const Group* cached=&(*runtime)[0];
    const auto* material=cached->materials[0].c_str();
    // Same GUI frame: the dropdown was populated, then adding forces a move.
    editable.reserve(editable.capacity()+100);
    editable.push_back({"new",{"new material"}});
    assert(std::string(names[0])=="short");
    assert(std::string(names[1])==std::string(200,'L'));
    assert(cached->m_sName=="short"&&std::string(material)=="Original");
    labels.Refresh(editable,names);
    assert(names.size()==3&&std::string(names[2])=="new");
    assert(snapshots.Publish(editable));
    runtime=snapshots.Acquire(); // Next game update; previous Chams keeps its owner.
    assert(cached->m_sName=="short"&&std::string(material)=="Original");
    editable.erase(editable.begin());
    editable[0].m_sName="renamed";
    std::rotate(editable.begin(),editable.begin()+1,editable.end());
    assert(snapshots.Publish(editable));
    assert(cached->m_sName=="short"&&std::string(material)=="Original");
    std::weak_ptr<std::vector<Group>> previous=chamsOwner;
    chamsOwner.reset();assert(previous.expired());
    editable.clear();assert(snapshots.Publish(editable));
    assert(snapshots.Acquire()->empty());
    editable.resize(40,Group{"bounded",{"Original"}});
    assert(snapshots.Publish(editable));labels.Refresh(editable,names);
    assert(snapshots.Acquire()->size()==32&&names.size()==32);
    const unsigned before=allocations.load();
    for(int i=0;i<10000;++i)
    {
        assert(!snapshots.Publish(editable));
        const auto view=snapshots.Acquire();assert(view->size()==32);
        labels.Refresh(editable,names);
    }
    assert(allocations.load()==before);
    // UI publication and render reads do not share mutable vectors/strings.
    std::barrier start(2);std::atomic<bool> done=false;
    std::thread reader([&] {
        start.arrive_and_wait();
        while(!done.load())
        {
            const auto view=snapshots.Acquire();
            for(const auto& group:*view) assert(!group.materials.empty());
        }
    });
    start.arrive_and_wait();
    for(int i=0;i<500;++i)
    {
        editable.resize(1);editable[0].m_sName=std::string(100+unsigned(i)%40,'a');
        editable[0].materials[0]=editable[0].m_sName;
        assert(snapshots.Publish(editable));
    }
    done=true;reader.join();
}
