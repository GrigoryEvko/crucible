// Negative-compile test.  This file must fail to compile.
//
// A copy of the writer of a slot would publish the slot a second time, with a
// second row and a second kernel (crucible/KernelCache.h).
//
// Expected diagnostic: the deleted copy constructor of the writer.

#include <crucible/KernelCache.h>
#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

int main() {
    ::fixy::BumpArena<crucible::KernelArena> kernels =
        ::fixy::mint_bump_arena<crucible::KernelArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{64});
    crucible::KernelCache::KernelRef const held =
        kernels.make<crucible::CompiledKernel>(::foundation::effects::testing::test().alloc);
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
