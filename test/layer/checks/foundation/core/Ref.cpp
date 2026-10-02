// The compile-time checks of foundation/core/Ref.h.

#include <foundation/core/Ref.h>

#include <type_traits>

namespace foundation::core {

namespace detail::ref_checks {

// A stand-in for the allocation tag of foundation/effects, which is above
// this layer.
struct TestAllocation {
    using grants_allocation = void;
};

struct NoMarker {};

struct MarkerWithState {
    using grants_allocation = void;
    int state = 0;
};

struct Abstract {
    virtual ~Abstract() = default;
    virtual void act() = 0;
};

struct OverAligned {
    alignas(128) int value = 0;
};

// One pointer, and nothing more.
static_assert(sizeof(Box<int>) == sizeof(void*));
static_assert(sizeof(Box<OverAligned>) == sizeof(void*));

// Move-only, and the move does not throw.
static_assert(!std::is_copy_constructible_v<Box<int>>);
static_assert(!std::is_copy_assignable_v<Box<int>>);
static_assert(std::is_nothrow_move_constructible_v<Box<int>>);
static_assert(std::is_nothrow_move_assignable_v<Box<int>>);
static_assert(!std::is_trivially_copyable_v<Box<int>>);
static_assert(!std::is_default_constructible_v<Box<int>>);

// No pointer converts to a Box.
static_assert(!std::is_constructible_v<Box<int>, int*>);
static_assert(!std::is_convertible_v<int*, Box<int>>);

// The payload gate.
static_assert(BoxPayload<int>);
static_assert(BoxPayload<OverAligned>);
static_assert(!BoxPayload<int[4]>);
static_assert(!BoxPayload<int const>);
static_assert(!BoxPayload<int&>);
static_assert(!BoxPayload<void>);
static_assert(!BoxPayload<Abstract>);

// The allocation tag.
static_assert(AllocationCapability<TestAllocation>);
static_assert(!AllocationCapability<NoMarker>);
static_assert(!AllocationCapability<MarkerWithState>);
static_assert(!AllocationCapability<int>);

// The gate of the mint.
static_assert(CanMintBox<int, TestAllocation>);
static_assert(CanMintBox<int, TestAllocation, int>);
static_assert(!CanMintBox<int, NoMarker, int>);
static_assert(!CanMintBox<int, TestAllocation, char const*>);
static_assert(!CanMintBox<int[2], TestAllocation>);

// A borrow of a temporary Box is refused.
template <class B>
concept BorrowsFromTemporary = requires(B&& box) { static_cast<B&&>(box).get(); };
template <class B>
concept BorrowsFromLvalue = requires(B& box) { box.get(); };
static_assert(!BorrowsFromTemporary<Box<int>>);
static_assert(BorrowsFromLvalue<Box<int>>);

}  // namespace detail::ref_checks

}  // namespace foundation::core
