// The walk of the keys of a heap bit set lends the keys to a body for the call.
// A body that captures an Option of a View by reference could keep a borrow of
// the walk there, and the caller could read it after the walk.  with_keys
// refuses a body that can store a borrow outside the walk
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Array.h>
#include <foundation/core/BitSet.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {
struct SlotTag final {};
}  // namespace

int main() {
    using Count = ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>;
    auto const set =
        ::foundation::core::mint_heap_bit_set<SlotTag>(::foundation::effects::testing::test().alloc, Count{8});
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    int const count = set.with_keys([&kept](auto const& /*keys*/) noexcept { return kept.is_some() ? 1 : 0; });
    return count;
}
