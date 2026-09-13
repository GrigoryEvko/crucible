#pragma once

// Fn is a facade over a product of single-axis grades, not a stack of
// them. Expressing the same product as one nested Graded per axis
// would be algebraically equivalent but would put a nineteen-deep
// type expression at every binding declaration, so the axes are flat
// positional parameters with defaults instead.
//
// The nested form stays authoritative for cross-axis hash folding.
// The fold below reproduces that canonical order, so a value hashes
// the same whether it is materialised as an Fn or as the equivalent
// wrapper stack.

#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/Tagged.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety::fn {

namespace pred {
struct True {
    template <typename T>
    [[nodiscard]] static constexpr bool check(const T&) noexcept {
        return true;
    }
};
}  // namespace pred

enum class UsageMode : std::uint8_t {
    Linear = 0,  // exactly-once consumption
    Affine = 1,  // at-most-once
    Copy = 2,  // copyable, non-linear
    Ghost = 3,  // erased at codegen, ghost-only
    Borrow = 4,  // non-owning borrow capture
    Capability = 5,  // ephemeral external capability
};

enum class SecLevel : std::uint8_t {
    Unclassified = 0,  // freely observable
    Public = 1,  // observable
    Internal = 2,  // organization-internal
    Classified = 3,  // observation requires declassify
    Secret = 4,  // top of lattice — never declassified
};

namespace proto {
struct None {};  // no protocol obligation
}  // namespace proto

namespace lifetime {
struct Static {};  // valid for the entire program
template <auto RegionTag>
struct In {};  // valid within a named region
}  // namespace lifetime

namespace source = crucible::safety::source;

namespace trust = crucible::safety::trust;

enum class ReprKind : std::uint8_t {
    Opaque = 0,  // layout opaque
    C = 1,  // standard layout
    Packed = 2,  // no padding
    Aligned = 3,  // alignment hint
    Simd = 4,  // SIMD-vector layout
    Atomic = 5,  // atomic representation, CAS-capable carrier
};

namespace cost {
struct Unstated {};  // no claim, and an unbounded cost must be declared
struct Constant {};  // O(1)
template <auto N>
struct Linear {};  // O(N)
template <auto N>
struct Quadratic {};  // O(N^2)
struct Unbounded {};  // explicit unbounded
}  // namespace cost

namespace precision {
struct Exact {};  // bit-exact
struct F32 {};
struct F64 {};
template <auto Bound>
struct Higham {};  // Higham bound
}  // namespace precision

namespace space {
struct Zero {};  // stack only
struct Unbounded {};
template <auto N>
struct Bounded {};
}  // namespace space

enum class OverflowMode : std::uint8_t {
    Trap = 0,  // abort on overflow
    Wrap = 1,  // 2^N modular
    Saturate = 2,  // clamp to T's range
    Widen = 3,  // widen result type
};

enum class MutationMode : std::uint8_t {
    Immutable = 0,  // no in-place mutation
    Mutable = 1,  // arbitrary in-place mutation permitted
    Append = 2,  // append-only mutation
    Monotonic = 3,  // monotonic-advance mutation only
};

enum class ReentrancyMode : std::uint8_t {
    NonReentrant = 0,  // self-call rejected
    Reentrant = 1,  // self-call permitted
    Coroutine = 2,  // suspendable, resumable
};

// Codata observation depth.
namespace size_pol {
struct Unstated {};  // no claim, and a depth must be declared
template <auto Depth>
struct Sized {};
struct Productive {};  // codata
}  // namespace size_pol

namespace stale {
struct Fresh {};  // no staleness admitted
template <auto TauMax>
struct Stale {};
}  // namespace stale

// The axes below are wrapper-only: none of them has an Fn parameter
// slot, because the claim is per-value rather than per-binding, and
// the wrapper that carries it goes on the value at the call site.
// Each namespace exists so the defaults table can name a type for its
// axis and satisfy the reflection-driven coverage check that every
// axis resolves to something. Unconstrained is the strict default in
// each: the binding makes no claim at all.

namespace sync {
struct Unconstrained {};
}  // namespace sync

namespace regime {
struct Unconstrained {};
}  // namespace regime

namespace fp_mode {
struct Unconstrained {};
}  // namespace fp_mode

namespace syscall {
struct Unconstrained {};
}  // namespace syscall

namespace control_flow {
struct Unconstrained {};
}  // namespace control_flow
namespace call_shape {
struct Unconstrained {};
}  // namespace call_shape
namespace stack_use {
struct Unconstrained {};
}  // namespace stack_use
namespace global_state {
struct Unconstrained {};
}  // namespace global_state
namespace stdio {
struct Unconstrained {};
}  // namespace stdio

namespace hw_instruction {
struct Unconstrained {};
}  // namespace hw_instruction
namespace barrier_strength {
struct Unconstrained {};
}  // namespace barrier_strength
namespace simd_isa {
struct Unconstrained {};
}  // namespace simd_isa

namespace memory_scope {
struct Unconstrained {};
}  // namespace memory_scope

// CollisionRules is only declared here and specialized once the Fn
// template body is visible. That order is what lets the class-body
// assertion be dependent, so a direct Fn instantiation is rejected on
// the same rules as the mint path while the rules themselves can read
// Fn's per-axis aliases.

template <typename F>
struct CollisionRules;

template <typename F>
concept ValidComposition = CollisionRules<F>::valid;

// Observability has no slot of its own: it is derived from EffectRow
// at the consumer site.

template <typename Type, typename Refinement = pred::True, UsageMode Usage = UsageMode::Linear,
          typename EffectRow = effects::Row<>, SecLevel Security = SecLevel::Classified,
          typename Protocol = proto::None, typename Lifetime = lifetime::Static, typename Source = source::FromInternal,
          typename Trust = trust::Unverified,  // Biba-safe bottom: Verified is earned, never assumed
          ReprKind Repr = ReprKind::Opaque, typename Cost = cost::Unstated, typename Precision = precision::Exact,
          typename Space = space::Zero, OverflowMode Overflow = OverflowMode::Trap,
          MutationMode Mutation = MutationMode::Immutable, ReentrancyMode Reentrancy = ReentrancyMode::NonReentrant,
          typename Size = size_pol::Unstated, std::uint32_t Version = 1, typename Staleness = stale::Fresh>
struct Fn {
    static_assert(std::is_object_v<Type>, "Fn<Type, ...> requires Type to be a complete object type. "
                                          "Reject: void, reference types, bare function types.  For "
                                          "Fixy function bindings, use a function pointer or callable "
                                          "struct, not the bare function type.");
    static_assert(!std::is_const_v<Type>, "Fn<const T, ...> is malformed.  Const-qualifying the value "
                                          "type silently deletes copy- and move-assignment of the "
                                          "wrapper, breaking move semantics + the universal mint "
                                          "discipline.  Use Fn<T, ..., MutationMode::Immutable> to "
                                          "express logical immutability while keeping the wrapper's "
                                          "value-category discipline intact.");
    static_assert(!std::is_volatile_v<Type>, "Fn<volatile T, ...> is malformed.  volatile is a hardware-"
                                             "memory annotation (memory-mapped I/O, signal-safe storage) "
                                             "— it is not a property the Fn grade vector models.  Use "
                                             "std::atomic<T> for concurrent access or annotate the "
                                             "volatility at the Type definition site.");
    static_assert(!std::is_array_v<Type>, "Fn<T[N], ...> is malformed.  C arrays decay to pointers "
                                          "in function parameters, so the wrapper's `Fn(Type v)` "
                                          "constructor would silently rebind to a pointer rather "
                                          "than copying the array.  Use Fn<std::array<T, N>, ...> "
                                          "for value-semantic fixed arrays or Fn<Borrowed<T, "
                                          "Source>, ...> for a borrowed view.");

    using type_t = Type;
    using refinement_t = Refinement;
    static constexpr UsageMode usage_v = Usage;
    using effect_row_t = EffectRow;
    static constexpr SecLevel security_v = Security;
    using protocol_t = Protocol;
    using lifetime_t = Lifetime;
    using source_t = Source;
    using trust_t = Trust;
    static constexpr ReprKind repr_v = Repr;
    using cost_t = Cost;
    using precision_t = Precision;
    using space_t = Space;
    static constexpr OverflowMode overflow_v = Overflow;
    static constexpr MutationMode mutation_v = Mutation;
    static constexpr ReentrancyMode reentrancy_v = Reentrancy;
    using size_t_ = Size;
    static constexpr std::uint32_t version_v = Version;
    using staleness_t = Staleness;

    Type value_{};

    constexpr Fn() = default;

    explicit constexpr Fn(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>) : value_{std::move(v)} {}

    template <typename Self>
    [[nodiscard]] constexpr auto&& value(this Self&& self) noexcept {
        return std::forward<Self>(self).value_;
    }

    // The gate sits in the class body, after every per-axis member is
    // declared, so that constructing an Fn directly is checked on the
    // same rules as minting one. Gating only the factory would leave
    // direct construction as a way around the catalog.
    static_assert(ValidComposition<Fn>, "Fn<...> grade combination violates a collision rule. "
                                        "See the RuleCode enum in safety/CollisionCatalog.h for the "
                                        "full list of rejected combinations.");
};

}  // namespace crucible::safety::fn

#define CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION 1
#include <crucible/safety/CollisionCatalog.h>
#undef CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION

namespace crucible::safety::fn {

// The authority comes from the caller-supplied grade pack, so there
// is no context parameter and the only gate is ValidComposition. This
// factory covers the all-defaults case. A caller wanting explicit
// per-axis grades instantiates Fn directly, which the class-body gate
// checks identically, so no second factory exists.

template <typename Type>
    requires ValidComposition<Fn<Type>>
[[nodiscard]] constexpr auto mint_fn(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>) -> Fn<Type> {
    return Fn<Type>{std::move(v)};
}

namespace detail::fn_self_test {

static_assert(sizeof(Fn<int>) == sizeof(int), "Fn<int> with all default grades MUST be byte-equivalent to int. "
                                              "If this fires, a per-axis member field was added that defeats "
                                              "EBO collapse — revert to type-level grades only.");
static_assert(sizeof(Fn<char>) == sizeof(char), "Fn<char> default grades MUST EBO-collapse.");
static_assert(sizeof(Fn<double>) == sizeof(double), "Fn<double> default grades MUST EBO-collapse.");
static_assert(sizeof(Fn<int, pred::True, UsageMode::Affine, effects::Row<>, SecLevel::Public>) == sizeof(int),
              "Customizing non-default grades MUST NOT add runtime storage. "
              "The 19-axis grade vector is type-level only.");

using DefaultFn = Fn<int>;
static_assert(std::is_same_v<DefaultFn::type_t, int>);
static_assert(std::is_same_v<DefaultFn::refinement_t, pred::True>);
static_assert(DefaultFn::usage_v == UsageMode::Linear);
static_assert(std::is_same_v<DefaultFn::effect_row_t, effects::Row<>>);
static_assert(DefaultFn::security_v == SecLevel::Classified);
static_assert(std::is_same_v<DefaultFn::protocol_t, proto::None>);
static_assert(std::is_same_v<DefaultFn::lifetime_t, lifetime::Static>);
static_assert(std::is_same_v<DefaultFn::source_t, source::FromInternal>);
static_assert(std::is_same_v<DefaultFn::trust_t, trust::Unverified>);
static_assert(DefaultFn::repr_v == ReprKind::Opaque);
static_assert(std::is_same_v<DefaultFn::cost_t, cost::Unstated>);
static_assert(std::is_same_v<DefaultFn::precision_t, precision::Exact>);
static_assert(std::is_same_v<DefaultFn::space_t, space::Zero>);
static_assert(DefaultFn::overflow_v == OverflowMode::Trap);
static_assert(DefaultFn::mutation_v == MutationMode::Immutable);
static_assert(DefaultFn::reentrancy_v == ReentrancyMode::NonReentrant);
static_assert(std::is_same_v<DefaultFn::size_t_, size_pol::Unstated>);
static_assert(DefaultFn::version_v == 1);
static_assert(std::is_same_v<DefaultFn::staleness_t, stale::Fresh>);

using CustomFn =
    Fn<float, pred::True, UsageMode::Affine, effects::Row<>, SecLevel::Public, proto::None, lifetime::Static,
       source::FromUser, trust::Tested, ReprKind::C, cost::Constant, precision::F32, space::Zero,
       OverflowMode::Saturate, MutationMode::Mutable, ReentrancyMode::Reentrant, size_pol::Unstated,
       /*Version=*/3, stale::Fresh>;
static_assert(CustomFn::usage_v == UsageMode::Affine);
static_assert(CustomFn::security_v == SecLevel::Public);
static_assert(std::is_same_v<CustomFn::source_t, source::FromUser>);
static_assert(std::is_same_v<CustomFn::trust_t, trust::Tested>);
static_assert(CustomFn::repr_v == ReprKind::C);
static_assert(CustomFn::overflow_v == OverflowMode::Saturate);
static_assert(CustomFn::mutation_v == MutationMode::Mutable);
static_assert(CustomFn::reentrancy_v == ReentrancyMode::Reentrant);
static_assert(CustomFn::version_v == 3);

static_assert(ValidComposition<Fn<int>>, "Default Fn<...> grades must satisfy the collision catalog.");
static_assert(ValidComposition<CustomFn>);

static_assert(mint_fn(42).value_ == 42);
static_assert(std::is_same_v<decltype(mint_fn(42)), Fn<int>>);
static_assert(std::is_same_v<decltype(mint_fn(3.14)), Fn<double>>);

[[nodiscard]] consteval std::size_t usage_mode_count() noexcept {
    return std::meta::enumerators_of(^^UsageMode).size();
}
[[nodiscard]] consteval std::size_t sec_level_count() noexcept { return std::meta::enumerators_of(^^SecLevel).size(); }
[[nodiscard]] consteval std::size_t repr_kind_count() noexcept { return std::meta::enumerators_of(^^ReprKind).size(); }
[[nodiscard]] consteval std::size_t overflow_mode_count() noexcept {
    return std::meta::enumerators_of(^^OverflowMode).size();
}
[[nodiscard]] consteval std::size_t mutation_mode_count() noexcept {
    return std::meta::enumerators_of(^^MutationMode).size();
}
[[nodiscard]] consteval std::size_t reentrancy_mode_count() noexcept {
    return std::meta::enumerators_of(^^ReentrancyMode).size();
}

static_assert(usage_mode_count() == 6, "UsageMode enumerator count drift — every consumer with switch "
                                       "arms on UsageMode must add the new arm.");
static_assert(sec_level_count() == 5, "SecLevel enumerator count drift.");
static_assert(repr_kind_count() == 6, "ReprKind enumerator count drift.");
static_assert(overflow_mode_count() == 4, "OverflowMode enumerator count drift.");
static_assert(mutation_mode_count() == 4, "MutationMode enumerator count drift.");
static_assert(reentrancy_mode_count() == 3, "ReentrancyMode enumerator count drift.");

static_assert(tier_of_axis(DimensionAxis::Type) == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Refinement) == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Usage) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Effect) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Security) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Protocol) == TierKind::Typestate);
static_assert(tier_of_axis(DimensionAxis::Lifetime) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Provenance) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Trust) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Representation) == TierKind::Lattice);
static_assert(tier_of_axis(DimensionAxis::Complexity) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Precision) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Space) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Overflow) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Mutation) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Reentrancy) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Size) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Version) == TierKind::Versioned);
static_assert(tier_of_axis(DimensionAxis::Staleness) == TierKind::Semiring);

// The NSDMI on value_ costs trivial default construction, and that is
// the intended trade: zero-initializing by default rules out an
// uninitialized read.
static_assert(std::is_default_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_default_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_move_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_move_assignable_v<Fn<int>>);
static_assert(std::is_nothrow_copy_constructible_v<Fn<int>>);
static_assert(std::is_trivially_destructible_v<Fn<int>>);

// Positive coverage for the four type gates above: an over-broad
// rejection is caught here rather than at a downstream call site.

static_assert(sizeof(Fn<unsigned int>) == sizeof(unsigned int));
static_assert(sizeof(Fn<long long>) == sizeof(long long));

// A const or array MEMBER is admissible. Only a const or array Type
// is not.
struct AggregateWithConstMember {
    const int x = 0;
    int y = 0;
};
struct AggregateWithArrayMember {
    int xs[4]{};
};
static_assert(sizeof(Fn<AggregateWithConstMember>) == sizeof(AggregateWithConstMember));
static_assert(sizeof(Fn<AggregateWithArrayMember>) == sizeof(AggregateWithArrayMember));

// The pointer is non-const, so the const pointee is admissible.
static_assert(sizeof(Fn<const int*>) == sizeof(const int*));

}  // namespace detail::fn_self_test

}  // namespace crucible::safety::fn

// The fold below walks the axes in declaration order. combine_ids
// mixes golden-ratio style and so is order-sensitive, and Fn's axes
// are positional, which is what separates an axis's position from its
// value without any canonicalization step.

#include <crucible/safety/diag/RowHashFold.h>

namespace crucible::safety::diag {

template <typename Type, typename Refinement, safety::fn::UsageMode Usage, typename EffectRow,
          safety::fn::SecLevel Security, typename Protocol, typename Lifetime, typename Source, typename Trust,
          safety::fn::ReprKind Repr, typename Cost, typename Precision, typename Space,
          safety::fn::OverflowMode Overflow, safety::fn::MutationMode Mutation, safety::fn::ReentrancyMode Reentrancy,
          typename Size, std::uint32_t Version, typename Staleness>
struct row_hash_contribution<
    safety::fn::Fn<Type, Refinement, Usage, EffectRow, Security, Protocol, Lifetime, Source, Trust, Repr, Cost,
                   Precision, Space, Overflow, Mutation, Reentrancy, Size, Version, Staleness>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        // The salt keeps the result off the zero slot in the case
        // where every enum axis holds its zero enumerator.
        std::uint64_t h = detail::WRAPPER_SAFETY_FN_TAG;

        // The payload may itself be a row-bearing wrapper stack. A
        // bare payload contributes zero and the later axes carry the
        // discrimination.
        h = detail::combine_ids(h, row_hash_contribution_v<Type>);

        // stable_type_id folds the reflection display name, so it is
        // stable only within one build.
        h = detail::combine_ids(h, stable_type_id<Refinement>);
        h = detail::combine_ids(h, stable_type_id<Protocol>);
        h = detail::combine_ids(h, stable_type_id<Lifetime>);
        h = detail::combine_ids(h, stable_type_id<Source>);
        h = detail::combine_ids(h, stable_type_id<Trust>);
        h = detail::combine_ids(h, stable_type_id<Cost>);
        h = detail::combine_ids(h, stable_type_id<Precision>);
        h = detail::combine_ids(h, stable_type_id<Space>);
        h = detail::combine_ids(h, stable_type_id<Size>);
        h = detail::combine_ids(h, stable_type_id<Staleness>);

        // Delegating to the row specialization is what makes the
        // effect row fold as a set, invariant under permutation.
        h = detail::combine_ids(h, row_hash_contribution_v<EffectRow>);

        h = detail::combine_ids(h, static_cast<std::uint64_t>(Usage));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Security));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Repr));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Overflow));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Mutation));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Reentrancy));

        h = detail::combine_ids(h, static_cast<std::uint64_t>(Version));
        return h;
    }();
};

namespace detail::fn_row_hash_self_test {

using crucible::safety::fn::Fn;
using crucible::safety::fn::UsageMode;
using crucible::safety::fn::SecLevel;
using crucible::safety::fn::ReprKind;
using crucible::safety::fn::OverflowMode;
using crucible::safety::fn::MutationMode;
using crucible::safety::fn::ReentrancyMode;
using crucible::effects::Effect;
using crucible::effects::Row;

static_assert(row_hash_contribution_v<int> == 0);
static_assert(row_hash_contribution_v<Fn<int>> != 0);
static_assert(row_hash_contribution_v<Fn<int>> != row_hash_contribution_v<int>);

static_assert(row_hash_contribution_v<Fn<int>> != 0);
static_assert(row_hash_contribution_v<Fn<float>> != 0);
// Two default-axis Fns over bare payloads of different types hash the
// same, and that is correct: the row hash discriminates rows, not
// payloads, and both bare payloads contribute zero. Payload identity
// is separated at the cache-lookup tier instead.
static_assert(row_hash_contribution_v<Fn<int>> == row_hash_contribution_v<Fn<float>>);

static_assert(row_hash_contribution_v<Fn<int>>
              != row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True, UsageMode::Copy>>);

static_assert(
    row_hash_contribution_v<Fn<int>>
    != row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True, UsageMode::Linear, Row<>, SecLevel::Public>>);

static_assert(
    row_hash_contribution_v<Fn<int>>
    != row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True, UsageMode::Linear, Row<Effect::IO>>>);

// Permutation invariance, inherited from the set-semantic row fold.
static_assert(
    row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True, UsageMode::Linear, Row<Effect::IO, Effect::Bg>>>
    == row_hash_contribution_v<
        Fn<int, crucible::safety::fn::pred::True, UsageMode::Linear, Row<Effect::Bg, Effect::IO>>>);

static_assert(!row_hash_of_v<Fn<int>>.is_sentinel());

}  // namespace detail::fn_row_hash_self_test

}  // namespace crucible::safety::diag
