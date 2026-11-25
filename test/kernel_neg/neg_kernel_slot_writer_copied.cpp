// Negative-compile test.  This file must fail to compile.
//
// A copy of the writer of a slot would publish the slot a second time, with a
// second row and a second kernel (crucible/KernelCache.h).
//
// Expected diagnostic: the deleted copy constructor of the writer.

#include <crucible/KernelCache.h>
#include <crucible/MerkleDag.h>

#include <utility>

int main() {
    crucible::CompiledKernel const kernel{};
    crucible::KernelCache::KernelRef const held = ::fixy::erase_brand(::fixy::to(kernel));
    crucible::KernelCache::KernelCacheSlot slot{};
    slot.claim(crucible::ContentHash{1})
        .match(
            [held](crucible::KernelCache::KernelCacheSlot::WriterHandle writer) noexcept {
                crucible::KernelCache::KernelCacheSlot::WriterHandle second{writer};
                std::move(writer).publish(crucible::RowHash{1}, held);
                std::move(second).publish(crucible::RowHash{2}, held);
            },
            [](crucible::ContentHash) noexcept {});
    return 0;
}
