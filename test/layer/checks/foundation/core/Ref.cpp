// The compile-time checks of foundation/core/Ref.h.

#include <foundation/core/Ref.h>

#include <foundation/core/Text.h>
#include <foundation/core/Verbs.h>

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::ref_checks {

// A stand-in for the allocation tag of foundation/effects, which is above
// this layer.
struct TestAllocation {
    using grants_allocation = void;
};

struct NoMarker {};

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

// The gate of the mint.
static_assert(CanMintBox<int, TestAllocation>);
static_assert(CanMintBox<int, TestAllocation, int>);
static_assert(!CanMintBox<int, NoMarker, int>);
static_assert(!CanMintBox<int, TestAllocation, char const*>);
static_assert(!CanMintBox<int[2], TestAllocation>);

// ── Ref ────────────────────────────────────────────────────────────────

// One pointer, and an Option of a Ref has no flag.
static_assert(sizeof(Ref<int>) == sizeof(void*));
static_assert(sizeof(Ref<OverAligned const>) == sizeof(void*));
static_assert(sizeof(Option<Ref<int>>) == sizeof(void*));
static_assert(sizeof(Option<Ref<Abstract>>) == sizeof(void*));

// A Ref copies as a pointer copies, and the seal makes it not trivially
// copyable, so std::bit_cast builds no Ref from the bytes of an address.
static_assert(std::is_trivially_copy_constructible_v<Ref<int>>);
static_assert(std::is_trivially_move_constructible_v<Ref<int>>);
static_assert(std::is_trivially_destructible_v<Ref<int>>);
static_assert(std::is_copy_assignable_v<Ref<int>>);
static_assert(!std::is_trivially_copyable_v<Ref<int>>);

// No empty Ref, and no Ref from a pointer.
static_assert(!std::is_default_constructible_v<Ref<int>>);
static_assert(!std::is_constructible_v<Ref<int>, int*>);
static_assert(!std::is_constructible_v<Ref<int>, std::nullptr_t>);

// A const target comes from a mutable one, and never the reverse.
static_assert(std::is_convertible_v<Ref<int>, Ref<int const>>);
static_assert(!std::is_convertible_v<Ref<int const>, Ref<int>>);
static_assert(!std::is_constructible_v<Ref<int>, Ref<int const>>);
static_assert(!std::is_constructible_v<Ref<long>, Ref<int>>);

// to() takes an lvalue only.
template <class R, class T>
concept ReachesTemporary = requires { R::to(T{}); };
template <class R, class T>
concept ReachesLvalue = requires(T& object) { R::to(object); };
static_assert(!ReachesTemporary<Ref<int>, int>);
static_assert(!ReachesTemporary<Ref<int const>, int const>);
static_assert(ReachesLvalue<Ref<int>, int>);
static_assert(ReachesLvalue<Ref<int const>, int const>);

// The target gate.
static_assert(RefTarget<int>);
static_assert(RefTarget<int const>);
static_assert(RefTarget<Abstract>);
static_assert(!RefTarget<int[2]>);
static_assert(!RefTarget<int volatile>);
static_assert(!RefTarget<int&>);
static_assert(!RefTarget<void>);

// A Ref has no compare: same_object() tells if two Refs reach one object.
static_assert(!std::equality_comparable<Ref<int>>);
static_assert(!std::totally_ordered<Ref<int>>);

// A Ref borrows with the dereference only, and has no get().
template <class R>
concept HasGet = requires(R ref) { ref.get(); };
static_assert(!HasGet<Ref<int>>);

// The dereference and same_object() fold in a constant evaluation.
consteval bool reaches_in_constant_evaluation() {
    int first = 1;
    int second = 2;
    Ref<int> const ref = Ref<int>::to(first);
    *ref = 3;
    return first == 3 && same_object(ref, Ref<int const>::to(first)) && !same_object(ref, Ref<int>::to(second));
}
static_assert(reaches_in_constant_evaluation());

// ── Box ────────────────────────────────────────────────────────────────

// A borrow of a temporary Box is refused.
template <class B>
concept BorrowsFromTemporary = requires(B&& box) { static_cast<B&&>(box).view(); };
template <class B>
concept BorrowsFromLvalue = requires(B& box) { box.view(); };
static_assert(!BorrowsFromTemporary<Box<int>>);
static_assert(BorrowsFromLvalue<Box<int>>);

// view() gives a Ref of the object, and a Ref of a const object from a
// const Box.  The Box borrows through view() and the dereference only.
static_assert(std::is_same_v<decltype(std::declval<Box<int>&>().view()), Ref<int>>);
static_assert(std::is_same_v<decltype(std::declval<Box<int> const&>().view()), Ref<int const>>);
static_assert(!HasGet<Box<int>>);

// ── The closed surface ─────────────────────────────────────────────────

// The family list of the Ref family (plan section 11.5).  Each public member
// of a type of the family is a shared verb, a language protocol or a name of
// this list.  Ref and Box have no empty state, so they take the dereference.
inline constexpr TextView ref_names{"Ref to same_object downcast KindFamily KindTagged Box mint_box BumpArena make "
                                    "array copy_text Slot emplace clear FnRef CallRef Address address_of is_aligned "
                                    "Hole"};
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<int>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<int const>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<Abstract>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Box<int>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Box<OverAligned>, DereferenceProtocol);

}  // namespace detail::ref_checks

}  // namespace foundation::core
