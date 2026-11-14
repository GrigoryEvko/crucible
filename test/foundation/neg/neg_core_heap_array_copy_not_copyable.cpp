// mint_heap_array_copy copies each element of its View.  An element that
// cannot copy with no throw fails the gate.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

struct MoveOnly {
    int value = 0;
    MoveOnly() noexcept = default;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly(MoveOnly const&) = delete;
};

int main() {
    ::foundation::core::View<MoveOnly const> const source{};
    auto copied =
        ::foundation::core::mint_heap_array_copy<MoveOnly>(::foundation::effects::testing::test().alloc, source);
    return static_cast<int>(copied.size());
}
