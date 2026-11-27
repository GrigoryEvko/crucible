// The body of the element door with_masked of a MaskedArray keeps a Ref of
// the element in a variable that it captures by reference.  The Ref outlives
// the frame, so a move of the array, or of a child owner that the element
// holds (the child move of test_core_owner_probe.cpp), can end the storage
// that the Ref names.  The element is a class of Atomic cells, so the door
// that is not const lends it in place.  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the body.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

namespace {

struct NodeBrand final {};

struct Node {
    using brand_type = NodeBrand;
    ::foundation::core::Atomic<int> label{0};
};

}  // namespace

int main() {
    ::foundation::core::MaskedArray<Node> array =
        ::foundation::core::mint_masked_array<Node>(::foundation::effects::testing::test().alloc, 4);
    ::foundation::core::Option<::foundation::core::Ref<Node, NodeBrand>> kept{::foundation::core::none};
    array.with_masked(
        0, [&kept](Node& first) noexcept { kept = ::foundation::core::some(::foundation::core::to(first)); });
    return kept.is_some() ? 0 : 1;
}
