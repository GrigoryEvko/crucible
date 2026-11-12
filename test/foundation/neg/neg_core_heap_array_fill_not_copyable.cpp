// mint_heap_array copies its fill into each element.  An element that cannot
// copy with no throw fails the gate.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

struct MoveOnly {
    int value = 0;
    MoveOnly() noexcept = default;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly(MoveOnly const&) = delete;
};

int main() {
    MoveOnly const fill{};
    auto items = ::foundation::core::mint_heap_array(::foundation::effects::Alloc{}, 4, fill);
    return static_cast<int>(items.size());
}
