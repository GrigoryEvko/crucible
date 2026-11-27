// The body of the element door of a HeapArray of arrays replaces the element
// that it reads, through a Ref of the array that the body captures.  The
// replace gives the storage of the inner array back to the allocator while
// the body holds a View of it.  A HeapArray lends a plain element in place to
// no body, so the call of the door does not compile.  The rule borrow-match
// refuses the same body: the type refuses it with no plugin.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

using Items = ::foundation::core::HeapArray<std::uint64_t>;

[[nodiscard]] Items make_items(std::uint64_t fill) noexcept {
    return ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 64, fill);
}

}  // namespace

int main() {
    ::foundation::core::HeapArray<Items> nested = ::foundation::core::mint_heap_array_with<Items>(
        ::foundation::effects::testing::test().alloc, 2, [](std::size_t) noexcept { return make_items(7); });
    std::size_t read = 0;
    auto other = ::foundation::core::to(nested);
    nested.with_element(
        0,
        [other, &read](Items& outer) mutable noexcept {
            auto view = outer.view();
            static_cast<void>(other->exchange(0, make_items(8)));
            read = view.size();
        },
        [] noexcept {});
    return read == 64 ? 0 : 1;
}
