// The frame door with no key on a keyed FixedHeapArray.  A keyed array opens a
// frame only with the LoanKey of its brand.
#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>
int main() {
    auto [paired, key] = ::foundation::core::mint_fixed_array<int, 1>();
    using Brand = typename decltype(key)::brand_type;
    auto words = ::foundation::core::mint_fixed_heap_array<int, 4, alignof(int), Brand>(
        ::foundation::effects::testing::test().alloc, 0);
    words.with_mut([](auto run) noexcept { static_cast<void>(run.size()); });
    return words.at(::foundation::core::index<0>()) + paired.at(::foundation::core::index<0>());
}
