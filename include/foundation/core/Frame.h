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
// THE FAMILY
//   Each door of the family stands beside its owner.  Each door that lends a
//   ViewMut or a RefMut builds it with the key of this header:
//     with_mut(std::move(owner), body)   HeapArray and MaskedArray
//                       (foundation/core/Array.h), FixedList (List.h) and
//                       GrowList (Grow.h)
//     array.with_mut(body)   a FixedArray, in place (Array.h), and a
//                       FixedHeapArray, in place (FixedHeapArray.h)
//     array.with_mut(key, body)   a keyed FixedArray, in place, with its
//                       LoanKey (THE LOAN KEY), by move or by a borrow of the key
//     array.with_element(key, index, body)   one element of a keyed
//                       FixedArray, with its LoanKey
//     with_mut_windows<T>(block, key, windows, body)   some windows of a
//                       KeyedBytes block, with its LoanKey (foundation/AlignedBytes.h)
//     sum.with_mut(arms...)   Option and Result (Choice.h) and OneOf (OneOf.h)
//     array.with_element(index, body)   the element at an Index<N> of a
//                       FixedArray, given to the body in place (Array.h),
//                       with no key
//     list.with_element(position, on_some, on_none)   the element at a
//                       position of a GrowList (Grow.h) or of a HeapArray
//                       (Array.h), given to an arm in place, with no key
//     with_mut_windows<T>(block, windows, body)   one ViewMut<T> for each of
//                       some disjoint windows of a named AlignedBytes
//                       (foundation/AlignedBytes.h), as slice::get_disjoint_mut
//                       of Rust gives one unique borrow for each of some indexes.
//                       The block stays with the caller, so its frame proves
//                       only that its windows do not overlap
//
// A borrow that spans more than one call, such as the records of one op that
// runs other ops, takes a loan of a WindowStack (foundation/core/WindowStack.h):
// each loan holds a window that no other open loan holds, and the loans end in
// the reverse order of their opens.
//
// A body of the first form that gives void gives the owner back alone.  A
// body that gives a value gives a Record of the value and the owner.  The
// value holds no borrow and names no brand of the frame (FrameResult of
// foundation/core/Choice.h), so no unique borrow leaves its frame through
// the return.  The bodies and the arms of the other two forms give their
// result, under the same rule.
//
// THE TWO FORMS OF AN OWNER
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
// THE TWO FORMS
//   A sum is a small value that its holder keeps, often as a member that
//   other code reads.  A move into a frame would write that member two
//   times, and a read of its state from a second thread would race with the
//   move.  So the door of a sum writes no byte of the sum.  It works as the
//   borrow match does: the rule borrow-match refuses an arm that assigns,
//   moves or changes the sum, and an owning sum counts the lend (THE LEND
//   COUNT of Choice.h).  A sum gives one object, and no kernel reads two of
//   them, so its frame needs no alias proof.
//
//   An owner gives a run, and a door of a kernel reads the brands of runs to
//   prove that they do not overlap (THE ALIAS PROOF).  That proof needs a
//   frame that holds its owner, so the door of a heap owner takes the owner
//   by move, and the door of an inline owner lends in place under the rule
//   borrow-match (THE TWO FORMS OF AN OWNER).
//
// THE BRAND OF A FRAME
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
// THE ALIAS PROOF
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
// THE LOAN KEY
//   An owner that lends its run in place stays reachable in the body of its
//   frame: through a capture, a function that the body calls, or a Ref of the
//   owner.  Each door call gives its frame a brand of its own, so a second
//   frame of the same owner would give a second ViewMut of a different brand
//   over the same run, and the alias proof would be false.  So a keyed owner
//   opens a frame only with a LoanKey<Brand> of its own brand:
//     auto [array, key] = mint_fixed_array<int, 64>();
//     key = array.with_mut(std::move(key), body);   the key moves into the
//                       door, and the door gives it back after the body
//     array.with_mut(key, body);                     the door borrows the key
//                       for the call
//   A key is a linear class with no state.  Only a mint or a root door makes
//   one, with the key of detail::FrameDoor, so no body makes a key.  A spelling with no
//   key brand, such as FixedArray<T, N>, has the brand Unkeyed and the doors
//   with no key.
//
// THE GATE
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
// THE BRAND OF A KEY
//   A mint gives the fresh brand of its call site (CRUCIBLE_FRESH_BRAND), so
//   the brand names one site and not one object.  Two objects of one brand,
//   from two runs of one site or from a copy of an owner, need no brand of
//   their own: one key opens either one, and the gate keeps a second key of the
//   brand out of each body, so the frames of the brand open one at a time.  The
//   rule brand-escape keeps a fresh brand out of a result, a member and an
//   object of static storage duration.  So a key of a fresh brand lives in the
//   automatic storage of the scope of its mint.  A move or a copy of an owner
//   keeps its brand, because the brand is in its type.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

namespace detail {

// The key of the constructors of ViewMut and RefMut.  FrameDoor is its one
// friend, and only the frame doors call the door.  The door and the key hold
// no template, so no explicit specialization of a template reaches the key.
// The door is in namespace detail, and a use of that namespace outside the
// base is a finding.
struct FrameDoor;

class [[nodiscard]] FrameKey final {
    friend struct FrameDoor;

    constexpr FrameKey() noexcept {}
    constexpr FrameKey(FrameKey const&) noexcept {}

public:
    FrameKey& operator=(FrameKey const&) = delete("a key of a frame door lives for one construction");
};

struct FrameDoor {
    [[nodiscard]] static constexpr FrameKey key() noexcept { return FrameKey{}; }
};

// The type that the call static_cast<Body&&>(body)(argument) of a door gives
// for an argument of type Argument.  Each door calls its body with the call
// operator, so the alias reads that call alone.  It instantiates none of the
// classes of std::invoke_result, which the body of each call of a door would
// instantiate again.
template <class Body, class Argument>
using body_result_t = decltype(::std::declval<Body>()(::std::declval<Argument>()));

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
// the rule process-wide-state (utils/tools/quarantine/declaration.cpp, THE
// LOAN-KEY STATICS) refuses an object of static or thread storage duration
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

// The key of the frames of the keyed owners of one brand (THE LOAN KEY).  The
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
template <class Brand>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::scope_end_door{}]]
                   [[= ::foundation::transfer::not_shareable{}]][[= loan_key_mark{}]] LoanKey final {
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
    constexpr LoanKey(LoanKey&&) noexcept = default;
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

// True when the walk of the gate (THE GATE) reaches the key from the type
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

}  // namespace detail

// True when no value of Root reaches a key of the brand (THE GATE): no part
// of it, by value, through a reference, a pointer or a template argument, at
// each depth, is a LoanKey<Brand>.  Each keyed door asks it of its body and of
// the element of its owner.  The walk reads each class one time.  Complexity:
// linear in the distinct types that Root reaches, at compile time only.
template <class Root, class Brand>
concept ReachesNoLoanKey =
    IsKeyBrand<Brand> && !detail::reaches_loan_key_(^^std::remove_cvref_t<Root>, std::meta::dealias(^^LoanKey<Brand>));

}  // namespace foundation::core
