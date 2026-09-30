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
// The walk also refuses a class whose state it cannot read.  A lambda
// with captures has a trivial copy constructor, so it is implicit-lifetime,
// and GCC 16 reflects no capture, so the walk finds no subobject in it.  A
// closure that captures an epoch would give an epoch that no successor step
// made.  foundation/reflect/TypeComponents.h names the shape, and the walk
// refuses it as a subobject at every depth.
//
// A class can also refuse a lifetime start while it stays implicit-lifetime.
// A value that must come only from its own doors, such as a count or a
// version, keeps a trivial copy constructor so that the ABI passes it in a
// register, and that trivial constructor makes it implicit-lifetime.  Such a
// class carries the annotation no_start_over_bytes, and the walk refuses it
// wherever it sits: alone, in an array, as a base or as a member.
//
// utils/scripts/check-start-lifetime.py refuses a direct use of the two library
// functions outside a reviewed list.  New code uses start_as_array.

#include <foundation/reflect/TypeComponents.h>

#include <concepts>
#include <cstddef>
#include <memory>
#include <meta>
#include <span>
#include <type_traits>

namespace foundation::lifetime {

// The annotation of a class whose lifetime must never start over bytes.
// Spell it on the class: struct [[=::foundation::lifetime::no_start_over_bytes{}]] X.
struct no_start_over_bytes {};

// A member that closes the two byte routes to the class that holds it, and
// keeps the call ABI of that class.
//
// std::bit_cast builds a trivially copyable class from bytes, and the
// checked lifetime start builds an implicit-lifetime class over bytes.
// Neither calls a constructor, so neither meets the door of a proof.  A
// proof that the ABI passes in a register keeps trivial copy and move
// constructors and a trivial destructor.  GCC also counts a class whose
// copies and moves are all deleted as trivially copyable.  Such a class
// holds one seal:
//
//     [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
//
// The assignments of the seal are user-provided, so the assignments of the
// class are not trivial, and the class is not trivially copyable.  The seal
// carries no_start_over_bytes, so the checked lifetime start refuses the
// class.  The copy and move constructors and the destructor of the seal are
// trivial, so the Itanium ABI still passes the class in registers.  The
// seal adds no byte, and a class that holds only a seal stays empty.  A
// class that holds a sealed member is sealed through that member, and a
// second seal in it can need a byte of its own.
struct[[= no_start_over_bytes{}]] byte_seal {
    constexpr byte_seal() noexcept = default;
    constexpr byte_seal(const byte_seal&) noexcept = default;
    constexpr byte_seal(byte_seal&&) noexcept = default;
    constexpr byte_seal& operator=(const byte_seal&) noexcept { return *this; }
    constexpr byte_seal& operator=(byte_seal&&) noexcept { return *this; }
    ~byte_seal() = default;
};

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
    if (is_class_or_union && !std::meta::annotations_of_with_type(type, ^^no_start_over_bytes).empty()) return false;
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
// keeps that access in its element type.
template <typename Storage, typename T>
using element_for_storage_t = std::conditional_t<std::is_const_v<Storage>, const T, T>;

// Starts the lifetime of count objects of type T over the storage and
// returns them as a span.  The storage must hold count * sizeof(T) bytes
// aligned for T.  The element of the span is const when the storage or T
// is const.  A single object is a span of one.
//
// Volatile storage is refused, because libstdc++ cannot build a span of a
// volatile class type.  A caller that reads through volatile converts
// data() to a pointer to volatile, which is an implicit conversion.
template <ImplicitLifetimeThroughout T, typename Storage>
    requires(!std::is_volatile_v<Storage> && !std::is_volatile_v<T>)
[[nodiscard]] std::span<element_for_storage_t<Storage, T>> start_as_array(Storage* storage,
                                                                          std::size_t count) noexcept {
    // libstdc++ names the result in an asm output, and a const element
    // type fails there.  So the start takes T without its qualifiers, and
    // the span adds the const back.
    return {std::start_lifetime_as_array<std::remove_cv_t<T>>(storage, count), count};
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
// A union with a proof member.  The union is local to the function, so no
// namespace of foundation holds a union with a proof member, and the union
// audit of test/fixy/test_forgeable_proofs.cpp needs no exception.
[[nodiscard]] consteval bool refuses_a_union_member() {
    union ProofOrByte {
        unsigned char byte;
        ProofShape proof;
    };
    return !ImplicitLifetimeThroughout<ProofOrByte>;
}
static_assert(refuses_a_union_member(), "a union member is a subobject");
static_assert(!ImplicitLifetimeThroughout<HoldsReference>, "no lifetime start binds a reference member");

// A marked class is implicit-lifetime, so the marker alone refuses it, and
// the walk carries the refusal through a member, an array, a base and a
// union member.
struct[[= ::foundation::lifetime::no_start_over_bytes{}]] MarkedCount {
    unsigned long long count = 0;
};
struct HoldsMarked {
    MarkedCount count;
};
struct DerivesMarked : MarkedCount {};
static_assert(std::is_implicit_lifetime_v<MarkedCount> && !ImplicitLifetimeThroughout<MarkedCount>,
              "the marker refuses a class that is implicit-lifetime");
static_assert(!ImplicitLifetimeThroughout<HoldsMarked> && !ImplicitLifetimeThroughout<MarkedCount[2]>
                  && !ImplicitLifetimeThroughout<DerivesMarked>,
              "the marker refuses the class as a member, as an array element and as a base");
[[nodiscard]] consteval bool refuses_a_marked_union_member() {
    union MarkedOrByte {
        unsigned char byte;
        MarkedCount count;
    };
    return !ImplicitLifetimeThroughout<MarkedOrByte>;
}
static_assert(refuses_a_marked_union_member(), "the marker refuses the class as a union member");

// A closure with captures is implicit-lifetime, and the walk cannot read
// what it holds, so the walk refuses it alone, as a member, as an array
// element and as a base.  A closure with no capture holds nothing.
inline constexpr auto carries_a_count = [count = 7ULL] { return count; };
inline constexpr auto carries_nothing = [] { return 7ULL; };
using CountCarrier = std::remove_const_t<decltype(carries_a_count)>;
struct HoldsCountCarrier {
    CountCarrier carrier;
};
struct DerivesCountCarrier : CountCarrier {};
static_assert(std::is_implicit_lifetime_v<CountCarrier> && !ImplicitLifetimeThroughout<CountCarrier>,
              "a closure with captures is implicit-lifetime, and the walk must refuse it");
static_assert(!ImplicitLifetimeThroughout<HoldsCountCarrier> && !ImplicitLifetimeThroughout<CountCarrier[2]>
                  && !ImplicitLifetimeThroughout<DerivesCountCarrier>,
              "the walk refuses the closure as a member, as an array element and as a base");
static_assert(ImplicitLifetimeThroughout<std::remove_const_t<decltype(carries_nothing)>>);

// The element of the span is const when the storage or T is const, and
// volatile storage is refused.
template <typename Storage, typename T>
concept can_start_as = requires(Storage* storage) {
    { start_as_array<T>(storage, 1) } -> std::same_as<std::span<element_for_storage_t<Storage, T>>>;
};
static_assert(can_start_as<unsigned char, Plain> && can_start_as<void, Plain>);
static_assert(can_start_as<const unsigned char, Plain> && can_start_as<unsigned char, const Plain>);
static_assert(std::is_same_v<element_for_storage_t<const unsigned char, Plain>, const Plain>);
static_assert(std::is_same_v<element_for_storage_t<unsigned char, const Plain>, const Plain>);
static_assert(!can_start_as<volatile unsigned char, Plain> && !can_start_as<const volatile void, Plain>,
              "volatile storage is refused, and a caller adds volatile to data()");
static_assert(!can_start_as<unsigned char, HoldsProof> && !can_start_as<const void, ProofShape[1]>,
              "the checked start refuses a proof subobject through every storage qualifier");

// A sealed class keeps a trivial copy, a trivial destructor and its size,
// and the two byte routes refuse it.  A class that holds only a seal stays
// empty, and a class that holds a sealed member is sealed.
class SealedView {
    const int* target_ = nullptr;
    [[no_unique_address]] byte_seal seal_{};

public:
    constexpr SealedView() noexcept = default;
};
class SealedWitness {
    [[no_unique_address]] byte_seal seal_{};

public:
    constexpr SealedWitness() noexcept {}
    SealedWitness(const SealedWitness&) = delete;
    SealedWitness& operator=(const SealedWitness&) = delete;
};
struct HoldsSealedView {
    SealedView view;
};
static_assert(!std::is_trivially_copyable_v<byte_seal> && !ImplicitLifetimeThroughout<byte_seal>);
static_assert(!std::is_trivially_copyable_v<SealedView> && !ImplicitLifetimeThroughout<SealedView>,
              "std::bit_cast and the checked lifetime start refuse a sealed class");
static_assert(std::is_trivially_copy_constructible_v<SealedView> && std::is_trivially_move_constructible_v<SealedView>
                  && std::is_trivially_destructible_v<SealedView>,
              "a sealed class keeps the trivial copy that passes it in registers");
static_assert(sizeof(SealedView) == sizeof(const int*), "the seal adds no byte");
static_assert(std::is_empty_v<SealedWitness> && !std::is_trivially_copyable_v<SealedWitness>,
              "a pinned class that holds only a seal stays empty, and the trait stops calling it trivially copyable");
static_assert(!std::is_trivially_copyable_v<HoldsSealedView> && !ImplicitLifetimeThroughout<HoldsSealedView>,
              "a class that holds a sealed member is sealed through it");

}  // namespace detail::lifetime_self_test

}  // namespace foundation::lifetime
