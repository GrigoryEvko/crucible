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
// is undefined behavior.  start_as_array refuses the three routes at
// compile time.  The test walks the type through each array extent,
// each base class and each non-static data member, the members of a
// union included, and it refuses a reference member, which no lifetime
// start binds.
//
// scripts/check-start-lifetime.sh refuses a direct use of the two library
// functions outside a reviewed list.  New code uses start_as_array.

#include <cstddef>
#include <memory>
#include <meta>
#include <span>
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

// Starts the lifetime of count objects of type T over the storage and
// returns them as a span.  The storage must hold count * sizeof(T) bytes
// aligned for T.  A single object is a span of one.
template <ImplicitLifetimeThroughout T>
[[nodiscard]] std::span<T> start_as_array(void* storage, std::size_t count) noexcept {
    return {std::start_lifetime_as_array<T>(storage, count), count};
}

// Starts the lifetime of count const objects of type T over the storage
// and returns them as a span.  The storage must hold count * sizeof(T)
// bytes aligned for T.
template <ImplicitLifetimeThroughout T>
[[nodiscard]] std::span<const T> start_as_array(const void* storage, std::size_t count) noexcept {
    return {std::start_lifetime_as_array<T>(storage, count), count};
}

namespace detail::lifetime_self_test {

// A type with a private user-provided constructor, the shape of a proof.
class ProofShape {
public:
    ProofShape(const ProofShape&) noexcept {}

private:
    ProofShape() noexcept {}
};
struct HoldsProof {
    ProofShape proof;
};
struct DerivesProof : ProofShape {};
union ProofOrByte {
    unsigned char byte;
    ProofShape proof;
};
struct HoldsReference {
    int& target;
};
struct Plain {
    int value;
    double weight[4];
};
struct Nested {
    Plain inner[2];
    unsigned char tail;
};

static_assert(ImplicitLifetimeThroughout<int> && ImplicitLifetimeThroughout<const int>);
static_assert(ImplicitLifetimeThroughout<Plain> && ImplicitLifetimeThroughout<Nested>);
static_assert(ImplicitLifetimeThroughout<Plain[3]> && ImplicitLifetimeThroughout<int*>);
static_assert(!ImplicitLifetimeThroughout<ProofShape>, "a proof type is not implicit-lifetime");
static_assert(!ImplicitLifetimeThroughout<ProofShape[1]>,
              "an array of proofs is an implicit-lifetime type, and the walk must refuse it");
static_assert(std::is_implicit_lifetime_v<HoldsProof> && !ImplicitLifetimeThroughout<HoldsProof>,
              "an aggregate that holds a proof is implicit-lifetime, and the walk must refuse it");
static_assert(!ImplicitLifetimeThroughout<DerivesProof>, "a base class is a subobject");
static_assert(!ImplicitLifetimeThroughout<ProofOrByte>, "a union member is a subobject");
static_assert(!ImplicitLifetimeThroughout<HoldsReference>, "no lifetime start binds a reference member");

}  // namespace detail::lifetime_self_test

}  // namespace foundation::lifetime
