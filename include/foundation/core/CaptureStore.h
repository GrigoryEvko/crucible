#pragma once

// The store check of a frame body: no capture of the body takes a borrow out
// of the frame.
//
// A frame door lends a borrow to a body for the time of one call
// (foundation/core/Frame.h), and the result of the body holds no borrow
// (FrameResult of foundation/core/Choice.h).  A body can still write a
// borrow of the frame into an object that it captures, and the caller then
// reads that object after the frame ends.  StoresNoBorrowOutside<Body>
// refuses such a body by the types of its captures, whatever the door that
// made the borrow and whatever its brand.
//
// Walk
//   The walk reads each capture of the body (the members of its class, which
//   the sixth patch of GCC shows) and each type that a capture reaches.  Each
//   node of the walk is in one of three regions:
//     owned      the body owns the object: a capture by value, and each part
//                of it.  A write of it stays in the body
//     read_only  the body reads the object through a const reference, a
//                pointer to const or a borrow of a const element.  C++ const
//                does not go through a reference member, the target of a
//                pointer or a borrow, or a mutable member, so the walk reads
//                each part of such an object as C++ does: each one stays
//                writable when its type is not const
//     writable   the body writes the object, and the caller can read it after
//                the call: the target of a capture by a reference that is not
//                const, of a pointer to an object that is not const, or of a
//                borrow of an element that is not const, also inside a
//                read_only object
//   A borrow slot in the writable region refuses the body.  A borrow slot is
//   a pointer to an object, a class that states refers{}, refers_unique{} or
//   refers_deep{} (foundation/Transfer.h), such as a View or a Ref, and a
//   class whose state the walk cannot read.  So a capture of a View<T const>
//   by reference, or of an Option of an erased Ref, refuses the body, and so
//   does a capture of an object whose method can store a View.
//
//   The regions move as follows:
//     * the root is the class of the body, owned, or writable when the door
//       takes a named body that is no closure.  The caller can read a member
//       of a named class, and no member of a closure
//     * a member by value and a base keep the region of their class.  A
//       mutable member is writable.  A member by reference, the target of a
//       pointer and the target of a borrow are writable unless they are
//       const, and read_only then.  This holds in each region, also behind a
//       const pointer and in a read_only object.  A pointer is a borrow slot
//       only in the writable region
//     * a borrow names its target: the first type argument of a View or a
//       Ref.  A class of the base that states refers_deep{}, such as a Link,
//       gives only const access to its target when it is const, so its target
//       is read_only in a read_only object
//     * a class whose state the walk cannot read is a borrow slot in each
//       region, because a const object of it can still hold a reference
//       member.  A class that is incomplete in the unit stops the build in
//       each region, with an error that names the class and the cause.  A
//       unit that captures such an object includes the header that completes
//       its class
//     * a borrow of a frame, a class whose type arguments name a scope brand
//       (foundation/Brand.h), is no borrow slot.  Only that frame makes a
//       value of its brand, so a body of an inner frame writes none into it.
//       Its type arguments name its targets.  A ViewMut and a RefMut of an
//       outer frame are such borrows, and a Ref of a scope brand too
//     * an owner holds its storage through pointers that no caller sets: a
//       class on OwnerCore (foundation/core/Owner.h), a class that states
//       by_arguments{}, synchronized{} or payload_cell{}, or owns_its_storage{}.
//       The walk reads its type arguments in its own region and no member.  A
//       core whose state releases nothing, such as the run of a region, and a
//       share of a pool that states shares{} own no storage, so the walk reads
//       their members
//     * a handle of the base that states keeps_own_borrows{}
//       (foundation/core/TypeParts.h), such as an Epoch, is no borrow slot,
//       and the walk reads no part of it.  Each door of it that stores an
//       object runs ArenaKeeps, so the object holds borrows only of the brand
//       of the handle
//     * a pointer member of a class of the base that states refers_static{}
//       (foundation/Transfer.h), such as the pointer of a StaticText, names
//       static storage, so it is no borrow slot.  The walk reads each other
//       part of the class
//     * each other class gives its members.  A class template can hold a
//       value of a type argument in raw bytes, as a FixedList or an Option
//       does, so a class with an array of bytes or a union in a member gives
//       its type arguments too, in its own region.  A type argument that is
//       an incomplete class, such as a tag that a unit only declares, holds no
//       value
//     * an array gives its element in the region of the array
//   A function pointer, a pointer to member, a scalar and an enumeration are
//   no borrow slot.
//
// A capture by value of a plain value or of a borrow of a const element, a
// capture by const reference, and a capture by reference of an int, of an
// owner of plain values, of an arena, of an epoch or of a ViewMut of an outer
// frame pass.
// The cost is a refusal of a capture by reference of a large object that
// holds a borrow, such as a capture of this in a class with a member Ref.
// Such a body captures the parts that it writes.
//
// The walk is a function that is no template, so no unit can specialize what
// it admits.  It reads each pair of a type and a region one time.  It follows
// each pointer, borrow and borrow of a frame in its own visited list, so a
// class that reaches itself ends the walk, and the memo keeps the verdict of
// each captured type for the translation unit.  It reads a class through
// type_parts_v of foundation/core/TypeParts.h, which reads each class one
// time for the translation unit, with an operation budget for each class.
// Complexity: linear in the distinct pairs that the body reaches, times the
// scan of the visited list, at compile time only.

#include <foundation/core/Frame.h>
#include <foundation/core/TypeParts.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

// The mark owns_its_storage of foundation/core/Owner.h names an owner whose
// type arguments the store check reads, and no member (the walk).

namespace detail {

// The three regions of the walk.
enum class StoreRegion : unsigned char {
    owned,
    read_only,
    writable,
};

// How a door takes its body: a named body that the caller can read after the
// call, or a body that the caller cannot read.
enum class BodyForm : unsigned char {
    unnamed,
    named,
};

// The region of the target of a reference, a pointer or a borrow: writable
// unless it is const.
[[nodiscard]] consteval StoreRegion target_region_(TypePart const& target) {
    return target.has(PartFact::was_const) ? StoreRegion::read_only : StoreRegion::writable;
}

// The refusal of a capture that reaches the class Type, whose definition the
// unit does not see.  The walk instantiates it only for such a class, and the
// two assertions then stop the build: the first gives the cause and the cure,
// and the second names the class.  So the walk fails closed on such a class in
// each region, and the error names the class.  The condition depends on Type
// and is false for each class.
template <std::meta::info Type>
consteval bool refuse_incomplete_class_() {
    static_assert(Type == std::meta::info{},
                  "the store check of a frame door: a capture of the body reaches a class, and the definition of that "
                  "class is not visible in this unit.  The check reads each part of a capture, so it refuses a class "
                  "whose parts it cannot read.  Include the header that defines the class.  The next message names "
                  "the class");
    static_assert(Type == std::meta::info{}, std::meta::display_string_of(Type));
    return true;
}

template <std::meta::info Type>
inline constexpr bool refuses_incomplete_class_v = refuse_incomplete_class_<Type>();

// Stops the build with the name of the class type, whose definition the unit
// does not see (refuse_incomplete_class_).
[[nodiscard]] consteval bool refuse_incomplete_capture_(std::meta::info type) {
    std::meta::info const arguments[] = {std::meta::reflect_constant(type)};
    return std::meta::extract<bool>(std::meta::substitute(^^refuses_incomplete_class_v, arguments));
}

// One node of the walk: a bare type in a region.  kept is the reflection of
// the kept parts of a class, when the walk has it (TypePart), and may_read
// tells that a class is complete.
struct StoreNode {
    std::meta::info type{};
    std::meta::info kept{};
    StoreRegion region = StoreRegion::owned;
    bool may_read = false;
};

// The node of a type that the walk reaches other than by value: a class is
// complete or the walk cannot read it.
[[nodiscard]] consteval StoreNode indirect_node_(std::meta::info type, StoreRegion region) {
    std::meta::info const bare = ::foundation::reflect::bare_type(type);
    return StoreNode{bare, std::meta::info{}, region, is_class_or_union_(bare) && std::meta::is_complete_type(bare)};
}

// True for the node of a template argument that names no object that a value
// can hold: an incomplete class, such as a tag that a unit only declares.  A
// class holds no value of an incomplete class, and a pointer member that names
// one is a member that the walk reads.
[[nodiscard]] consteval bool holds_no_value_of_(StoreNode const& argument) {
    return is_class_or_union_(argument.type) && !argument.may_read;
}

// The visited list of the walk, and the next node of each region.  The list
// is one array, so the walk allocates one time for a small graph.
class StoreWalk {
public:
    consteval StoreWalk() noexcept = default;
    StoreWalk(StoreWalk const&) = delete("a walk owns its list, so a copy would free the list two times");
    StoreWalk& operator=(StoreWalk const&) = delete("a walk owns its list, so a copy would free the list two times");
    // A destructor cannot be consteval, and a walk exists only in a constant
    // evaluation, so the branch is the only path.
    constexpr ~StoreWalk() {
        if consteval {
            delete[] nodes_;
        }
    }

    // Adds the node to the list, unless the list holds its type in its region
    // or the type can hold no borrow slot (is_leaf_).  Complexity: linear in
    // the list.
    consteval void push(StoreNode const& node) {
        std::meta::info const type = node.type;
        if (is_leaf_(type)) return;
        StoreNode const* const nodes = nodes_;
        for (std::size_t index = 0; index < size_; ++index) {
            if (nodes[index].type == type && nodes[index].region == node.region
                && (nodes[index].may_read || !node.may_read)) {
                return;
            }
        }
        if (size_ == capacity_) grow_();
        nodes_[size_] = node;
        ++size_;
    }

    // True when a node waits in some region.
    [[nodiscard]] consteval bool has_next() {
        return waits_(StoreRegion::writable) || waits_(StoreRegion::owned) || waits_(StoreRegion::read_only);
    }

    // The next node: the first one of the writable region, so a refusal comes
    // early, then of the owned region, and then of the read_only region.
    [[nodiscard]] consteval StoreNode take() {
        StoreRegion region = StoreRegion::read_only;
        if (waits_(StoreRegion::writable)) {
            region = StoreRegion::writable;
        } else if (waits_(StoreRegion::owned)) {
            region = StoreRegion::owned;
        }
        return nodes_[next_[static_cast<std::size_t>(region)]++];
    }

private:
    // True when a node of the region waits, and next_ of the region is then
    // its place.
    [[nodiscard]] consteval bool waits_(StoreRegion region) {
        std::size_t& next = next_[static_cast<std::size_t>(region)];
        while (next < size_ && nodes_[next].region != region) {
            ++next;
        }
        return next < size_;
    }

    // Doubles the capacity, and moves each node into the new array.
    // Complexity: linear in the number of nodes.
    consteval void grow_() {
        std::size_t const capacity = capacity_ == 0 ? 32 : 2 * capacity_;
        StoreNode* const nodes = new StoreNode[capacity]{};
        for (std::size_t index = 0; index < size_; ++index)
            nodes[index] = nodes_[index];
        delete[] nodes_;
        nodes_ = nodes;
        capacity_ = capacity;
    }

    StoreNode* nodes_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::size_t next_[3]{};
};

// Pushes the bases and the members of the class in the region: a member by
// reference in the region of its target, a mutable member of an object that
// the body does not own in the writable region, and each other part in the
// region of its class.  A pointer member of a class of the base that states
// refers_static{} names static storage, so it is no part of the walk.
consteval void push_members_(StoreWalk& walk, TypeParts const& parts, StoreRegion region) {
    PartRun const run = parts.run();
    for (std::size_t index = parts.member_start; index < parts.size; ++index) {
        TypePart const part = run[index];
        if (part.has(PartFact::is_plain)) continue;
        if (parts.refers_static && std::meta::is_pointer_type(part.type)) continue;
        if (part.has(PartFact::was_reference)) {
            walk.push(indirect_node_(part.type, target_region_(part)));
        } else {
            bool const is_written = part.has(PartFact::is_mutable) && region != StoreRegion::owned;
            walk.push(StoreNode{part.type, part.kept, is_written ? StoreRegion::writable : region, true});
        }
    }
}

// True when the walk from the start type in the start region reaches a
// borrow slot in the writable region.  The start type is bare, and a class
// type is complete or the walk refuses it.  The walk pushes the target of each
// pointer, borrow and borrow of a frame into its own list, so a type that
// reaches itself, such as a node with a pointer to its parent, ends with the
// visited list and asks no memo in the middle of its own walk.
[[nodiscard]] consteval bool reaches_slot_from_(std::meta::info start_type, StoreRegion start) {
    constexpr StoreRegion read_only = StoreRegion::read_only;
    constexpr StoreRegion writable = StoreRegion::writable;
    StoreWalk walk;
    walk.push(indirect_node_(start_type, start));
    while (walk.has_next()) {
        StoreNode const node = walk.take();
        std::meta::info const type = node.type;
        StoreRegion const region = node.region;
        if (std::meta::is_pointer_type(type)) {
            std::meta::info const pointee = std::meta::remove_pointer(type);
            if (std::meta::is_function_type(pointee)) continue;
            if (region == writable) return true;
            walk.push(indirect_node_(pointee, std::meta::is_const_type(pointee) ? read_only : writable));
            continue;
        }
        if (std::meta::is_array_type(type)) {
            walk.push(indirect_node_(std::meta::remove_all_extents(type), region));
            continue;
        }
        if (!is_class_or_union_(type)) continue;
        if (!node.may_read) return refuse_incomplete_capture_(type);
        if (node.kept == std::meta::info{} && is_plain_class_(type)) continue;
        TypeParts const& parts =
            std::meta::extract<TypeParts const&>(node.kept == std::meta::info{} ? kept_parts_of(type) : node.kept);
        if (parts.is_unreadable) return true;
        PartRun const run = parts.run();
        if (parts.is_frame_borrow) {
            for (std::size_t index = 0; index < parts.argument_count; ++index) {
                TypePart const argument = run[index];
                StoreNode const target = indirect_node_(argument.type, target_region_(argument));
                if (!argument.has(PartFact::is_scope_brand) && !holds_no_value_of_(target)) walk.push(target);
            }
            continue;
        }
        if (parts.is_borrow) {
            if (region == writable) return true;
            bool const gives_const = region == read_only && parts.gives_const_access;
            bool const is_read = gives_const || parts.is_first_argument_const;
            if (parts.first_argument != std::meta::info{}) {
                walk.push(indirect_node_(parts.first_argument, is_read ? read_only : writable));
            }
            continue;
        }
        if (parts.keeps_own_borrows) continue;
        if (!parts.is_storage_owner) push_members_(walk, parts, region);
        if (!parts.is_storage_owner && !parts.holds_bytes) continue;
        for (std::size_t index = 0; index < parts.argument_count; ++index) {
            StoreNode const argument = indirect_node_(run[index].type, region);
            if (!holds_no_value_of_(argument)) walk.push(argument);
        }
    }
    return false;
}

// Memo
//   The verdict of one type in one region does not depend on the body that
//   reaches it, so each translation unit computes it one time, in an
//   instance of this variable template.  The bodies of one class capture
//   this, a context or an owner of the class again and again, and each such
//   capture then costs one lookup.
template <class Type, StoreRegion Region>
inline constexpr bool reaches_slot_from_v = reaches_slot_from_(^^Type, Region);

// The class that a pointer to a type converts to when the walk from the type
// in the region reaches no borrow slot in the writable region, and else not.
template <StoreRegion Region>
struct NoSlotTest final {
    template <class Type>
        requires(!reaches_slot_from_v<Type, Region>)
    consteval NoSlotTest(Type*) noexcept {}
};

// The verdict of the type in the region, from the memo.  The read is the read
// of is_plain_class_ of foundation/core/TypeParts.h, with the constraint of
// NoSlotTest, so a type with no verdict refuses the body.
[[nodiscard]] consteval bool reaches_slot_(std::meta::info type, StoreRegion region) {
    std::meta::info const tests[] = {^^NoSlotTest<StoreRegion::owned>, ^^NoSlotTest<StoreRegion::read_only>,
                                     ^^NoSlotTest<StoreRegion::writable>};
    return !std::meta::is_convertible_type(std::meta::add_pointer(type), tests[static_cast<std::size_t>(region)]);
}

// True when the walk from one capture or one base of the class of a body
// reaches a borrow slot in the writable region: a capture by reference in the
// region of its target, and each other one owned.  Most captures name a scalar
// or a plain class, which holds a borrow slot in no region.
[[nodiscard]] consteval bool stores_borrow_in_(std::meta::info declared) {
    std::meta::info const type = ::foundation::reflect::bare_type(declared);
    if (is_leaf_(type)) return false;
    if (is_class_or_union_(type) && std::meta::is_complete_type(type) && is_plain_class_(type)) return false;
    StoreRegion region = StoreRegion::owned;
    if (std::meta::is_reference_type(declared)) {
        bool const is_const = std::meta::is_const_type(std::meta::remove_reference(declared));
        region = is_const ? StoreRegion::read_only : StoreRegion::writable;
    }
    return reaches_slot_(type, region);
}

// True when the walk from the class of a body reaches a borrow slot in the
// writable region.  body is the class of the body with no reference.  The
// class of a closure is unique to its lambda, so the walk reads its captures
// here and asks the memo for each of them.  A named class is the start of the
// memo itself.
[[nodiscard]] consteval bool stores_borrow_outside_(std::meta::info body, BodyForm form) {
    std::meta::info const root = ::foundation::reflect::bare_type(body);
    if (!std::meta::is_class_type(root)) return false;
    bool const is_named_class = form == BodyForm::named && std::meta::has_identifier(root)
                             && !std::meta::is_const_type(std::meta::dealias(body));
    if (is_named_class) return reaches_slot_(root, StoreRegion::writable);
    std::meta::access_context const unchecked = std::meta::access_context::unchecked();
    std::vector<std::meta::info> const bases = std::meta::bases_of(root, unchecked);
    std::vector<std::meta::info> const members = std::meta::nonstatic_data_members_of(root, unchecked);
    std::size_t const base_count = bases.size();
    std::size_t const member_count = members.size();
    bool const shows_no_state = base_count == 0 && member_count == 0;
    if (shows_no_state && !std::meta::has_template_arguments(root) && !std::meta::is_empty_type(root)) return true;
    for (std::size_t index = 0; index < base_count; ++index) {
        if (stores_borrow_in_(std::meta::type_of(bases[index]))) return true;
    }
    for (std::size_t index = 0; index < member_count; ++index) {
        if (stores_borrow_in_(std::meta::type_of(members[index]))) return true;
    }
    return false;
}

}  // namespace detail

// A body of a frame door that stores no borrow outside the frame (the walk):
// no capture reaches a borrow slot that the caller can read after the call.
// Body is the type that the door deduces for its forwarding parameter, so an
// lvalue reference names a body that the caller holds.
template <class Body>
concept StoresNoBorrowOutside =
    !detail::stores_borrow_outside_(^^std::remove_reference_t<Body>, std::is_lvalue_reference_v<Body>
                                                                         ? detail::BodyForm::named
                                                                         : detail::BodyForm::unnamed);

// The arms of a frame door of a sum, each of which stores no borrow outside
// the frame.
template <class... Arms>
concept ArmsStoreNoBorrowOutside = (StoresNoBorrowOutside<Arms> && ...);

// The reason of the refused twin of a frame door whose body stores a borrow
// outside the frame.
#define CRUCIBLE_STORING_BODY_REFUSAL                                                                            \
    "a body of a frame door stores no borrow outside the frame: capture a View or a Ref by value or by a const " \
    "reference, and capture only the part of an object that the body writes (StoresNoBorrowOutside of "          \
    "foundation/core/CaptureStore.h)"

}  // namespace foundation::core
