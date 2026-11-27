// The body of the element door of a MaskedArray of arrays replaces the
// element that it reads, through a Ref of the table that the body captures.
// The replace gives the storage of the inner array back to the allocator
// while the body holds a View of it.  A MaskedArray lends a plain element in
// place to no body, so the call of the door does not compile.  The rule
// borrow-match refuses the same body: the type refuses it with no plugin.

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
    ::foundation::core::MaskedArray<Items> table =
        ::foundation::core::mint_masked_array<Items>(::foundation::effects::testing::test().alloc, 4);
    table.set_masked(0, make_items(7));
    std::size_t read = 0;
    auto other = ::foundation::core::to(table);
    table.with_masked(0, [other, &read](Items& items) mutable noexcept {
        auto view = items.view();
        static_cast<void>(other->exchange_masked(0, make_items(8)));
        read = view.size();
    });
    return read == 64 ? 0 : 1;
}
