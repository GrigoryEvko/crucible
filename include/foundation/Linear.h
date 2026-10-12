#pragma once

// The linear marker of the base, and the vocabulary of its doors.
//
// A resource is a linear value: each path uses it exactly one time.  A
// class tells that its values are linear with one P3394 annotation:
//
//     class [[= ::foundation::linear::linear{}]] Token { ... };
//
// Reflection reads the annotation through std::meta::annotations_of.  The
// quarantine plugin reads the same annotation in the tree of GCC.  There,
// each annotation is an attribute, and its value has the type of the
// annotation.  So the trait of this header and the rules of the plugin
// read one fact.  No macro writes the annotation, so a build with no
// plugin reads it too.
//
// A class template whose instances are linear only for some arguments
// writes linear_when<CONDITION> in place of linear{}.  An instance whose
// condition is false carries the inert value not_linear{}.
//
// A class is linear when it carries the annotation, or when it can move
// and it holds a linear base, a linear member or an array of linear
// elements by value.  A union counts as a class.  A class that cannot move
// is not linear by its members, because no move can consume it: its
// destructor is the door at the end of its scope.  A pointer, a reference
// and a template argument hold no value, so they make no class linear.
//
// A marked class has the shape of a linear class, and the trait refuses a
// marked class without it.  The class is not trivially copyable, so
// std::bit_cast refuses it and a move of an Option copies no bytes of it.
// It is not copy constructible, and its move constructor and destructor
// do not throw.  A user-provided move assignment, or the member
// foundation::lifetime::byte_seal, gives the first fact.  The move
// constructor and the destructor can stay trivial, so the ABI passes the
// class in registers.
//
// Each exit from linearity is a named door.  The annotations of this
// header name the doors: drop_door and detach_door on a function, and
// scope_end_door on a class whose destructor releases its resource.  The
// annotation authority marks a class whose value conveys an authority,
// which the gate of foundation/effects/Computation.h reads.
//
// What the trait does not see, stated rather than implied: a lambda
// capture, because GCC 16 reflects no data member of a closure type, and a
// value behind type erasure.  The rules of the plugin read the closure in
// the tree.

#include <foundation/ByteSeal.h>
#include <foundation/reflect/TypeComponents.h>

#include <meta>
#include <type_traits>

namespace foundation::linear {

// The annotation of a linear class.
struct linear final {};

// The value of linear_when<false>: an annotation that marks nothing.
struct not_linear final {};

// The annotation of a class whose value conveys an authority, such as a
// permission token.
struct authority final {};

// The annotation of a function that ends a linear value and reports
// nothing.
struct drop_door final {};

// The annotation of a function that ends a linear value with a reason.
struct detach_door final {};

// The annotation of a class whose destructor releases its resource.  The
// end of the scope of a value of the class is then its named door.
struct scope_end_door final {};

// The annotation value of a class template whose instances are linear
// only for some arguments.
template <bool IsLinear>
inline constexpr auto linear_when = not_linear{};

template <>
inline constexpr auto linear_when<true> = linear{};

// True when the entity carries an annotation of the type.  The read goes
// through a constant array, so it calls no member of the container that
// the query gives.  The entity must be complete.  Complexity: linear in
// the number of annotations of the entity.
[[nodiscard]] consteval bool carries_annotation(std::meta::info entity, std::meta::info annotation_type) {
    const std::meta::info found =
        std::meta::reflect_constant_array(std::meta::annotations_of_with_type(entity, annotation_type));
    const std::meta::info found_type = std::meta::type_of(found);
    return std::meta::is_array_type(found_type) && std::meta::extent(found_type) != 0;
}

// True when the class or union carries the annotation linear.
[[nodiscard]] consteval bool is_marked_linear(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    if (!std::meta::is_class_type(bare) && !std::meta::is_union_type(bare)) return false;
    return carries_annotation(bare, ^^linear);
}

// True when the marked class has the shape of a linear class.
[[nodiscard]] consteval bool has_linear_shape(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    return !std::meta::is_trivially_copyable_type(bare) && !std::meta::is_copy_constructible_type(bare)
        && std::meta::is_nothrow_move_constructible_type(bare) && std::meta::is_nothrow_destructible_type(bare);
}

// True when the type is linear, by its annotation or by its parts.  A
// marked class without the shape of a linear class stops the build here,
// so no reader of the trait gets an answer for it.  Complexity: linear in
// the number of bases and members that the type holds by value.
[[nodiscard]] consteval bool is_linear_type(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    if (std::meta::is_array_type(bare)) return is_linear_type(std::meta::remove_all_extents(bare));
    if (!std::meta::is_class_type(bare) && !std::meta::is_union_type(bare)) return false;
    if (carries_annotation(bare, ^^linear)) {
        if (!has_linear_shape(bare)) {
            throw std::meta::exception(u8"linear: a class carries the annotation linear and does not have the shape "
                                       u8"of a linear class.  Make it move-only with a move constructor and a "
                                       u8"destructor that do not throw, and give it a user-provided move "
                                       u8"assignment so that it is not trivially copyable.",
                                       bare);
        }
        return true;
    }
    if (!std::meta::is_move_constructible_type(bare)) return false;
    for (const ::foundation::reflect::TypeNode& part :
         ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{bare, true})) {
        if (part.is_held_by_value && is_linear_type(part.type)) return true;
    }
    return false;
}

// True when T is linear.  The trait reads the type with its cv-qualifiers
// and its reference removed, and the type must be complete.  Only a
// translation unit that reads the trait evaluates it.  A gate reads the
// function is_linear_type, because no unit can specialize a function that
// is not a template.
template <class T>
inline constexpr bool is_linear_v = is_linear_type(^^T);

// A linear value that a door can take: an object of a linear type, not
// const, passed as an rvalue.
template <class T>
concept LinearValue = !std::is_reference_v<T> && !std::is_const_v<T> && is_linear_type(^^T);

// The drop door that reports nothing.  It moves the value into its own
// frame, so the destructor of the value runs before the door returns.  A
// search for this annotation lists each deliberate discard.
template <LinearValue T>
[[= drop_door{}]] constexpr void drop(T&& value) noexcept {
    T dropped(static_cast<T&&>(value));
    static_cast<void>(dropped);
}

// A named value stays with the caller, so a drop of it would leave a
// second name for the value.  The deleted twin gives the reason.
template <class T>
    requires(is_linear_type(^^T))
constexpr void drop(T&) = delete("the drop door ends a linear value that the caller gives up.  Pass it with std::move");

}  // namespace foundation::linear
