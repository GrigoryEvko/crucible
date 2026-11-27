// The body of the element door of a HeapArray of plain words assigns the
// array through a Ref of it that the body captures.  The assignment gives the
// storage of the element back to the allocator, so the read of the element
// after it would read freed memory.  A HeapArray lends a plain element in
// place to no body, so the call of the door does not compile.  The rule
// borrow-match refuses the same body: the type refuses it with no plugin.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

[[nodiscard]] ::foundation::core::HeapArray<std::uint64_t> make_items(std::uint64_t fill) noexcept {
    return ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 64, fill);
}

}  // namespace

int main() {
    ::foundation::core::HeapArray<std::uint64_t> items = make_items(7);
    std::uint64_t read = 0;
    auto other = ::foundation::core::to(items);
    items.with_element(
        0,
        [other, &read](std::uint64_t& element) mutable noexcept {
            *other = make_items(8);
            read = element;
        },
        [] noexcept {});
    return read == 7 ? 0 : 1;
}
