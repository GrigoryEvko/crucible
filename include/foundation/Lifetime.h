#pragma once

// Start the lifetime of an object over bytes, only for a type whose every
// subobject is an implicit-lifetime type.
//
// std::start_lifetime_as<T> requires that T is an implicit-lifetime type.
// That test reads T only, and three routes pass it with a type that
// holds an object whose lifetime never starts:
//
//   an array of a type that is not implicit-lifetime   every array type
//                                                       is implicit-lifetime
//   std::start_lifetime_as_array<T>                    libstdc++ checks
//                                                       nothing about T
//   an aggregate that holds such a type                every aggregate is
//                                                       implicit-lifetime
//
// A proof type (a mint key, a context, a capability, a permission) is not
// an implicit-lifetime type.  So each of the three routes gives a pointer
// to a proof object whose lifetime never started, and a read through it
// is undefined behavior.  The concept ImplicitLifetimeThroughout refuses the
// three routes at compile time, and start_as_array of foundation/core/Array.h
// takes only a type that the concept accepts.  The test walks the type
// through each array extent, each base class and each non-static data
// member, the members of a union included, and it refuses a reference
// member, which no lifetime start binds.
//
// The walk also refuses a class whose state it cannot read, as a subobject
// at every depth (foundation/reflect/TypeComponents.h names the shape).  A
// lambda with captures has a trivial copy constructor, so it is
// implicit-lifetime, and its captures are its members (the sixth GCC
// patch), so the walk reads them: a closure that captures an epoch is
// refused for the epoch.  On a compiler without the patch the closure shows
// no subobject, and the walk refuses it as a class it cannot read.
//
// A class can also refuse a lifetime start while it stays implicit-lifetime.
// Such a class carries the annotation no_start_over_bytes of
// foundation/ByteSeal.h, and the walk refuses it wherever it sits: alone, in
// an array, as a base or as a member.  A class that only carries the
// annotation or holds a byte_seal includes foundation/ByteSeal.h.  The walk
// refuses a class with the mark closed_tag of foundation/ByteSeal.h in the
// same way: its bytes hold the tag of a sum, and bytes can hold a tag that
// names no alternative.  So no lifetime start over bytes makes an Option or
// a Result of the plain form, or a type that holds one.
//
// start_as_array gives its objects as a View of the base, so it stands in
// foundation/core/Array.h, which includes this header and the View.  It does
// not call std::start_lifetime_as_array, so no header of the base includes
// <memory> for it.  It gives the same guarantee with a builtin and the
// barrier of libstdc++.  The comment on start_as_array tells how.
//
// The rule owners of the quarantine plugin refuses each use of the two
// library functions in a file that a build compiles, other than this header.
// New code uses start_as_array.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/reflect/TypeComponents.h>

#include <concepts>
#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::lifetime {

namespace detail {

// A class that nests deeper than this is refused, not walked.
inline constexpr int max_subobject_depth = 64;

// True when the type, and each subobject of it at each depth, is an
// implicit-lifetime type.  Complexity: linear in the number of subobject
// declarations that the walk reaches.
[[nodiscard]] consteval bool is_implicit_lifetime_throughout_at(std::meta::info spelled, int depth) {
    if (depth > max_subobject_depth) return false;
    const std::meta::info type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_reference_type(type)) return false;
    if (std::meta::is_array_type(type)) {
        return is_implicit_lifetime_throughout_at(std::meta::remove_all_extents(type), depth + 1);
    }
    const bool is_class_or_union = std::meta::is_class_type(type) || std::meta::is_union_type(type);
    if (is_class_or_union && !std::meta::is_complete_type(type)) return false;
    if (is_class_or_union
        && ::foundation::reflect::holds_unreadable_state(::foundation::reflect::TypeNode{type, true})) {
        return false;
    }
    if (is_class_or_union && ::foundation::linear::carries_annotation(type, ^^no_start_over_bytes)) return false;
    if (is_class_or_union && ::foundation::linear::carries_annotation(type, ^^::foundation::core::closed_tag)) {
        return false;
    }
    if (!std::meta::extract<bool>(std::meta::substitute(^^std::is_implicit_lifetime_v, {type}))) return false;
    if (!is_class_or_union) return true;
    for (const std::meta::info base : std::meta::bases_of(type, std::meta::access_context::unchecked())) {
        if (!is_implicit_lifetime_throughout_at(std::meta::type_of(base), depth + 1)) return false;
    }
    for (const std::meta::info member :
         std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
        if (!is_implicit_lifetime_throughout_at(std::meta::type_of(member), depth + 1)) return false;
    }
    return true;
}

}  // namespace detail

// A type whose lifetime can start over bytes with each of its subobjects
// alive.
template <typename T>
concept ImplicitLifetimeThroughout = detail::is_implicit_lifetime_throughout_at(^^T, 0);

// T, made const when the storage is const.  A view over read-only bytes
// keeps that access in its element type.  start_as_array of
// foundation/core/Array.h gives its View this element type.
template <typename Storage, typename T>
using element_for_storage_t = std::conditional_t<std::is_const_v<Storage>, const T, T>;

namespace detail {

// A type with a private user-provided constructor, the shape of a proof,
// and a class that holds one.  The checks of this header and of the
// buffer headers that start a lifetime over bytes name the two types, so
// they live here and not in one check file.
class ProofShape {
public:
    ProofShape(const ProofShape&) noexcept {}

private:
    ProofShape() noexcept {}
};
struct HoldsProof {
    ProofShape proof;
};

}  // namespace detail

}  // namespace foundation::lifetime
