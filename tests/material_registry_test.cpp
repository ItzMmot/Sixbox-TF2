// tests/material_registry_test.cpp — material registration vs engine worker reads.
#include "../SixBox/src/Features/Visuals/Materials/MaterialRegistry.h"
#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <cstdlib>
#include <new>
#include <thread>
static std::atomic<unsigned> allocations=0;
void* operator new(std::size_t size)
{++allocations;if(auto* p=std::malloc(size?size:1)) return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
int main()
{
    CMaterialRegistry<const int*> registry;
    std::array<int,4096> materials{};
    assert(!registry.Contains(nullptr));registry.Track(nullptr);
    registry.Track(&materials[0]);registry.Track(&materials[0]);
    assert(registry.Contains(&materials[0]));
    registry.Erase(&materials[0]);assert(!registry.Contains(&materials[0]));
    // Model synchronous engine re-entry after a removal: the lock must already
    // be released before a refcount call can invoke the hook on this thread.
    registry.Track(&materials[0]);registry.Erase(&materials[0]);
    assert(!registry.Contains(&materials[0]));

    std::barrier start(5);
    std::atomic<int> finished=0;
    std::atomic<unsigned> reads=0;
    auto writer=[&](int parity) {
        start.arrive_and_wait();
        for(int round=0;round<12;++round)
        {
            // Rehash, erase and clear overlap real Contains calls on readers.
            for(int i=parity;i<int(materials.size());i+=2) registry.Track(&materials[i]);
            for(int i=parity;i<int(materials.size());i+=2) registry.Erase(&materials[i]);
            registry.Clear();
        }
        ++finished;
    };
    auto reader=[&] {
        start.arrive_and_wait();unsigned iteration=0;
        do {registry.Contains(&materials[iteration++%materials.size()]);++reads;}
        while(finished.load()!=2);
    };
    std::thread a(writer,0),b(writer,1),c(reader),d(reader);
    start.arrive_and_wait();a.join();b.join();c.join();d.join();
    assert(reads>0);
    registry.Clear();
    for(const auto& material:materials) assert(!registry.Contains(&material));
    for(const auto& material:materials) registry.Track(&material);
    const unsigned before=allocations.load();
    for(int i=0;i<100000;++i) assert(registry.Contains(&materials[i%materials.size()]));
    assert(allocations.load()==before); // Hook lookups do not allocate.
    registry.Clear();
}
