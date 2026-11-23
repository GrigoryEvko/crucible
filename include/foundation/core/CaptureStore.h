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
//                pointer to const or a borrow of a const element.  The walk
//                takes const as deep: a write through a borrow that the object
//                holds is a write of the owner of that borrow, and each owner
//                of the base gives a const view of its parts.  So the walk
//                reads only the mutable members of such an object itself,
//                and not the mutable members of its parts
//     writable   the body writes the object, and the caller can read it after
//                the call: the target of a capture by a reference that is not
//                const, of a pointer to an object that is not const, or of a
//                borrow of an element that is not const
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
//       const, and read_only then.  The walk follows no reference, pointer or
//       borrow of a read_only object
//     * a borrow names its target: the first type argument of a View or a
//       Ref
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
// it admits.  It reads each pair of a type and a region one time, and the
// memo keeps the verdict of each captured type and each target of an
// indirection for the translation unit.  Complexity: linear in the distinct
// pairs that the body reaches, times the scan of the visited list, at compile
// time only.

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

// The mark owns_its_storage of foundation/core/Owner.h names an owner whose
// type arguments the store check reads, and no member (the walk).

namespace detail {

// The three regions of the walk.  The test of a scope brand is
// detail::names_scope_brand of foundation/core/Ref.h.
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

// True for an owner of the walk.  A class with no annotation states nothing,
// so the test reads the annotations only of a class that has some.
[[nodiscard]] consteval bool is_storage_owner_(std::meta::info type, Annotated annotated) {
    namespace tr = ::foundation::transfer;
    if (annotated == Annotated::yes
        && (tr::detail::states(type, ^^tr::by_arguments) || tr::detail::states(type, ^^tr::synchronized)
            || tr::detail::states(type, ^^tr::payload_cell) || tr::detail::states(type, ^^owns_its_storage))) {
        return true;
    }
    // Each owner of the base derives from OwnerCore in public, so the public
    // read of the bases sees it.
    std::meta::access_context const context = std::meta::access_context::current();
    std::size_t const base_count = std::meta::bases_of(type, context).size();
    for (std::size_t index = 0; index < base_count; ++index) {
        std::meta::info const base_type =
            std::meta::dealias(std::meta::type_of(std::meta::bases_of(type, context)[index]));
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
// type is complete or the walk refuses it.  The walk asks the memo for the
// target of each pointer, borrow and borrow of a frame, so the walk of one
// target type runs one time in a translation unit.  A cycle of the memo would
// need a class that holds a borrow of a frame of its own type, and such a
// cycle stops the build with no verdict.
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
        bool const is_read_only = region == read_only;
        if (std::meta::is_pointer_type(type)) {
            std::meta::info const pointee = std::meta::remove_pointer(type);
            if (std::meta::is_function_type(pointee) || is_read_only) continue;
            if (region == writable) return true;
            refl::TypeNode const target = refl::node_reached_indirectly(pointee, read);
            if (reaches_slot_(target.type, target_region_(target))) return true;
            continue;
        }
        if (std::meta::is_array_type(type)) {
            walk.push(region,
                      refl::TypeNode{refl::bare_type(std::meta::remove_all_extents(type)), node.may_read_members});
            continue;
        }
        bool const is_class = std::meta::is_class_type(type) || std::meta::is_union_type(type);
        if (!is_class) continue;
        if (is_read_only) {
            if (!node.may_read_members) continue;
            for (refl::TypeNode const& member : refl::member_components_of(node, read)) {
                if (member.is_mutable) walk.push(writable, member);
            }
            continue;
        }
        if (!node.may_read_members || refl::holds_unreadable_state(node)) return true;
        if (is_frame_borrow_(type)) {
            for (refl::TypeNode const& argument : refl::argument_components_of(node, read)) {
                if (holds_no_value_of_(argument) || is_scope_brand_(argument.type)) continue;
                if (reaches_slot_(argument.type, target_region_(argument))) return true;
            }
            continue;
        }
        Annotated const annotated = std::meta::annotations_of(type).size() != 0 ? Annotated::yes : Annotated::no;
        if (is_borrow_(type, annotated)) {
            if (region == writable) return true;
            for (refl::TypeNode const& argument : refl::argument_components_of(node, read)) {
                if (argument.is_value_argument) continue;
                if (reaches_slot_(argument.type, target_region_(argument))) return true;
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

}  // namespace foundation::core
