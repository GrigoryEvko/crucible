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
//       The walk reads its type arguments in its own region and no member
//     * each other class gives its members.  A class template can hold a
//       value of a type argument in raw bytes, as a FixedList or an Option
//       does, so a class with an array of bytes or a union in a member gives
//       its type arguments too, in its own region.  A type argument that is
//       an incomplete class, such as a tag that a unit only declares, holds no
//       value
//   A function pointer, a pointer to member, a scalar and an enumeration are
//   no borrow slot.
//
// A capture by value of a plain value or of a borrow of a const element, a
// capture by const reference, and a capture by reference of an int, of an
// owner of plain values, of an arena or of a ViewMut of an outer frame pass.
// The cost is a refusal of a capture by reference of a large object that
// holds a borrow, such as a capture of this in a class with a member Ref.
// Such a body captures the parts that it writes.
//
// The walk is a function that is no template, so no unit can specialize what
// it admits.  It reads each pair of a type and a region one time.  It follows
// each pointer, borrow and borrow of a frame in its own visited lists, so a
// class that reaches itself ends the walk, and the memo keeps the verdict of
// each captured type for the translation unit.  Complexity: linear in the
// distinct pairs that the body reaches, times the scan of the visited list,
// at compile time only.

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Owner.h>
#include <foundation/reflect/Members.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

// The mark owns_its_storage of foundation/core/Owner.h names an owner whose
// type arguments the store check reads, and no member (the walk).

namespace detail {

// The three regions of the walk.  The test of a scope brand is
// detail::names_scope_brand of foundation/core/Frame.h.
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

// Whether a class carries an annotation.
enum class Annotated : unsigned char {
    no,
    yes,
};

// The region of the target of a reference, a pointer or a borrow: writable
// unless it is const.
[[nodiscard]] consteval StoreRegion target_region_(::foundation::reflect::TypeNode const& target) {
    return target.was_const ? StoreRegion::read_only : StoreRegion::writable;
}

// True for a scope brand itself: the closure type of CRUCIBLE_SCOPE_BRAND.
// A closure type has no name, so the test reads the members of no named
// class.
[[nodiscard]] consteval bool is_scope_brand_(std::meta::info type) {
    if (!std::meta::is_class_type(type) || std::meta::has_identifier(type)) return false;
    return names_scope_brand(type);
}

// True for a class with a type argument that is a scope brand: a borrow of a
// frame (the walk), such as ViewMut<T, Brand>, RefMut<T, Brand> or
// Ref<T, Brand> of the brand of a frame.
[[nodiscard]] consteval bool is_frame_borrow_(std::meta::info type) {
    if (!std::meta::has_template_arguments(type)) return false;
    std::vector<std::meta::info> const arguments = std::meta::template_arguments_of(type);
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (std::meta::is_type(arguments[index]) && is_scope_brand_(std::meta::dealias(arguments[index]))) return true;
    }
    return false;
}

// True for a template argument that names no object that a value can hold:
// a value argument, or an incomplete class, such as a tag that a unit only
// declares.  A class holds no value of an incomplete class, and a pointer
// member that names one is a member that the walk reads.
[[nodiscard]] consteval bool holds_no_value_of_(::foundation::reflect::TypeNode argument) {
    bool const is_class = std::meta::is_class_type(argument.type) || std::meta::is_union_type(argument.type);
    return argument.is_value_argument || (is_class && !argument.may_read_members);
}

// True for raw storage that can hold a value of a type argument of its
// class: an array of bytes, or a union.  A class with no such member holds a
// value of a type argument only in a member of a type that names it, which
// the walk reads.
[[nodiscard]] consteval bool is_byte_storage_(std::meta::info type) {
    if (std::meta::is_union_type(type)) return true;
    if (!std::meta::is_array_type(type)) return false;
    std::meta::info const element = std::meta::dealias(std::meta::remove_cv(std::meta::remove_all_extents(type)));
    return element == ^^unsigned char || element == ^^char || element == ^^signed char || element == ^^std::byte;
}

// True for an owner of the walk: a class of the base (is_class_of_the_base_ of
// foundation/core/Owner.h) that states an owner mark or derives from
// OwnerCore.  No rule keeps the marks in the base, so the walk reads the
// members of a marked class outside the base.  A class with no annotation
// states nothing, so the test reads the annotations only of a class that has
// some.
[[nodiscard]] consteval bool is_storage_owner_(std::meta::info type, Annotated annotated) {
    namespace tr = ::foundation::transfer;
    if (!is_class_of_the_base_(type)) return false;
    if (annotated == Annotated::yes
        && (tr::detail::states(type, ^^tr::by_arguments) || tr::detail::states(type, ^^tr::synchronized)
            || tr::detail::states(type, ^^tr::payload_cell) || tr::detail::states(type, ^^owns_its_storage))) {
        return true;
    }
    // Each owner of the base derives from OwnerCore in public, so the public
    // read of the bases sees it.
    std::vector<std::meta::info> const bases = std::meta::bases_of(type, std::meta::access_context::current());
    for (std::size_t index = 0; index < bases.size(); ++index) {
        std::meta::info const base_type = std::meta::dealias(std::meta::type_of(bases[index]));
        if (std::meta::has_template_arguments(base_type) && std::meta::template_of(base_type) == ^^OwnerCore) {
            return true;
        }
    }
    return false;
}

// True for a class that states refers{}, refers_unique{} or refers_deep{}.
[[nodiscard]] consteval bool is_borrow_(std::meta::info type, Annotated annotated) {
    namespace tr = ::foundation::transfer;
    return annotated == Annotated::yes
        && (tr::detail::states(type, ^^tr::refers) || tr::detail::states(type, ^^tr::refers_unique)
            || tr::detail::states(type, ^^tr::refers_deep));
}

// True for a borrow whose const object gives only const access to its
// target: a class of the base (is_class_of_the_base_ of
// foundation/core/Owner.h) that states refers_deep{}.  The walk trusts the
// mark only in the base, and the check file of each such class pins that
// each const door gives a borrow of a const target.
[[nodiscard]] consteval bool gives_const_access_(std::meta::info type, Annotated annotated) {
    namespace tr = ::foundation::transfer;
    return annotated == Annotated::yes && is_class_of_the_base_(type) && tr::detail::states(type, ^^tr::refers_deep);
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

// The visited lists of the walk, one for each region, and the next node of
// each list.
class StoreWalk {
public:
    // Adds the node to the list of the region, unless the list holds its type
    // or the type can hold no borrow slot: an arithmetic type or an
    // enumeration.  Complexity: linear in the list.
    consteval void push(StoreRegion region, ::foundation::reflect::TypeNode node) {
        if (std::meta::is_arithmetic_type(node.type) || std::meta::is_enum_type(node.type)) return;
        ::foundation::reflect::TypeNodeList& list = list_(region);
        for (::foundation::reflect::TypeNode const& seen : list) {
            if (seen.type == node.type && (seen.may_read_members || !node.may_read_members)) return;
        }
        list.push_back(node);
    }

    // True when a node waits in some list.
    [[nodiscard]] consteval bool has_next() const {
        return next_[0] < lists_[0].size() || next_[1] < lists_[1].size() || next_[2] < lists_[2].size();
    }

    // The region of the next node: the writable region first, so a refusal
    // comes early.
    [[nodiscard]] consteval StoreRegion next_region() const {
        if (next_[2] < lists_[2].size()) return StoreRegion::writable;
        if (next_[0] < lists_[0].size()) return StoreRegion::owned;
        return StoreRegion::read_only;
    }

    // The next node of the region.
    [[nodiscard]] consteval ::foundation::reflect::TypeNode take(StoreRegion region) {
        std::size_t const slot = static_cast<std::size_t>(region);
        return lists_[slot].begin()[next_[slot]++];
    }

private:
    [[nodiscard]] consteval ::foundation::reflect::TypeNodeList& list_(StoreRegion region) {
        return lists_[static_cast<std::size_t>(region)];
    }

    ::foundation::reflect::TypeNodeList lists_[3]{};
    std::size_t next_[3]{};
};

[[nodiscard]] consteval bool reaches_slot_(std::meta::info type, StoreRegion region);

// True when the walk from the start type in the start region reaches a
// borrow slot in the writable region.  The start type is bare, and a class
// type is complete or the walk refuses it.  The walk pushes the target of each
// pointer, borrow and borrow of a frame into its own lists, so a type that
// reaches itself, such as a node with a pointer to its parent, ends with the
// visited lists and asks no memo in the middle of its own walk.
[[nodiscard]] consteval bool reaches_slot_from_(std::meta::info start_type, StoreRegion start) {
    namespace refl = ::foundation::reflect;
    constexpr refl::SpecializationRead read = refl::SpecializationRead::Instantiating;
    constexpr StoreRegion owned = StoreRegion::owned;
    constexpr StoreRegion read_only = StoreRegion::read_only;
    constexpr StoreRegion writable = StoreRegion::writable;
    StoreWalk walk;
    walk.push(start, refl::node_reached_indirectly(start_type, read));
    while (walk.has_next()) {
        StoreRegion const region = walk.next_region();
        refl::TypeNode const node = walk.take(region);
        std::meta::info const type = node.type;
        if (std::meta::is_pointer_type(type)) {
            std::meta::info const pointee = std::meta::remove_pointer(type);
            if (std::meta::is_function_type(pointee)) continue;
            if (region == writable) return true;
            refl::TypeNode const target = refl::node_reached_indirectly(pointee, read);
            walk.push(target_region_(target), target);
            continue;
        }
        if (std::meta::is_array_type(type)) {
            walk.push(region,
                      refl::TypeNode{refl::bare_type(std::meta::remove_all_extents(type)), node.may_read_members});
            continue;
        }
        bool const is_class = std::meta::is_class_type(type) || std::meta::is_union_type(type);
        if (!is_class) continue;
        if (!node.may_read_members) return refuse_incomplete_capture_(type);
        if (refl::holds_unreadable_state(node)) return true;
        if (is_frame_borrow_(type)) {
            for (refl::TypeNode const& argument : refl::argument_components_of(node, read)) {
                if (holds_no_value_of_(argument) || is_scope_brand_(argument.type)) continue;
                walk.push(target_region_(argument), refl::node_reached_indirectly(argument.type, read));
            }
            continue;
        }
        Annotated const annotated = std::meta::annotations_of(type).size() != 0 ? Annotated::yes : Annotated::no;
        if (is_borrow_(type, annotated)) {
            if (region == writable) return true;
            bool const gives_const = region == read_only && gives_const_access_(type, annotated);
            for (refl::TypeNode const& argument : refl::argument_components_of(node, read)) {
                if (argument.is_value_argument) continue;
                walk.push(gives_const ? read_only : target_region_(argument),
                          refl::node_reached_indirectly(argument.type, read));
                break;
            }
            continue;
        }
        bool const is_owner = is_storage_owner_(type, annotated);
        bool holds_bytes = is_owner;
        if (!is_owner) {
            for (refl::TypeNode const& member : refl::member_components_of(node, read)) {
                holds_bytes = holds_bytes || is_byte_storage_(member.type);
                if (member.was_reference) {
                    walk.push(target_region_(member), member);
                } else if (member.is_mutable && region != owned) {
                    walk.push(writable, member);
                } else {
                    walk.push(region, member);
                }
            }
        }
        if (!holds_bytes) continue;
        for (refl::TypeNode const& argument : refl::argument_components_of(node, read)) {
            if (!holds_no_value_of_(argument)) walk.push(region, argument);
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
template <std::meta::info Type, StoreRegion Region>
inline constexpr bool reaches_slot_from_v = reaches_slot_from_(Type, Region);

// The memo of the verdict of the type in the region (the memo).
[[nodiscard]] consteval bool reaches_slot_(std::meta::info type, StoreRegion region) {
    std::meta::info const arguments[] = {std::meta::reflect_constant(type), std::meta::reflect_constant(region)};
    return std::meta::extract<bool>(std::meta::substitute(^^reaches_slot_from_v, arguments));
}

// True when the walk from the class of a body reaches a borrow slot in the
// writable region.  body is the class of the body with no reference.  The
// class of a closure is unique to its lambda, so the walk reads its captures
// here and asks the memo for each of them.  A named class is the start of the
// memo itself.
[[nodiscard]] consteval bool stores_borrow_outside_(std::meta::info body, BodyForm form) {
    namespace refl = ::foundation::reflect;
    constexpr refl::SpecializationRead read = refl::SpecializationRead::Instantiating;
    std::meta::info const root = refl::bare_type(body);
    if (!std::meta::is_class_type(root)) return false;
    bool const is_named_class = form == BodyForm::named && std::meta::has_identifier(root)
                             && !std::meta::is_const_type(std::meta::dealias(body));
    if (is_named_class) return reaches_slot_(root, StoreRegion::writable);
    refl::TypeNode const root_node{root, true};
    if (refl::holds_unreadable_state(root_node)) return true;
    for (refl::TypeNode const& member : refl::member_components_of(root_node, read)) {
        StoreRegion region = StoreRegion::owned;
        if (member.was_reference) {
            region = target_region_(member);
        }
        if (reaches_slot_(member.type, region)) return true;
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
