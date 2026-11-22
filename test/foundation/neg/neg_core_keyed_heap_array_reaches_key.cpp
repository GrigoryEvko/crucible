// A body of the keyed frame door of a FixedHeapArray captures an object that
// holds a Ref of the key.  A function that the body calls could then open a
// second frame of the array.  The gate of the door refuses the body.
#include <foundation/core/FixedHeapArray.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>
namespace {
template <class KeyRef>
struct Holder {
    KeyRef key;
};
}  // namespace
int main() {
    auto [paired, key] = ::foundation::core::mint_fixed_array<int, 1>();
    using Brand = typename decltype(key)::brand_type;
    auto words = ::foundation::core::mint_fixed_heap_array<int, 4, alignof(int), Brand>(
        ::foundation::effects::testing::test().alloc, 0);
    Holder<decltype(::foundation::core::to(key))> const holder{::foundation::core::to(key)};
    key = words.with_mut(std::move(key), [&holder](auto run) noexcept {
        static_cast<void>(holder);
        static_cast<void>(run.size());
    });
    return words.at(::foundation::core::index<0>()) + paired.at(::foundation::core::index<0>());
}
