#pragma once

// Compiled kernels for a test of the kernel cache.
//
// The cache never reads a kernel, so the address of a kernel stands for it.
// Each kernel is an object of an arena of the KernelArena brand, as each
// KernelRef of the cache names.

#include <crucible/KernelCache.h>
#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace crucible::test {

// The arena of the kernels of a test.
using Kernels = ::fixy::BumpArena<KernelArena>;

// A new arena of kernels.  Its first block holds some thousands of kernels.
[[nodiscard]] inline Kernels make_kernel_arena() noexcept {
    return ::fixy::mint_bump_arena<KernelArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{4096});
}

// A new kernel, at an address of its own.
[[nodiscard]] inline KernelCache::KernelRef make_kernel(Kernels& kernels CRUCIBLE_LIFETIMEBOUND) noexcept {
    return kernels.make<CompiledKernel>(::foundation::effects::testing::test().alloc);
}
void make_kernel(Kernels&&) = delete("a temporary arena releases the kernel at the end of the statement");

// True when the lookup found the kernel.
[[nodiscard]] inline bool is_found(KernelCache::FoundKernel const& found, KernelCache::KernelRef kernel) noexcept {
    return found.match(
        [kernel](KernelCache::KernelRef const& held) noexcept { return ::fixy::same_object(held, kernel); },
        []() noexcept { return false; });
}

}  // namespace crucible::test
