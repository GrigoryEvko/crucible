#pragma once

// The frame of a unique borrow: the key of the frame doors, and RefMut.
//
// A frame door gives a body the unique borrow of what an owner holds, for
// the time of the call.  This is the inout parameter of Hylo, and the split
// of a borrow in the linear design: own -o view (x) (view -o own).  The door
// has three forms.
//
//   with_mut(std::move(owner), body)
//                       of HeapArray, MaskedArray and GrowList, the owners on
//                       the heap, and of FixedList: the owner moves into the
//                       frame of the door, body gets the ViewMut<T, Brand> of
//                       the elements (foundation/core/Region.h), and the door
//                       gives the owner back.  While the body runs, the caller
//                       holds no name for the owner: the name that the caller
//                       moved from holds no storage, or no element of a
//                       FixedList
//   owner.with_mut(body)
//                       of FixedArray, an inline owner: body gets the
//                       ViewMut<T, Brand> of the elements in place, and the
//                       door gives the result of body.  The door writes no
//                       byte of the owner
//   sum.with_mut(arms...)
//                       of Option, Result and OneOf: the arm of the active
//                       alternative gets the RefMut<T, Brand> of its payload
//                       in place, and the sum keeps the payload
//
// Family
//   Each door of the family stands beside its owner.  Each door that lends a
//   ViewMut or a RefMut builds it with the key of this header:
//     with_mut(std::move(owner), body)   HeapArray and MaskedArray
//                       (foundation/core/Array.h), FixedList (List.h) and
//                       GrowList (Grow.h)
//     with_mut(std::move(box), body)   the object of a Box
//                       (foundation/core/Ref.h): the Box moves into the frame
//                       and comes back, with no key
//     array.with_mut(body)   a FixedArray, in place (Array.h), and a
//                       FixedHeapArray, in place (FixedHeapArray.h)
//     array.with_mut(key, body)   a keyed FixedArray or a keyed
//                       FixedHeapArray, in place, with its LoanKey (the loan key),
//                       by move or by a borrow of the key
//     array.with_element(key, index, body)   one element of a keyed
//                       FixedArray or of a keyed FixedHeapArray, with its LoanKey
//     array.with_masked(key, body)   the element at key & (count - 1) of a
//                       MaskedArray (Array.h), given to the body in place, with
//                       no LoanKey: the key is the position
//     with_mut_windows<T>(block, key, windows, body)   some windows of a
//                       KeyedBytes block, with its LoanKey (foundation/AlignedBytes.h)
//     sum.with_mut(arms...)   Option and Result (Choice.h) and OneOf (OneOf.h)
//     array.with_element(index, body)   the element at an Index<N> of a
//                       FixedArray, given to the body in place (Array.h),
//                       with no key
//     list.with_element(position, on_some, on_none)   the element at a
//                       position of a GrowList (Grow.h) or of a HeapArray
//                       (Array.h), given to an arm in place, with no key
//     with_mut_windows<T>(std::move(block), windows, body)   one ViewMut<T> for
//                       each of some disjoint windows of an AlignedBytes
//                       (foundation/AlignedBytes.h), as slice::get_disjoint_mut
//                       of Rust gives one unique borrow for each of some
//                       indexes.  The block moves into the frame and comes
//                       back, with no key
//     build_fixed_array<T, N>(body)   a new FixedArray (Array.h): the array is a
//                       local of the door while body runs, so no name of it is
//                       in the scope of the caller, and it takes no key
//
// A borrow of a run that other frames of the same storage nest inside, such
// as the records of one op that runs other ops, takes a window door of a
// WindowStack (foundation/core/WindowStack.h): each door lends a window that no
// other open window holds, and the windows end in the reverse order of their
// opens.
//
// A body of the first form that gives void gives the owner back alone.  A
// body that gives a value gives a Record of the value and the owner.  The
// value holds no borrow and names no brand of the frame (FrameResult of
// foundation/core/Choice.h), so no unique borrow leaves its frame through
// the return.  The bodies and the arms of the other two forms give their
// result, under the same rule.
//
// Two forms of an owner
//   A move of an owner on the heap takes a pointer and a count, so its frame
//   holds the owner at the cost of O(1), and the caller holds no name for it.
//   A move of an inline owner copies each of its elements into the frame and
//   back, and the old name keeps a stale copy.  For a large array that is a
//   copy of the whole run for each frame.  So an inline owner lends its run
//   in place, as a sum does.  Its door carries the annotation borrow_match,
//   so the rule borrow-match refuses a call of a member of the same owner
//   that is not const in the body, such as a second door.  A function that
//   the body calls can still reach the owner, as for a sum, and the alias
//   proof then holds only for the body that the rule reads.
//
// Two forms
//   A sum is a small value that its holder keeps, often as a member that
//   other code reads.  A move into a frame would write that member two
//   times, and a read of its state from a second thread would race with the
//   move.  So the door of a sum writes no byte of the sum.  It works as the
//   borrow match does: the rule borrow-match refuses an arm that assigns,
//   moves or changes the sum, and an owning sum counts the lend (the lend
//   count in Choice.h).  A sum gives one object, and no kernel reads two of
//   them, so its frame needs no alias proof.
//
//   An owner gives a run, and a door of a kernel reads the brands of runs to
//   prove that they do not overlap (the alias proof).  That proof needs a
//   frame that holds its owner, so the door of a heap owner takes the owner
//   by move, and the door of an inline owner lends in place under the rule
//   borrow-match (the two forms of an owner).
//
// Brand of a frame
//   Each frame door takes a scope brand (CRUCIBLE_SCOPE_BRAND of
//   foundation/Brand.h) in its last template parameter, after a pack that
//   takes each explicit argument, so no caller names it.  Each unique borrow
//   that the frame gives carries that brand.  Only a frame door makes the key
//   of the constructors of ViewMut and RefMut, so each unique borrow comes
//   from a frame.  Each call site of a door gives its frame a brand of its
//   own.  The door of a heap owner is a function at namespace scope.  The
//   door of an inline owner and the door of a sum are member templates, and
//   GCC 16 gives their default lambda a type for each call site too, as two
//   nested frames of one Option type show.  No proof reads the brand of a
//   frame of a sum.  The brand keeps the RefMut out
//   of the result of the arm, and the rule brand-escape keeps it out of each
//   object of static or thread storage duration.
//
// Alias proof
//   A frame of a heap owner holds its owner by value, and the rule
//   borrow-match refuses a second door of an inline owner in the body of its
//   frame, so no two live frames hold one object.  An owner owns its storage
//   alone, so the runs of two frames do not overlap.  Thus two ViewMuts of two different brands name storage
//   that does not overlap.  A call site that runs two times, in a loop or in
//   a recursion, gives its two frames one brand.  One brand proves no
//   difference, so a reader of the brands takes the slow and safe path for
//   it.  The proof needs a brand for each frame, and not a brand for each
//   run of a site.
//
//   A plain View that the caller made before the frame can still name the
//   storage of a heap owner.  A View has no brand, so it proves no
//   difference either.
//
//   What stays open: a body can store a unique borrow in an object outside
//   the frame through a capture by reference, in a type that names no brand,
//   such as an address, a plain View or a Ref with the erased brand.  The
//   borrow then outlives its frame, as a stored ReadView does
//   (foundation/permissions/ReadView.h).  No code outside the body can name
//   the brand of the frame, so only such a type carries a borrow out.
//
// A RefMut holds one address.  It does not copy.  A move leaves the source
// with its address, and the rule use-after-move refuses a later use of the
// source.  The move constructor and the destructor are trivial, so the ABI
// passes a RefMut in a register.  The seal makes the move assignment
// user-provided, so std::bit_cast builds no RefMut.
//
// Loan key
//   An owner that lends its run in place stays reachable in the body of its
//   frame: through a capture, a function that the body calls, or a Ref of the
//   owner.  Each door call gives its frame a brand of its own, so a second
//   frame of the same owner would give a second ViewMut of a different brand
//   over the same run, and the alias proof would be false.  So a keyed owner
//   opens a frame only with a LoanKey<Brand> of its own brand:
//     auto [array, key] = mint_keyed_owner(fixed_array_owner<int, 64>);
//     key = array.with_mut(std::move(key), body);   the key moves into the
//                       door, and the door gives it back after the body
//     array.with_mut(key, body);                     the door borrows the key
//                       for the call
//   A key is a linear class with no state.  Only the mint of a keyed owner
//   (mint_keyed_owner of foundation/core/KeyedMint.h) or a root door makes
//   one, with the key of the frame doors that the door class of its header
//   gives (the doors of the owners above this header), so no body makes a
//   key.  A spelling with no key brand, such as FixedArray<T, N>, has the
//   brand Unkeyed and the doors with no key.
//
// Gate
//   A keyed door refuses a body that reaches a key of the brand of its owner
//   (ReachesNoLoanKey): a capture of the key, a capture of an object that holds
//   the key or that points to it, at each depth of the walk of
//   foundation/reflect/TypeComponents.h.  The walk instantiates each class that
//   it reaches through a pointer, and it refuses a class whose members it cannot
//   read, such as an incomplete class behind a pointer.  So a body reaches no
//   key, and a function that the body calls gets only what the body gives it.
//   No code inside the body thus opens a second frame of an owner of the brand.
//   A value behind type erasure, such as a void pointer, hides its type from the
//   walk.  The quarantine refuses such a pointer outside the base.
//
// Brand of a key
//   A mint gives the fresh brand of its call site (CRUCIBLE_FRESH_BRAND), so
//   the brand names one site and not one object.  Two objects of one brand,
//   from two runs of one site or from a copy of an owner, need no brand of
//   their own: one key opens either one, and the gate keeps a second key of the
//   brand out of each body, so the frames of the brand open one at a time.  The
//   rule brand-escape keeps a fresh brand out of a result, a member and an
//   object of static storage duration.  So a key of a fresh brand lives in the
//   automatic storage of the scope of its mint.  A move or a copy of an owner
//   keeps its brand, because the brand is in its type.
//
// Borrow of a root door
//   A key of a named brand comes only from a root door: a door that starts a
//   thread, or the door of one dispatch, mints a key for each brand that its
//   body names in a trailing parameter LoanKey<B>&.  Another thread, or the
//   caller in a frame of its own, can hold a key of the same brand.  So the
//   door refuses a body that reaches an owner of the brand through a borrow:
//   the two keys would open two frames of one owner.  It also refuses a body
//   that reaches a key of the brand in either region, because a body could
//   move its key into a field that the caller reads after the call.  An owner
//   that moves into the body by value stays admitted (BorrowsNoKeyOwner).
//   A key brand that opens one side of an owner of another brand states that
//   brand: it derives from states_owner_brand<OwnerBrand>.  The walk then
//   counts an owner of the stated brand as an owner of the key brand, so a
//   body that borrows the owner and names a key of one of its sides gets no
//   second key of that side.  A statement only makes the walk refuse more
//   bodies, so a brand that states a brand it does not open weakens no proof.
//
// Dispatch brand
//   The body of a dispatch reaches its owners through outside data, such as the
//   Vigil of the PyTorch boundary, so the borrow walk does not apply to it.  A
//   brand of such an owner states dispatch_brand{}.  Only the dispatch door of
//   fixy/concurrent/Dispatch.h mints a key of it, one window at a time on each
//   thread, and each door that starts a thread refuses it
//   (foundation/core/RootKeys.h).  A key of a dispatch brand does not move, so
//   no body moves it into a field that outlives the window.  Ref, Box, View,
//   ViewMut and RefMut refuse a key of each brand as their element, so no body
//   keeps a borrow of it either.  The quarantine refuses a raw pointer outside
//   the base.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/LoanKeyTrait.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

// Doors of the owners above this header
// An owner in a header above this one builds a ViewMut, a RefMut or a
// LoanKey of what it holds, or reads the run of an owner that it holds with
// the key of a frame, such as frame_run_ of a FixedArray.  An invariant of
// the owner proves the run and the brand: the frame of a door holds its
// owner, a mint gives the fresh brand of its call, or the owner reads only an
// array of its own.  Such an owner reaches the key through one plain door
// class.  This header declares the class, and detail::FrameDoor names it as
// a friend.  The header of the owner defines it, and the owner is its friend.
// The comment of each door states the invariant of its owners.  A home owner
// row of utils/rule-table.txt keeps the one definition of each door in that
// header, so no other file defines a second door of the same name.
class RegionFrameKeyDoor;
class ArrayFrameKeyDoor;
class ListFrameKeyDoor;
class GrowFrameKeyDoor;
class SmallListFrameKeyDoor;
class FixedHeapArrayFrameKeyDoor;
class BoxFrameKeyDoor;
class ChoiceFrameKeyDoor;
class OneOfFrameKeyDoor;
class RootKeysFrameKeyDoor;
class WindowLoanFrameKeyDoor;
class SortFrameKeyDoor;
class ReduceFrameKeyDoor;
class ProtocolFrameKeyDoor;
class AlignedBytesFrameKeyDoor;
class OwnedRegionFrameKeyDoor;
class CyclicBufferFrameKeyDoor;
class KeyedMintFrameKeyDoor;
class ThreadTasksFrameKeyDoor;
class ParallelForFrameKeyDoor;
class LaneRingFrameKeyDoor;

namespace detail {

// The key of the constructors of ViewMut and RefMut, of a LoanKey, and of the
// frame reads of an owner, such as frame_run_.  FrameDoor is its one friend.
// The door and the key hold no template, so no explicit specialization of a
// template reaches the key.
class FrameDoor;

class [[nodiscard]] FrameKey final {
    friend class FrameDoor;

    constexpr FrameKey() noexcept {}
    constexpr FrameKey(FrameKey const&) noexcept {}

public:
    FrameKey& operator=(FrameKey const&) = delete("a key of a frame door lives for one construction");
};

// The door of the key of the frame doors.  Its one member is private, and
// its friends are the door classes that this header declares (the doors of
// the owners above this header).  So no other code of the base
// builds a ViewMut or a RefMut of a brand that it names over a run or an
// object that it names, and no other code mints a LoanKey.  A helper that
// builds a unique borrow for an owner, such as give_run_back_ of
// foundation/core/Region.h, takes the key from its caller as its proof.
class FrameDoor final : ::foundation::NoObject<FrameDoor> {
    friend class ::foundation::core::RegionFrameKeyDoor;
    friend class ::foundation::core::ArrayFrameKeyDoor;
    friend class ::foundation::core::ListFrameKeyDoor;
    friend class ::foundation::core::GrowFrameKeyDoor;
    friend class ::foundation::core::SmallListFrameKeyDoor;
    friend class ::foundation::core::FixedHeapArrayFrameKeyDoor;
    friend class ::foundation::core::BoxFrameKeyDoor;
    friend class ::foundation::core::ChoiceFrameKeyDoor;
    friend class ::foundation::core::OneOfFrameKeyDoor;
    friend class ::foundation::core::RootKeysFrameKeyDoor;
    friend class ::foundation::core::WindowLoanFrameKeyDoor;
    friend class ::foundation::core::SortFrameKeyDoor;
    friend class ::foundation::core::ReduceFrameKeyDoor;
    friend class ::foundation::core::ProtocolFrameKeyDoor;
    friend class ::foundation::core::AlignedBytesFrameKeyDoor;
    friend class ::foundation::core::OwnedRegionFrameKeyDoor;
    friend class ::foundation::core::CyclicBufferFrameKeyDoor;
    friend class ::foundation::core::KeyedMintFrameKeyDoor;
    friend class ::foundation::core::ThreadTasksFrameKeyDoor;
    friend class ::foundation::core::ParallelForFrameKeyDoor;
    friend class ::foundation::core::LaneRingFrameKeyDoor;

    [[nodiscard]] CRUCIBLE_INLINE static constexpr FrameKey key() noexcept { return FrameKey{}; }
};

// The type that the call static_cast<Body&&>(body)(argument) of a door gives
// for an argument of type Argument.  Each door calls its body with the call
// operator, so the alias reads that call alone.  It instantiates none of the
// classes of std::invoke_result, which the body of each call of a door would
// instantiate again.
template <class Body, class Argument>
using body_result_t = decltype(::std::declval<Body>()(::std::declval<Argument>()));

// True when type is a scope brand or names one: the closure type of
// CRUCIBLE_SCOPE_BRAND, whose call operator holds the mark
// foundation::brand::scope_brand, or a class template specialization with
// such a type argument at each depth, such as the brand of a view of a walk
// of rows.  A value of a scope brand does not outlive the call of its mint
// (foundation/Brand.h).  The call operator of a closure is public, so the
// current access context reads it.  Each loop reads its list one time, so a
// class with many template arguments, such as an index sequence of 256
// positions, costs one read of the list.  Complexity: linear in the members
// of the type, and linear in the type arguments at each depth.  An
// incomplete class, such as a tag that a unit only declares, is no brand.
[[nodiscard]] consteval bool names_scope_brand(std::meta::info type) {
    if (!std::meta::is_class_type(type) || !std::meta::is_complete_type(type)) return false;
    std::vector<std::meta::info> const members = std::meta::members_of(type, std::meta::access_context::current());
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::meta::info const member = members[index];
        if (std::meta::is_function(member) && std::meta::is_operator_function(member)
            && std::meta::operator_of(member) == std::meta::operators::op_parentheses
            && !std::meta::annotations_of_with_type(member, ^^::foundation::brand::scope_brand).empty()) {
            return true;
        }
    }
    if (!std::meta::has_template_arguments(type)) return false;
    std::vector<std::meta::info> const arguments = std::meta::template_arguments_of(type);
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (std::meta::is_type(arguments[index]) && names_scope_brand(std::meta::dealias(arguments[index])))
            return true;
    }
    return false;
}

}  // namespace detail

// The object of a unique borrow: an object type that is not an array, not
// const and not volatile.  A unique borrow writes its object, so a const
// object has none.
template <class T>
concept UniqueTarget = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>;

// The unique borrow of one object that a frame door holds, such as the
// payload of a sum.  The brand is the brand of the frame.
template <class T, ::foundation::brand::IsBrand Brand>
    requires UniqueTarget<T>
class [[nodiscard]] RefMut final {
    static_assert(!detail::is_loan_key_type_(^^T),
                  "a RefMut of a LoanKey would keep a borrow of the key past the frame that lent it: borrow the key "
                  "with LoanKey<B>& for the call");

    T* object_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    using brand_type = Brand;

    // The borrow of object, with the key that only FrameDoor builds.  A
    // reference is never null, so no key makes a null RefMut.
    constexpr RefMut(detail::FrameKey const& /*key*/, T& object) noexcept : object_{__builtin_addressof(object)} {}

    RefMut(RefMut const&) = delete("a RefMut is the one writer of its object: a copy would be a second writer");
    RefMut&
    operator=(RefMut const&) = delete("a RefMut is the one writer of its object: a copy would be a second writer");
    constexpr RefMut(RefMut&&) noexcept = default;
    constexpr RefMut& operator=(RefMut&&) noexcept = default;
    ~RefMut() = default;

    [[nodiscard]] constexpr T& operator*() & noexcept { return *object_; }
    [[nodiscard]] constexpr T const& operator*() const& noexcept { return *object_; }
    [[nodiscard]] constexpr T* operator->() & noexcept { return object_; }
    [[nodiscard]] constexpr T const* operator->() const& noexcept { return object_; }
};

// The mark of a loan key and of a keyed owner.  The kind loan-key-static of
// the rule process-wide-state (utils/tools/quarantine/declaration.cpp, the
// loan-key statics) refuses an object of static or thread storage duration
// that reaches a class with the mark, because a body with no capture reaches
// such an object and the gate does not see it.  A class template whose
// instances are keyed only for some arguments writes loan_key_mark_when.
struct loan_key_mark final {};
struct no_loan_key_mark final {};

template <bool IsKeyed>
inline constexpr auto loan_key_mark_when = no_loan_key_mark{};

template <>
inline constexpr auto loan_key_mark_when<true> = loan_key_mark{};

// The brand of an owner with no key.  FixedArray<T, N> takes it by default,
// and its frame doors take no key.  No mint and no root door gives a
// LoanKey<Unkeyed>.  The class is a named empty class, so the rule
// brand-escape reads no fresh brand in it, and a spelling of FixedArray<T, N>
// is no site of the rule erased-brand.
struct Unkeyed final {};

// A brand whose owners open their frames only with a LoanKey of it: the
// fresh brand of a mint, or a named brand that a root door gives.  The erased
// identity DefaultBrand and Unkeyed name no owner.
template <class Brand>
concept IsKeyBrand = ::foundation::brand::IsFreshBrand<Brand> && !std::is_same_v<Brand, Unkeyed>;

// The annotation of a dispatch brand (the dispatch brand): a named brand whose
// keys only the dispatch door of fixy/concurrent/Dispatch.h mints.
struct dispatch_brand final {};

// True when the brand states dispatch_brand.  A brand that is not complete
// states nothing.
[[nodiscard]] consteval bool is_dispatch_brand_(std::meta::info brand) {
    std::meta::info const bare = std::meta::dealias(brand);
    return std::meta::is_class_type(bare) && std::meta::is_complete_type(bare)
        && ::foundation::linear::carries_annotation(bare, ^^dispatch_brand);
}

template <class Brand>
concept IsDispatchBrand = IsKeyBrand<Brand> && is_dispatch_brand_(^^Brand);

// The statement of a key brand that opens one side of the owners of
// OwnerBrand (the borrow of a root door): the key brand derives from it.  The
// statement is a base and not a member alias, because GCC 16 lists no member
// alias of a specialization whose argument is a fresh brand, and it lists each
// base.  The class is empty, so a brand that derives from it stays empty.
template <class OwnerBrand>
struct states_owner_brand {};

// The key of the frames of the keyed owners of one brand (the loan key).  The
// class is empty, so it costs no byte and no instruction.  It is linear: each
// path gives it back to its holder or ends it through a door.  It does not
// copy, so one key gives one frame at a time.  Its move and its end are
// trivial, and the seal makes its assignment user-provided, so std::bit_cast
// builds no key.  It moves to another thread, and two threads never read one
// key (not_shareable of foundation/Transfer.h).  A key holds no resource, so
// the end of its scope is its door (scope_end_door of foundation/Linear.h).
// DefaultBrand and Unkeyed name no owner, so no mint and no root door gives a
// key of them.  The signature of a keyed door of an owner of Unkeyed names
// the type, and the keyed doors of such an owner take no key, so the class
// takes each brand.
//
// A key of a dispatch brand does not move (the dispatch brand): the key cell
// of the dispatch door builds it in place, and a body borrows it.  So no body
// moves it into a field that outlives the window of its dispatch.  It is no
// linear class then, because the linear shape asks a move.  Its move
// assignment stays, and the seal makes it user-provided, so std::bit_cast
// builds no such key.  Only a second key of the brand could be its source, and
// a window holds one.
template <class Brand>
class [[nodiscard]][[= ::foundation::linear::linear_when<!is_dispatch_brand_(^^Brand)>]]
                   [[= ::foundation::linear::scope_end_door{}]][[= ::foundation::transfer::not_shareable{}]]
                   [[= loan_key_mark{}]][[= detail::loan_key_class_{}]] LoanKey final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    using brand_type = Brand;

    // The key of a mint or of a root door, with the key of the frame doors
    // that only FrameDoor builds.  Only the mints of a keyed owner and the
    // root doors build a loan key.
    constexpr explicit LoanKey(detail::FrameKey const& /*key*/) noexcept {}

    LoanKey(LoanKey const&) =
        delete("a loan key opens one frame at a time: a copy would open a second frame of an owner");
    LoanKey& operator=(LoanKey const&) =
        delete("a loan key opens one frame at a time: a copy would open a second frame of an owner");
    constexpr LoanKey(LoanKey&&) noexcept
        requires(!is_dispatch_brand_(^^Brand))
    = default;
    LoanKey(LoanKey&&)
        requires(is_dispatch_brand_(^^Brand))
    = delete("a key of a dispatch brand stays in the window of its dispatch: a move would carry it past the window");
    constexpr LoanKey& operator=(LoanKey&&) noexcept = default;
    ~LoanKey() = default;
};

namespace detail {

// True when the node is the key, or a class whose members the walk cannot
// read.  A class that the walk reached through a pointer and that is
// incomplete has no readable members, and neither has a class whose state
// reflection does not show.  The gate refuses both, so it fails closed.
[[nodiscard]] consteval bool is_key_or_unreadable_(::foundation::reflect::TypeNode node, std::meta::info key) {
    if (node.type == key) {
        return true;
    }
    bool const is_class = std::meta::is_class_type(node.type) || std::meta::is_union_type(node.type);
    return (is_class && !node.may_read_members) || ::foundation::reflect::holds_unreadable_state(node);
}

// True when the walk of the gate (the gate) reaches the key from the type
// root, or a class whose members it cannot read.  The walk is the walk of
// first_component_satisfying of foundation/reflect/TypeComponents.h with the
// instantiating read.  It is a function that is no template, so no unit can
// specialize what the gate admits.  It reads the nodes in the order that it
// lists them, and it reads each node one time.  Complexity: linear in the
// distinct types that root reaches, times the scan of the visited list, at
// compile time only.
[[nodiscard]] consteval bool reaches_loan_key_(std::meta::info root, std::meta::info key) {
    namespace refl = ::foundation::reflect;
    constexpr refl::SpecializationRead read = refl::SpecializationRead::Instantiating;
    refl::TypeNodeList listed;
    listed.push_back(refl::TypeNode{refl::bare_type(root), true});
    for (std::size_t next = 0; next < listed.size(); ++next) {
        refl::TypeNode const node = listed.begin()[next];
        bool was_read = false;
        for (std::size_t earlier = 0; earlier < next; ++earlier) {
            refl::TypeNode const& seen = listed.begin()[earlier];
            if (seen.type == node.type && (seen.may_read_members || !node.may_read_members)) {
                was_read = true;
                break;
            }
        }
        if (was_read) {
            continue;
        }
        if (is_key_or_unreadable_(node, key)) {
            return true;
        }
        for (refl::TypeNode const& component : refl::argument_components_of(node, read)) {
            listed.push_back(component);
        }
        for (refl::TypeNode const& component : refl::member_components_of(node, read)) {
            listed.push_back(component);
        }
    }
    return false;
}

// The brand of the owners that a key brand opens (the borrow of a root door):
// the argument of the base states_owner_brand of the key brand, or the key
// brand itself when it states none.  A brand that is not complete states
// nothing.  It is a function that is no template, so no unit can specialize
// what it gives.  Complexity: linear in the bases of the brand, at compile
// time only.
[[nodiscard]] consteval std::meta::info stated_owner_brand_(std::meta::info brand) {
    namespace refl = ::foundation::reflect;
    std::meta::info const bare = refl::bare_type(brand);
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare)) {
        return bare;
    }
    for (refl::TypeNode const& part : refl::member_components_of(refl::TypeNode{bare, true})) {
        bool const is_statement = part.member == std::meta::info{} && std::meta::has_template_arguments(part.type)
                               && std::meta::template_of(part.type) == ^^states_owner_brand;
        if (is_statement) {
            refl::TypeNodeList const arguments = refl::argument_components_of(part);
            if (arguments.size() == 1) {
                return arguments.begin()[0].type;
            }
        }
    }
    return bare;
}

// True for a keyed owner of the brand or a key of the brand: a complete class
// with loan_key_mark whose template arguments name the brand, or the brand
// of its owners that the brand states (stated_owner_brand_).
[[nodiscard]] consteval bool is_owner_or_key_of_(std::meta::info type, std::meta::info brand,
                                                 std::meta::info owner_brand) {
    if (!std::meta::is_class_type(type) || !std::meta::is_complete_type(type)) {
        return false;
    }
    if (!::foundation::linear::carries_annotation(type, ^^loan_key_mark) || !std::meta::has_template_arguments(type)) {
        return false;
    }
    ::foundation::reflect::TypeNode const node{type, false};
    for (::foundation::reflect::TypeNode const& argument : ::foundation::reflect::argument_components_of(node)) {
        if (!argument.is_value_argument && (argument.type == brand || argument.type == owner_brand)) {
            return true;
        }
    }
    return false;
}

// The two regions of the walk of the borrow of a root door: what a value of
// the root holds, and what it reaches through a borrow.
enum class BorrowRegion : unsigned char {
    held,
    borrowed,
};

// True when the walk of the borrow of a root door reaches an owner of the
// brand or a key of the brand through a borrow (is_owner_or_key_of_, with
// the brand of the owners that the brand states), or a class whose members it
// cannot read.  Each node has a region, and the walk keeps one list for each
// region, so a type read in one region is read again in the other:
//   * the root is held.  A base and a member by value have the region of
//     their class.  A member that is a reference, the target of a pointer,
//     each part of a function type and of a member pointer, and the first
//     argument of a class that states refers{}, refers_unique{} or
//     refers_deep{} are borrowed.  The element of an array has the region of
//     the array
//   * each other template argument names a type and reaches no object, so it
//     is held, apart from an argument that is a reference type.  The members
//     give each borrow.  The handle of a PermissionedChannel names its payload
//     in consumer_pops<Form, T, Handle> and holds none, and with the region of
//     its class that handle would refuse a payload that it takes by value
//   * a class that states by_arguments{}, such as a Box, owns the object of
//     its first argument: that argument has the region of the class, and the
//     walk reads no member
//   * a class that states synchronized{} or payload_cell{}, reached through a
//     borrow, hands its values in and out by value (foundation/Transfer.h):
//     the walk reads its arguments as held and no member.  The walk thus
//     trusts each statement of such a class for its private state
// It is a function that is no template, so no unit can specialize what it
// admits.  Complexity: linear in the distinct pairs of a type and a region,
// times the scan of the visited list, at compile time only.
[[nodiscard]] consteval bool reaches_borrowed_key_owner_(std::meta::info root, std::meta::info brand) {
    namespace refl = ::foundation::reflect;
    namespace tr = ::foundation::transfer;
    constexpr refl::SpecializationRead read = refl::SpecializationRead::Instantiating;
    constexpr BorrowRegion held = BorrowRegion::held;
    constexpr BorrowRegion borrowed = BorrowRegion::borrowed;
    std::meta::info const owner_brand = stated_owner_brand_(brand);
    refl::TypeNodeList held_nodes;
    refl::TypeNodeList borrowed_nodes;
    std::size_t next_held = 0;
    std::size_t next_borrowed = 0;
    auto nodes_of = [&](BorrowRegion region) -> refl::TypeNodeList& {
        return region == borrowed ? borrowed_nodes : held_nodes;
    };
    held_nodes.push_back(refl::TypeNode{refl::bare_type(root), true});
    while (next_held < held_nodes.size() || next_borrowed < borrowed_nodes.size()) {
        BorrowRegion const region = next_held < held_nodes.size() ? held : borrowed;
        std::size_t const at = region == borrowed ? next_borrowed++ : next_held++;
        refl::TypeNode const node = nodes_of(region).begin()[at];
        bool was_read = false;
        for (std::size_t earlier = 0; earlier < at; ++earlier) {
            refl::TypeNode const& seen = nodes_of(region).begin()[earlier];
            if (seen.type == node.type && (seen.may_read_members || !node.may_read_members)) {
                was_read = true;
                break;
            }
        }
        if (was_read) {
            continue;
        }
        std::meta::info const type = node.type;
        bool const is_class = std::meta::is_class_type(type) || std::meta::is_union_type(type);
        if ((is_class && !node.may_read_members) || refl::holds_unreadable_state(node)) {
            return true;
        }
        if (region == borrowed && is_owner_or_key_of_(type, brand, owner_brand)) {
            return true;
        }
        bool const is_box = is_class && tr::detail::states(type, ^^tr::by_arguments);
        bool const is_referring =
            is_class
            && (tr::detail::states(type, ^^tr::refers) || tr::detail::states(type, ^^tr::refers_unique)
                || tr::detail::states(type, ^^tr::refers_deep));
        bool const is_cell =
            is_class && (tr::detail::states(type, ^^tr::synchronized) || tr::detail::states(type, ^^tr::payload_cell));
        bool const is_indirect = std::meta::is_pointer_type(type) || std::meta::is_function_type(type)
                              || std::meta::is_member_pointer_type(type);
        bool is_first_argument = true;
        for (refl::TypeNode const& component : refl::argument_components_of(node, read)) {
            BorrowRegion target = held;
            if (is_indirect || component.was_reference || (is_referring && is_first_argument)) {
                target = borrowed;
            } else if (std::meta::is_array_type(type) || (is_box && is_first_argument)) {
                target = region;
            }
            is_first_argument = false;
            nodes_of(target).push_back(component);
        }
        if (is_box || (is_cell && region == borrowed)) {
            continue;
        }
        for (refl::TypeNode const& component : refl::member_components_of(node, read)) {
            nodes_of(component.was_reference ? borrowed : region).push_back(component);
        }
    }
    return false;
}

}  // namespace detail

// True when no value of Root reaches a key of the brand (the gate): no part
// of it, by value, through a reference, a pointer or a template argument, at
// each depth, is a LoanKey<Brand>.  Each keyed door asks it of its body and of
// the element of its owner.  The walk reads each class one time.  Complexity:
// linear in the distinct types that Root reaches, at compile time only.
template <class Root, class Brand>
concept ReachesNoLoanKey =
    IsKeyBrand<Brand> && !detail::reaches_loan_key_(^^std::remove_cvref_t<Root>, std::meta::dealias(^^LoanKey<Brand>));

// True when a part of a body of a root door is safe for a key of the brand
// (the borrow of a root door): it reaches no key of the brand in either region,
// and it reaches no owner of the brand through a borrow.  A root door asks it
// of its body, of the result of the body and of each parameter that is no key.
// Complexity: two walks, linear in the distinct types that Root reaches, at
// compile time only.
template <class Root, class Brand>
concept BorrowsNoKeyOwner =
    ReachesNoLoanKey<Root, Brand>
    && !detail::reaches_borrowed_key_owner_(^^std::remove_cvref_t<Root>, std::meta::dealias(^^Brand));

}  // namespace foundation::core
