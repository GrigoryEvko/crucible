#pragma once

// The frame of a unique borrow: the key of the frame doors, and RefMut.
//
// A frame door gives a body the unique borrow of what an owner holds, for
// the time of the call.  This is the inout parameter of Hylo, and the split
// of a borrow in the linear design: own -o view (x) (view -o own).  The door
// has two forms.
//
//   with_mut(std::move(owner), body)
//                       of FixedArray, HeapArray, MaskedArray, FixedList and
//                       GrowList: the owner moves into the frame of the door,
//                       body gets the ViewMut<T, Brand> of the elements
//                       (foundation/core/Region.h), and the door gives the
//                       owner back.  While the body runs, the caller holds no
//                       name for the owner: the name that the caller moved
//                       from holds no storage, or a copy of the elements of an
//                       inline owner
//   sum.with_mut(arms...)
//                       of Option, Result and OneOf: the arm of the active
//                       alternative gets the RefMut<T, Brand> of its payload
//                       in place, and the sum keeps the payload
//
// A body of the first form that gives void gives the owner back alone.  A
// body that gives a value gives a Record of the value and the owner.  The
// value holds no borrow and names no brand of the frame (FrameResult of
// foundation/core/Choice.h), so no unique borrow leaves its frame through
// the return.  The arms of the second form give their result, under the
// same rule.
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
//   frame that holds its owner, so the door of an owner takes the owner by
//   move.
//
// THE BRAND OF A FRAME
//   Each frame door takes a scope brand (CRUCIBLE_SCOPE_BRAND of
//   foundation/Brand.h) in its last template parameter, after a pack that
//   takes each explicit argument, so no caller names it.  Each unique borrow
//   that the frame gives carries that brand.  Only a frame door makes the key
//   of the constructors of ViewMut and RefMut, so each unique borrow comes
//   from a frame.  The door of an owner is a function at namespace scope, so
//   each call site of it gives its frame a brand of its own.  The door of a
//   sum is a member of a class template, and a lambda in a default template
//   argument of such a member gives one type for each class.  So each frame
//   of one sum type has one brand, and that brand proves no difference.  It
//   keeps the RefMut out of the result of the arm, and the rule brand-escape
//   keeps it out of each object of static or thread storage duration.
//
// THE ALIAS PROOF
//   A frame of an owner holds its owner by value, so no two live frames hold
//   one object.  An owner owns its storage alone, so the runs of two frames
//   do not overlap.  Thus two ViewMuts of two different brands name storage
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

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>

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

}  // namespace foundation::core
