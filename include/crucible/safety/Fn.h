#pragma once

// ── crucible::safety — Fn.h (Phase 0 P0-1) ──────────────────────────
//
// The unified Fn<...> aggregator template that bundles a value with
// its 20-axis grade vector per fixy.md §24.1.  Every Fixy binding
// ultimately compiles to an `Fn<Type, ...>` instantiation; the
// substrate enforces zero runtime cost (`sizeof(Fn<int>) ==
// sizeof(int)`) so the entire dispatch vocabulary lives in the type
// system.
//
//   Aggregates: 19 positional template parameters (the 20 fixy
//               dimensions minus dim 11 Observability, which is
//               derived from EffectRow per fixy.md §24.1)
//   Tier basis: routes through DimensionTraits.h (P0-3, #1094) —
//               TierKind / DimensionAxis / tier_of_axis<D>
//   ValidComposition gate: PLACEHOLDER (always-true) — the 12 §6.8
//               collision rules ship in safety/CollisionCatalog.h
//               (P0-2, #1096); when that lands, this concept gate
//               picks up the disjunction body
//   Universal mint pattern: `mint_fn(value)` per CLAUDE.md §XXI
//               (token mint flavor — derives authority from the
//               caller-supplied per-axis grade pack, no Ctx)
//
// ── Dimension catalog (fixy.md §24.1) ──────────────────────────────
//
//   #  Tier  Param name      Default                       Notes
//   ── ────  ──────────────  ───────────────────────────── ─────────
//    1  F    Type            (required)                    fn arg
//    2  F    Refinement      pred::True                    EBO
//    3  S    Usage           UsageMode::Linear             enum
//    4  S    EffectRow       effects::Row<>                empty row
//    5  S    Security        SecLevel::Classified          enum
//    6  T    Protocol        proto::None                   EBO
//    7  S    Lifetime        lifetime::Static              EBO
//    8  S    Source          source::FromInternal          re-export
//    9  S    Trust           trust::Verified               re-export
//   10  L    Repr            ReprKind::Opaque              enum
//   11  S    (Observability) — derived from EffectRow      not stored
//   12  —    (FX dim 12 Clock Domain dropped per §24.1)
//   13  S    Cost            cost::Unstated                EBO
//   14  S    Precision       precision::Exact              EBO
//   15  S    Space           space::Zero                   EBO
//   16  S    Overflow        OverflowMode::Trap            enum
//   17  —    (FX dim 17 FP Order dropped per §24.1)
//   18  S    Mutation        MutationMode::Immutable       enum
//   19  S    Reentrancy      ReentrancyMode::NonReentrant  enum
//   20  S    Size            size::Unstated                EBO
//   21  V    Version         1                             u32
//   22  S    Staleness       stale::Fresh                  EBO
//
// ── Storage discipline (zero-runtime-cost) ──────────────────────────
//
// Fn<...> carries EXACTLY ONE runtime member: `Type value_`.  All
// other dimension grades are type-level metadata accessible via
// static member aliases (`type_t`, `usage_v`, `effect_row_t`, …) and
// do not contribute to sizeof.  This means:
//
//   sizeof(Fn<int>)                                 == sizeof(int)
//   sizeof(Fn<int, pred::True, UsageMode::Affine>)  == sizeof(int)
//   sizeof(Fn<std::array<float, 4>>)                == sizeof(std::array<float, 4>)
//
// Static layout invariants below pin this contract.  A regression
// where a future maintainer adds a per-axis runtime member would
// fail the sentinel TU build at the static_assert site, not in
// production downstream.
//
// ── Why not Graded<M, L, T>? ────────────────────────────────────────
//
// Graded<M, L, T> models a SINGLE-AXIS grade.  Fn<...> is a 19-axis
// product.  Naively wrapping each axis as Graded<...> would produce
// a 19-deep nest with redundant metadata at each level.  The
// canonical wrapper-nesting order (CLAUDE.md §XVI) IS the algebraic
// product, but it is ergonomically toxic at the binding-declaration
// level (a 19-line type expression for a single function).  Fn<...>
// is the FACADE over the product: one positional template
// parameter per axis with sane defaults; the substrate's underlying
// Graded<...> machinery powers the per-axis composition.
//
// The wrapper-nesting form REMAINS authoritative for cross-axis
// hash folding (row_hash, FOUND-I02); Fn<...> exposes the same
// hash via reflection-driven traversal of its per-axis members,
// matching the canonical-order fold so federation cache keys are
// consistent regardless of whether a value is materialized as
// `Fn<int, ...>` or `HotPath<Hot, DetSafe<Pure, Computation<R, int>>>`.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — `Type value_` is the only runtime member; if T has
//                NSDMI, the wrapper inherits it; if not, the wrapper
//                MUST be brace-initialized (the explicit ctor takes
//                T by value).
//   TypeSafe   — every per-axis enum is `enum class : uint8_t`; the
//                accessor `usage_v` returns the strong type, no
//                implicit conversion to the underlying integer.
//   NullSafe   — no pointer members.
//   MemSafe    — defaulted copy/move; T's move semantics carry
//                through; no allocations performed by Fn.
//   BorrowSafe — no mutation of cross-cutting state.
//   ThreadSafe — no atomics; the per-thread soundness of an Fn is
//                exactly the per-thread soundness of T.
//   LeakSafe   — no resources owned beyond T.
//   DetSafe    — every operation is constexpr; the 19-axis grade is
//                a STATIC property of the binding.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// `sizeof(Fn<T, ...>) == sizeof(T)` — pinned by the static_assert
// block.  Construction cost = T's construction cost.  Moves +
// copies = T's moves + copies.  Accessor template aliases compile
// to immediate values under -O3.  The 19-axis grade vector imposes
// ZERO bytes of runtime overhead.
//
// ── References ────────────────────────────────────────────────────
//
//   misc/fixy.md §24.1            — the 20-dimension grade vector
//   misc/fixy.md §24.2            — §6.8 ValidComposition catalog
//   misc/02_05_2026.md            — Phase 0 commitment (P0-1 row)
//   safety/DimensionTraits.h      — Tier S/L/T/F/V dispatch (P0-3)
//   safety/Tagged.h               — source::* and trust::* re-exports
//   effects/EffectRow.h           — effects::Row<...> for dim 4
//   CLAUDE.md §XXI                — universal mint pattern
//   CLAUDE.md §XVI                — canonical wrapper-nesting order

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

// ═════════════════════════════════════════════════════════════════════
// ── Per-axis vocabulary (Phase 0 P0-1) ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each axis's vocabulary lives in its own sub-namespace so the
// production callsite reads `fn::source::FromUser`, `fn::stale::Fresh`,
// etc.  Where the substrate already ships an authoritative type
// (Tagged.h source/trust, effects::Row), Fn re-exports it; where
// the substrate has no type yet, Fn ships a minimal placeholder.
// The placeholders are EMPTY (zero-state) on purpose — they exist
// only as type-level dispatch tags.

// ── Dim 2 Refinement (Tier-F) — predicate carrier ──────────────────
namespace pred {
    struct True {
        template <typename T>
        [[nodiscard]] static constexpr bool check(const T&) noexcept { return true; }
    };
}

// ── Dim 3 Usage (Tier-S) ───────────────────────────────────────────
enum class UsageMode : std::uint8_t {
    Linear = 0,   // exactly-once consumption (default)
    Affine = 1,   // at-most-once
    Copy   = 2,   // copyable, non-linear
    Ghost  = 3,   // erased at codegen, ghost-only
};

// ── Dim 5 Security (Tier-S) ────────────────────────────────────────
enum class SecLevel : std::uint8_t {
    Unclassified = 0,  // freely observable
    Public       = 1,  // observable
    Internal     = 2,  // organization-internal
    Classified   = 3,  // default — observation requires declassify
    Secret       = 4,  // top of lattice — never declassified
};

// ── Dim 6 Protocol (Tier-T) ────────────────────────────────────────
namespace proto {
    struct None {};   // no protocol obligation (default)
}

// ── Dim 7 Lifetime (Tier-S) ────────────────────────────────────────
namespace lifetime {
    struct Static {};                     // valid for the entire program (default)
    template <auto RegionTag> struct In {};  // valid within a named region
}

// ── Dim 8 Provenance (Tier-S) — re-export Tagged.h source::* ───────
//
// We use `safety::source::FromInternal` as the default tag (matches
// fixy.md spec's `source::Internal` semantically — the existing
// substrate already established the `From*` naming convention).
namespace source = crucible::safety::source;

// ── Dim 9 Trust (Tier-S) — re-export Tagged.h trust::* ─────────────
namespace trust = crucible::safety::trust;

// ── Dim 10 Representation (Tier-L) ─────────────────────────────────
enum class ReprKind : std::uint8_t {
    Opaque  = 0,  // layout opaque — default
    C       = 1,  // repr(C) — standard layout
    Packed  = 2,  // repr(packed) — no padding
    Aligned = 3,  // repr(align(N)) — alignment hint
    Simd    = 4,  // SIMD-vector layout
};

// ── Dim 13 Complexity (Tier-S) — cost annotation ──────────────────
namespace cost {
    struct Unstated {};                     // default — must declare unbounded
    struct Constant {};                     // O(1)
    template <auto N> struct Linear {};     // O(N)
    template <auto N> struct Quadratic {};  // O(N^2)
    struct Unbounded {};                    // explicit unbounded
}

// ── Dim 14 Precision (Tier-S) — FP error bound ────────────────────
namespace precision {
    struct Exact {};                        // default — bit-exact
    struct F32 {};
    struct F64 {};
    template <auto Bound> struct Higham {}; // Higham bound
}

// ── Dim 15 Space (Tier-S) — allocation bound ──────────────────────
namespace space {
    struct Zero {};                          // default — stack only
    struct Unbounded {};
    template <auto N> struct Bounded {};
}

// ── Dim 16 Overflow (Tier-S) ──────────────────────────────────────
enum class OverflowMode : std::uint8_t {
    Trap     = 0,  // default — abort on overflow
    Wrap     = 1,  // 2^N modular
    Saturate = 2,  // clamp to T's range
    Widen    = 3,  // widen result type
};

// ── Dim 18 Mutation (Tier-S) ──────────────────────────────────────
enum class MutationMode : std::uint8_t {
    Immutable = 0,  // default — no in-place mutation
    Mutable   = 1,  // arbitrary in-place mutation permitted
    Append    = 2,  // append-only mutation
    Monotonic = 3,  // monotonic-advance mutation only
};

// ── Dim 19 Reentrancy (Tier-S) ────────────────────────────────────
enum class ReentrancyMode : std::uint8_t {
    NonReentrant = 0,  // default — self-call rejected
    Reentrant    = 1,  // self-call permitted
    Coroutine    = 2,  // suspendable, resumable
};

// ── Dim 20 Size (Tier-S) — codata observation depth ───────────────
namespace size_pol {
    struct Unstated {};                       // default — must declare
    template <auto Depth> struct Sized {};
    struct Productive {};                     // codata
}

// ── Dim 22 Staleness (Tier-S) ──────────────────────────────────────
namespace stale {
    struct Fresh {};                          // default — τ = 0
    template <auto TauMax> struct Stale {};
}

// ═════════════════════════════════════════════════════════════════════
// ── ValidComposition concept gate (Phase 0 P0-1 placeholder) ───────
// ═════════════════════════════════════════════════════════════════════
//
// The 12 §6.8 collision rules ship in safety/CollisionCatalog.h
// (P0-2, #1096).  When CollisionCatalog.h lands it specializes this
// concept's body to the disjunction `!(I002 || L002 || E044 || ...)`
// with structured per-rule diagnostics via safety/diag/CollisionCatalog.h.
// Until then, every Fn<...> instantiation passes this gate — that's
// intentional: P0-1 ships the SHAPE, P0-2 ships the BODY.

template <typename F>
concept ValidComposition = true;

// ═════════════════════════════════════════════════════════════════════
// ── Fn<...> aggregator template ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// 19 positional template parameters per fixy.md §24.1; dim 11
// Observability is derived from EffectRow at the consumer site
// (`with CT` regions only enforce it, per §24.1).  See the catalog
// table at the top of this file for parameter slot order.

template <
    typename       Type,
    typename       Refinement   = pred::True,
    UsageMode      Usage        = UsageMode::Linear,
    typename       EffectRow    = effects::Row<>,
    SecLevel       Security     = SecLevel::Classified,
    typename       Protocol     = proto::None,
    typename       Lifetime     = lifetime::Static,
    typename       Source       = source::FromInternal,
    typename       Trust        = trust::Verified,
    ReprKind       Repr         = ReprKind::Opaque,
    typename       Cost         = cost::Unstated,
    typename       Precision    = precision::Exact,
    typename       Space        = space::Zero,
    OverflowMode   Overflow     = OverflowMode::Trap,
    MutationMode   Mutation     = MutationMode::Immutable,
    ReentrancyMode Reentrancy   = ReentrancyMode::NonReentrant,
    typename       Size         = size_pol::Unstated,
    std::uint32_t  Version      = 1,
    typename       Staleness    = stale::Fresh
>
struct Fn {
    // ── Type constraint ──────────────────────────────────────────
    //
    // Fn<Type, ...> demands a complete object type (not void, not
    // reference, not bare function type, not abstract).  Fixy
    // function bindings emit as function POINTERS or callable
    // structs — never as bare function types — so the rejection
    // is structural for every Fixy callsite.  The test_safety_neg
    // fixtures `neg_fn_rejects_void_type` and `neg_fn_rejects_
    // reference_type` pin this contract.
    static_assert(std::is_object_v<Type>,
        "Fn<Type, ...> requires Type to be a complete object type. "
        "Reject: void, reference types, bare function types, "
        "abstract classes.  For Fixy function bindings, use a "
        "function pointer or callable struct, not the bare function "
        "type.");

    // ── Per-axis introspection surface (compile-time accessors) ───
    using type_t                                       = Type;
    using refinement_t                                 = Refinement;
    static constexpr UsageMode      usage_v            = Usage;
    using effect_row_t                                 = EffectRow;
    static constexpr SecLevel       security_v         = Security;
    using protocol_t                                   = Protocol;
    using lifetime_t                                   = Lifetime;
    using source_t                                     = Source;
    using trust_t                                      = Trust;
    static constexpr ReprKind       repr_v             = Repr;
    using cost_t                                       = Cost;
    using precision_t                                  = Precision;
    using space_t                                      = Space;
    static constexpr OverflowMode   overflow_v         = Overflow;
    static constexpr MutationMode   mutation_v         = Mutation;
    static constexpr ReentrancyMode reentrancy_v       = Reentrancy;
    using size_t_                                      = Size;
    static constexpr std::uint32_t  version_v          = Version;
    using staleness_t                                  = Staleness;

    // ── Sole runtime member ──────────────────────────────────────
    // The 19-axis grade vector is type-level only — no per-axis
    // member fields, so EBO is automatic via the absence of empty
    // bases.  See sizeof invariant in detail::fn_self_test.
    Type value_{};

    // ── Construction ────────────────────────────────────────────
    constexpr Fn() = default;

    explicit constexpr Fn(Type v)
        noexcept(std::is_nothrow_move_constructible_v<Type>)
        : value_{std::move(v)} {}

    // ── Value access (deducing-this for const/non-const) ────────
    template <typename Self>
    [[nodiscard]] constexpr auto&& value(this Self&& self) noexcept {
        return std::forward<Self>(self).value_;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_fn — universal mint pattern (CLAUDE.md §XXI) ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// Token-mint flavor: derives Fn<Type, ...> authority from the
// caller-supplied per-axis grade pack (no Ctx parameter, no
// CtxFitsX gate).  The single concept gate is `ValidComposition`;
// when CollisionCatalog.h (P0-2) lands, the gate fires for the 12
// rejected dimension compositions with structured diagnostics.
//
// Two overloads:
//   1. Default-axis: `mint_fn(value)` — every axis takes its
//      default grade.  Fast path for the common case.
//   2. Named-axis:   instantiate Fn<...> directly with explicit
//      per-axis grades, then construct from the value.  No
//      separate factory — direct construction IS the mint when
//      the user wants per-axis customization.
//
// The default-axis form satisfies the universal-mint discipline
// (single-concept requires-clause + [[nodiscard]] + constexpr +
// noexcept-when-T-is-noexcept-movable + concrete return type).

template <typename Type>
    requires ValidComposition<Fn<Type>>
[[nodiscard]] constexpr auto mint_fn(Type v)
    noexcept(std::is_nothrow_move_constructible_v<Type>) -> Fn<Type>
{
    return Fn<Type>{std::move(v)};
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time + reflection-driven coverage) ──────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::fn_self_test {

// ── Storage invariant — the load-bearing zero-runtime-cost claim ──
static_assert(sizeof(Fn<int>) == sizeof(int),
    "Fn<int> with all default grades MUST be byte-equivalent to int. "
    "If this fires, a per-axis member field was added that defeats "
    "EBO collapse — revert to type-level grades only.");
static_assert(sizeof(Fn<char>) == sizeof(char),
    "Fn<char> default grades MUST EBO-collapse.");
static_assert(sizeof(Fn<double>) == sizeof(double),
    "Fn<double> default grades MUST EBO-collapse.");
static_assert(sizeof(Fn<int, pred::True, UsageMode::Affine,
                         effects::Row<>, SecLevel::Public>) == sizeof(int),
    "Customizing non-default grades MUST NOT add runtime storage. "
    "The 19-axis grade vector is type-level only.");

// ── Default-grade introspection ───────────────────────────────────
using DefaultFn = Fn<int>;
static_assert(std::is_same_v<DefaultFn::type_t,         int>);
static_assert(std::is_same_v<DefaultFn::refinement_t,   pred::True>);
static_assert(DefaultFn::usage_v                     == UsageMode::Linear);
static_assert(std::is_same_v<DefaultFn::effect_row_t, effects::Row<>>);
static_assert(DefaultFn::security_v                  == SecLevel::Classified);
static_assert(std::is_same_v<DefaultFn::protocol_t,   proto::None>);
static_assert(std::is_same_v<DefaultFn::lifetime_t,   lifetime::Static>);
static_assert(std::is_same_v<DefaultFn::source_t,     source::FromInternal>);
static_assert(std::is_same_v<DefaultFn::trust_t,      trust::Verified>);
static_assert(DefaultFn::repr_v                      == ReprKind::Opaque);
static_assert(std::is_same_v<DefaultFn::cost_t,       cost::Unstated>);
static_assert(std::is_same_v<DefaultFn::precision_t,  precision::Exact>);
static_assert(std::is_same_v<DefaultFn::space_t,      space::Zero>);
static_assert(DefaultFn::overflow_v                  == OverflowMode::Trap);
static_assert(DefaultFn::mutation_v                  == MutationMode::Immutable);
static_assert(DefaultFn::reentrancy_v                == ReentrancyMode::NonReentrant);
static_assert(std::is_same_v<DefaultFn::size_t_,      size_pol::Unstated>);
static_assert(DefaultFn::version_v                   == 1);
static_assert(std::is_same_v<DefaultFn::staleness_t,  stale::Fresh>);

// ── Custom-grade introspection ────────────────────────────────────
using CustomFn = Fn<float, pred::True, UsageMode::Affine,
                    effects::Row<>, SecLevel::Public,
                    proto::None, lifetime::Static,
                    source::FromUser, trust::Tested,
                    ReprKind::C, cost::Constant,
                    precision::F32, space::Zero,
                    OverflowMode::Saturate, MutationMode::Mutable,
                    ReentrancyMode::Reentrant, size_pol::Unstated,
                    /*Version=*/3, stale::Fresh>;
static_assert(CustomFn::usage_v      == UsageMode::Affine);
static_assert(CustomFn::security_v   == SecLevel::Public);
static_assert(std::is_same_v<CustomFn::source_t, source::FromUser>);
static_assert(std::is_same_v<CustomFn::trust_t,  trust::Tested>);
static_assert(CustomFn::repr_v       == ReprKind::C);
static_assert(CustomFn::overflow_v   == OverflowMode::Saturate);
static_assert(CustomFn::mutation_v   == MutationMode::Mutable);
static_assert(CustomFn::reentrancy_v == ReentrancyMode::Reentrant);
static_assert(CustomFn::version_v    == 3);

// ── ValidComposition placeholder behavior ─────────────────────────
static_assert(ValidComposition<Fn<int>>,
    "P0-1 placeholder admits every Fn<...> — when P0-2 lands the "
    "12 §6.8 rules supersede this and selectively reject.");
static_assert(ValidComposition<CustomFn>);

// ── mint_fn smoke ─────────────────────────────────────────────────
static_assert(mint_fn(42).value_ == 42);
static_assert(std::is_same_v<decltype(mint_fn(42)), Fn<int>>);
static_assert(std::is_same_v<decltype(mint_fn(3.14)), Fn<double>>);

// ── Reflection-driven enum-name coverage ──────────────────────────
//
// Each per-axis enum's enumerator catalog must round-trip via
// reflection — if a maintainer adds a new enumerator and forgets to
// update downstream switch arms (e.g., in CollisionCatalog.h's
// per-axis case branches), this assert catches the drift at the
// addition site, not in a downstream compile error.

[[nodiscard]] consteval std::size_t usage_mode_count() noexcept {
    return std::meta::enumerators_of(^^UsageMode).size();
}
[[nodiscard]] consteval std::size_t sec_level_count() noexcept {
    return std::meta::enumerators_of(^^SecLevel).size();
}
[[nodiscard]] consteval std::size_t repr_kind_count() noexcept {
    return std::meta::enumerators_of(^^ReprKind).size();
}
[[nodiscard]] consteval std::size_t overflow_mode_count() noexcept {
    return std::meta::enumerators_of(^^OverflowMode).size();
}
[[nodiscard]] consteval std::size_t mutation_mode_count() noexcept {
    return std::meta::enumerators_of(^^MutationMode).size();
}
[[nodiscard]] consteval std::size_t reentrancy_mode_count() noexcept {
    return std::meta::enumerators_of(^^ReentrancyMode).size();
}

static_assert(usage_mode_count()      == 4,
    "UsageMode enumerator count drift — every consumer with switch "
    "arms on UsageMode must add the new arm.");
static_assert(sec_level_count()       == 5,
    "SecLevel enumerator count drift.");
static_assert(repr_kind_count()       == 5,
    "ReprKind enumerator count drift.");
static_assert(overflow_mode_count()   == 4,
    "OverflowMode enumerator count drift.");
static_assert(mutation_mode_count()   == 4,
    "MutationMode enumerator count drift.");
static_assert(reentrancy_mode_count() == 3,
    "ReentrancyMode enumerator count drift.");

// ── Tier-classification cross-check ───────────────────────────────
//
// Every dimension that Fn aggregates must classify under one of
// DimensionTraits.h's 5 Tier families.  This assert pins the
// vocabulary from Fn.h to the dispatch from DimensionTraits.h —
// a future change to either side that breaks the linkage gets
// caught at compile time.

static_assert(tier_of_axis(DimensionAxis::Type)           == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Refinement)     == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Usage)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Effect)         == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Security)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Protocol)       == TierKind::Typestate);
static_assert(tier_of_axis(DimensionAxis::Lifetime)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Provenance)     == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Trust)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Representation) == TierKind::Lattice);
static_assert(tier_of_axis(DimensionAxis::Complexity)     == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Precision)      == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Space)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Overflow)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Mutation)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Reentrancy)     == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Size)           == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Version)        == TierKind::Versioned);
static_assert(tier_of_axis(DimensionAxis::Staleness)      == TierKind::Semiring);

// ── Construction sanity ───────────────────────────────────────────
//
// Fn<int> is default-constructible (NSDMI on `value_{}` zero-inits
// per InitSafe).  It is NOT trivially default-constructible because
// the NSDMI is itself a non-trivial initializer — that is the
// correct trade: zero-initialize-by-default guarantees no uninit
// reads, at the cost of `is_trivially_default_constructible_v`.
// Move/assign remain nothrow when T's are.
static_assert(std::is_default_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_default_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_move_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_move_assignable_v<Fn<int>>);
static_assert(std::is_nothrow_copy_constructible_v<Fn<int>>);
static_assert(std::is_trivially_destructible_v<Fn<int>>);

}  // namespace detail::fn_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Exercises the type surface with non-constant args.  Catches the
// consteval/SFINAE/inline-body trap that pure static_assert tests
// miss (per feedback_algebra_runtime_smoke_test_discipline).
// Called from test/test_safety_compile.cpp.

namespace detail::fn_self_test {

inline void runtime_smoke_test() {
    // Default-grade construction + accessor read.
    Fn<int> f1{42};
    [[maybe_unused]] auto v1 = f1.value();

    // Non-trivial type construction.
    Fn<double> f2{3.14159};
    [[maybe_unused]] auto v2 = f2.value();

    // Custom-grade construction.
    Fn<float, pred::True, UsageMode::Affine, effects::Row<>,
       SecLevel::Public, proto::None, lifetime::Static,
       source::FromUser, trust::Tested, ReprKind::C> f3{1.5f};
    [[maybe_unused]] auto v3 = f3.value();

    // mint_fn factory call.
    [[maybe_unused]] auto m1 = mint_fn(7);
    [[maybe_unused]] auto m2 = mint_fn(2.71);

    // Per-axis enum value-level access.
    [[maybe_unused]] auto u  = decltype(f1)::usage_v;
    [[maybe_unused]] auto s  = decltype(f1)::security_v;
    [[maybe_unused]] auto r  = decltype(f1)::repr_v;
    [[maybe_unused]] auto o  = decltype(f1)::overflow_v;
    [[maybe_unused]] auto m  = decltype(f1)::mutation_v;
    [[maybe_unused]] auto re = decltype(f1)::reentrancy_v;
    [[maybe_unused]] auto vr = decltype(f1)::version_v;

    // Move semantics.
    Fn<int> moved = std::move(f1);
    [[maybe_unused]] auto vm = moved.value();
}

}  // namespace detail::fn_self_test

}  // namespace crucible::safety::fn
