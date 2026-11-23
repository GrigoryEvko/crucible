// The compile-time checks of foundation/core/Frame.h.

#include <foundation/core/Frame.h>

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/List.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::frame_checks {

// The brand that a check names.  A program gets each brand from a frame door.
struct CheckBrand final {};

// A body that writes nothing and gives nothing, and a body that gives an
// integer.
struct WritesNothing final {
    template <class Run>
    void operator()(Run /*run*/) const noexcept {}
};
struct CountsElements final {
    template <class Run>
    std::size_t operator()(Run run) const noexcept {
        return run.size();
    }
};

// ── RefMut ────────────────────────────────────────────────────────────

// A RefMut has the size of one pointer.  It does not copy, its move and its
// end are trivial, so the ABI passes it in a register, and its assignment is
// not, so std::bit_cast builds none.  No constructor takes a pointer or an
// object with no key.
static_assert(sizeof(RefMut<int, CheckBrand>) == sizeof(int*));
static_assert(!std::is_copy_constructible_v<RefMut<int, CheckBrand>>);
static_assert(std::is_trivially_move_constructible_v<RefMut<int, CheckBrand>>);
static_assert(std::is_trivially_destructible_v<RefMut<int, CheckBrand>>);
static_assert(!std::is_trivially_copyable_v<RefMut<int, CheckBrand>>);
static_assert(!std::is_copy_assignable_v<RefMut<int, CheckBrand>>);
static_assert(!std::is_default_constructible_v<RefMut<int, CheckBrand>>);
static_assert(!std::is_constructible_v<RefMut<int, CheckBrand>, int*>);
static_assert(!std::is_constructible_v<RefMut<int, CheckBrand>, int&>);
static_assert(std::is_same_v<RefMut<int, CheckBrand>::brand_type, CheckBrand>);

// A RefMut writes its object, so a const, an array, a volatile and a
// reference have none.
static_assert(UniqueTarget<int> && UniqueTarget<FixedArray<int, 2>>);
static_assert(!UniqueTarget<int const> && !UniqueTarget<int[2]> && !UniqueTarget<int volatile>);
static_assert(!UniqueTarget<int&> && !UniqueTarget<void>);

// A const RefMut reads its object, and a RefMut that is not const writes it.
static_assert(std::is_same_v<decltype(*std::declval<RefMut<int, CheckBrand>&>()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<RefMut<int, CheckBrand> const&>()), int const&>);

// The key of the frame doors has no public constructor.
static_assert(!std::is_default_constructible_v<FrameKey>);
static_assert(!std::is_copy_assignable_v<FrameKey>);

// ── The result of a frame ─────────────────────────────────────────────

// The result of a body leaves the frame: void and a plain value pass, and a
// reference, an address, a borrow of the frame and a value that names the
// brand of the frame are refused.
template <class Brand>
struct Proof final {};
static_assert(FrameResult<void, CheckBrand> && FrameResult<int, CheckBrand>);
static_assert(FrameResult<Record<int, bool>, CheckBrand> && FrameResult<int const, CheckBrand>);
static_assert(!FrameResult<int&, CheckBrand> && !FrameResult<int*, CheckBrand>);
static_assert(!FrameResult<RefMut<int, CheckBrand>, CheckBrand> && !FrameResult<ViewMut<int, CheckBrand>, CheckBrand>);
static_assert(!FrameResult<Option<Ref<int, CheckBrand>>, CheckBrand> && !FrameResult<View<int>, CheckBrand>);
static_assert(!FrameResult<Proof<CheckBrand>, CheckBrand> && !FrameResult<CheckBrand, CheckBrand>);

// ── The frame door of an owner ────────────────────────────────────────

// The free door gives the owner back for a body that gives void, and a
// Record of the result and the owner for a body that gives a value.  The
// member door of a FixedArray lends its run in place and gives the result of
// the body.
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4>&>().with_mut(WritesNothing{})), void>);
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4>&>().with_mut(CountsElements{})), std::size_t>);
static_assert(std::is_same_v<decltype(with_mut(std::declval<HeapArray<int>>(), CountsElements{})),
                             Record<std::size_t, HeapArray<int>>>);
static_assert(std::is_same_v<decltype(with_mut(std::declval<MaskedArray<int>>(), WritesNothing{})), MaskedArray<int>>);
static_assert(std::is_same_v<decltype(with_mut(std::declval<FixedList<int, 4>>(), CountsElements{})),
                             Record<std::size_t, FixedList<int, 4>>>);
static_assert(std::is_same_v<decltype(with_mut(std::declval<GrowList<int>>(), WritesNothing{})), GrowList<int>>);

// The free door takes an owner only by move, and only an owner of the
// family: a named owner, a const owner, a View and a text have no frame
// door.  A FixedArray lends in place through its member door, so the free
// door takes it in no form.  The probes are variable templates and not
// concepts, because they gate nothing.
template <class Owner>
constexpr bool frames = requires(Owner&& owner) { with_mut(static_cast<Owner&&>(owner), WritesNothing{}); };
static_assert(frames<HeapArray<int>> && frames<GrowList<int>>);
static_assert(!frames<HeapArray<int>&> && !frames<HeapArray<int> const>);
static_assert(!frames<FixedArray<int, 2>> && !frames<FixedArray<int, 2>&>);
static_assert(!frames<View<int>> && !frames<GrowText> && !frames<HeapText>);

// A body whose result does not leave the frame binds to the deleted twin.
struct GivesRun final {
    template <class Run>
    Run operator()(Run run) const noexcept {
        return run;
    }
};
template <class Owner, class Body>
constexpr bool frames_with = requires(Owner&& owner, Body body) { with_mut(static_cast<Owner&&>(owner), body); };
static_assert(!frames_with<HeapArray<int>, GivesRun> && frames_with<HeapArray<int>, CountsElements>);

// ── The loan key ──────────────────────────────────────────────────────

// A key is empty and linear.  It does not copy, its move and its end are
// trivial, its assignment is not, so std::bit_cast builds none, and only its
// token builds it.
static_assert(std::is_empty_v<LoanKey<CheckBrand>> && ::foundation::linear::is_linear_v<LoanKey<CheckBrand>>);
static_assert(!std::is_copy_constructible_v<LoanKey<CheckBrand>> && !std::is_copy_assignable_v<LoanKey<CheckBrand>>);
static_assert(std::is_trivially_move_constructible_v<LoanKey<CheckBrand>>);
static_assert(std::is_trivially_destructible_v<LoanKey<CheckBrand>>);
static_assert(!std::is_trivially_copyable_v<LoanKey<CheckBrand>>);
static_assert(!std::is_default_constructible_v<LoanKey<CheckBrand>>);
static_assert(std::is_same_v<LoanKey<CheckBrand>::brand_type, CheckBrand>);

// The gate: a key, a reference to it, a class that holds a reference to it
// and a sum that holds it reach a key of the brand.  A key of a second brand,
// a plain value and a keyed array do not.  DefaultBrand and Unkeyed name no
// owner, so they have no gate.  A Ref of a key does not compile at all.
struct OtherBrand final {};
struct KeyReference {
    LoanKey<CheckBrand>& key;
};
static_assert(!ReachesNoLoanKey<LoanKey<CheckBrand>, CheckBrand>);
static_assert(!ReachesNoLoanKey<LoanKey<CheckBrand>&, CheckBrand>);
static_assert(!ReachesNoLoanKey<KeyReference, CheckBrand>);
static_assert(!ReachesNoLoanKey<Option<LoanKey<CheckBrand>>, CheckBrand>);
static_assert(ReachesNoLoanKey<LoanKey<OtherBrand>, CheckBrand> && ReachesNoLoanKey<int, CheckBrand>);
static_assert(ReachesNoLoanKey<FixedArray<int, 4, CheckBrand>, CheckBrand>);
static_assert(!ReachesNoLoanKey<int, ::foundation::brand::DefaultBrand> && !ReachesNoLoanKey<int, Unkeyed>);

// The borrow of a root door: a body that borrows a keyed owner of the brand
// gets no key of the brand, and a body that holds the owner by value gets
// one.  A key brand that states the brand of its owners (states_owner_brand)
// counts a borrow of an owner of that brand too.  A key brand of another
// brand that states nothing counts only its own owners.
struct StatedSideBrand final : states_owner_brand<CheckBrand> {};
struct BorrowsKeyedOwner {
    FixedArray<int, 4, CheckBrand>& owner;
};
struct HoldsKeyedOwner {
    FixedArray<int, 4, CheckBrand> owner;
};
static_assert(!BorrowsNoKeyOwner<BorrowsKeyedOwner, CheckBrand> && BorrowsNoKeyOwner<HoldsKeyedOwner, CheckBrand>);
static_assert(!BorrowsNoKeyOwner<BorrowsKeyedOwner, StatedSideBrand>);
static_assert(BorrowsNoKeyOwner<HoldsKeyedOwner, StatedSideBrand> && BorrowsNoKeyOwner<BorrowsKeyedOwner, OtherBrand>);

// A spelling with no key brand is the array of Unkeyed, with no key.  A
// brand of a mint and a named brand are key brands.
static_assert(std::is_same_v<FixedArray<int, 4>, FixedArray<int, 4, Unkeyed>>);
static_assert(IsKeyBrand<CheckBrand> && !IsKeyBrand<Unkeyed> && !IsKeyBrand<::foundation::brand::DefaultBrand>);

// The keyed doors of a FixedArray: the move form gives the key back alone or
// after the result, and the borrow form gives the result.  A keyed array has
// no door with no key, and an array of Unkeyed has no keyed door.
using KeyedArray = FixedArray<int, 4, CheckBrand>;
static_assert(
    std::is_same_v<decltype(std::declval<KeyedArray&>().with_mut(std::declval<LoanKey<CheckBrand>>(), WritesNothing{})),
                   LoanKey<CheckBrand>>);
static_assert(std::is_same_v<decltype(std::declval<KeyedArray&>().with_mut(std::declval<LoanKey<CheckBrand>>(),
                                                                           CountsElements{})),
                             Record<std::size_t, LoanKey<CheckBrand>>>);
static_assert(std::is_same_v<decltype(std::declval<KeyedArray&>().with_mut(std::declval<LoanKey<CheckBrand>&>(),
                                                                           CountsElements{})),
                             std::size_t>);
template <class Array>
constexpr bool opens_with_no_key = requires(Array& array) { array.with_mut(WritesNothing{}); };
static_assert(!opens_with_no_key<KeyedArray> && opens_with_no_key<FixedArray<int, 4>>);
template <class Array>
constexpr bool opens_with_key =
    requires(Array& array, LoanKey<CheckBrand>& key) { array.with_mut(key, WritesNothing{}); };
static_assert(opens_with_key<KeyedArray> && !opens_with_key<FixedArray<int, 4>>);

// ── The closed surface ────────────────────────────────────────────────

// A RefMut gives the dereference and its brand, and no other member.
inline constexpr TextView frame_names{"brand_type"};
CRUCIBLE_CLOSED_SURFACE(frame_names, RefMut<int, CheckBrand>, DereferenceProtocol);

}  // namespace detail::frame_checks

}  // namespace foundation::core
