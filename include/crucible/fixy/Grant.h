#pragma once

// ── crucible::fixy — Grant.h (FIXY-A3a + FIXY-A-PLUS-1/2) ─────────────
//
// The explicit-grant tag library — empty-state phantom tag types that
// declare a fixy binding's relaxation choices on each dim.  Every tag
// in this header is a ZERO-STATE final struct inheriting the
// `grant_base` marker; the marker base + `final` keyword TOGETHER
// close the inheritance-bypass cheat (FIXY-A7 #2 / FIXY-A-PLUS-1) at
// the substrate level via safety/NotInherited.h.
//
// **Phase A scope.**  The minimal catalog needed for the FIXY-A5/A6/A7
// fixtures.  The broader relaxation surface (lifetime regions, stance
// composition helpers) lands in Phase B alongside the production Fn
// aggregator.  For Phase A, every dim is reachable via at least
// `accept_default_strict_for<D>` — the universal acknowledgement tag.
//
// ── Two grant kinds ───────────────────────────────────────────────────
//
//   (1) **Explicit-accept** — `accept_default_strict_for<D>`.  The
//       author engaged dim D and chose the substrate's strict default
//       (matched against safety::fn::Fn defaults — see
//       fixy/Default.h's no-skew gate).
//
//   (2) **Relaxation tags** — `grant::copy`, `grant::with<...>`, etc.
//       The author engaged dim D and chose a non-strict grade.
//
// Both kinds expose the same surface:
//
//   * `static constexpr dim::DimAxis relaxes = D;`
//   * inherits `grant_base` (marker for IsGrantTag<>)
//   * `final` (closes the inheritance-bypass cheat)
//
// ── Inheritance-bypass closure (A1) ───────────────────────────────────
//
// Phase A's cheat probe #2 documented `struct fake :
// accept_default_strict_for<dim::Usage> {};` as an architectural
// limit.  FIXY-A-PLUS-1 closes the door:
//
//   * accept_default_strict_for<D> is `final`.  Derivation is a
//     compile error at the inheritance point with a clear GCC
//     diagnostic ("cannot derive from 'final' base class").
//   * Every relaxation tag is `final`.  Same closure.
//   * IsGrantTag<T> requires std::derived_from<T, grant_base>.  This
//     rejects random user structs that forge a `relaxes` field
//     without legitimate substrate provenance.  Authors who DO want
//     to extend the catalog inherit grant_base directly (legitimate
//     open extension).
//
// Net: the lookalike-via-inheritance attack is structurally blocked;
// legitimate downstream-extension is preserved.
//
// ── Sanitize remap (A2) ───────────────────────────────────────────────
//
// `grant::sanitize<TaintClass>` was originally mapped to dim::Trust,
// but FX §6 treats sanitization as PROVENANCE-driven (FromUser →
// Sanitized).  FIXY-A-PLUS-2 remaps to dim::Provenance.  A companion
// tag `grant::trust_assumed_for<TaintClass>` engages dim::Trust for
// the rare standalone "I assert trust without a sanitizer" case.
//
// ── Empty-with rejection (A4) ─────────────────────────────────────────
//
// `grant::with<>` (zero effects) is structurally degenerate — equivalent
// to engaging dim::Effect with the empty row, which IS the strict
// default.  Authors should use `accept_default_strict_for<dim::Effect>`
// for that case.  A `static_assert(sizeof...(Es) > 0)` in the body
// makes the intent explicit.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — every tag is an empty struct + empty marker base; no
//                member to leave uninitialized.
//   TypeSafe   — `relaxes` is dim::DimAxis (strong enum); inheritance
//                from grant_base is structural identity.
//   NullSafe   — zero-state; no pointers.
//   MemSafe    — zero-state; no resource.
//   BorrowSafe — pure metadata.
//   ThreadSafe — pure compile-time material.
//   LeakSafe   — zero-state.
//   DetSafe    — bit-identical across compiles.
//
// ── Runtime cost ──────────────────────────────────────────────────────
//
// Zero.  Empty derived-from-empty stays at sizeof == 1 (smallest
// legal C++ object) and is EBO-collapsible to 0 bytes when embedded
// in `Fn<...>` or a tuple alongside non-empty members.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §3.1        — grant tag pattern + stance
//   misc/fixy.md §24.1                  — per-dim relaxation catalog
//   safety/NotInherited.h               — concept witness + FinalBy CRTP
//   fixy/Dim.h                          — dim::DimAxis identity layer
//   fixy/Reject.h                       — IsGrantTag concept consumer

#include <crucible/effects/Capabilities.h>
#include <crucible/fixy/Dim.h>
#include <crucible/safety/NotInherited.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── grant_base — marker for IsGrantTag discrimination ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// Empty marker base.  Every shipped grant tag inherits this; user-side
// extensions inherit this to opt in as legitimate grants.  IsGrantTag
// gates on derivation, which prevents random user structs from
// satisfying engagement via accidental `relaxes` member.
//
// NOT marked final — extension via derivation IS the intended path
// for downstream grant-catalog growth.  The cheat-bypass closure
// works because every SHIPPED grant tag IS final; a lookalike
// attempting to derive from a shipped tag hits `final` at the
// inheritance site.  An author who needs a new grant tag inherits
// grant_base DIRECTLY (legitimate; opt-in).

struct grant_base {};

// ═════════════════════════════════════════════════════════════════════
// ── Universal acknowledgement: accept_default_strict_for<D> ────────
// ═════════════════════════════════════════════════════════════════════
//
// One template covers all 20 dims via non-type template parameter.
// `final` closes the inheritance-bypass cheat (FIXY-A-PLUS-1).
// EBO-collapsible to 0 bytes when embedded; standalone sizeof == 1.

template <dim::DimAxis D>
struct accept_default_strict_for final : grant_base {
    static constexpr dim::DimAxis relaxes = D;
    static constexpr bool is_explicit_accept = true;
};

static_assert(std::is_empty_v<accept_default_strict_for<dim::Type>>);
static_assert(::crucible::safety::NotInherited<accept_default_strict_for<dim::Type>>,
    "accept_default_strict_for<D> must be `final` to close the inheritance-"
    "bypass cheat (FIXY-A7 #2 / FIXY-A-PLUS-1).");

// ═════════════════════════════════════════════════════════════════════
// ── Relaxation tags — minimal Phase A catalog ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace grant {

// ── Type (dim::Type) ──────────────────────────────────────────────────
//
// `typed<T>` — declares the binding's value type explicitly.
// `final` + grant_base discipline.
template <typename T>
struct typed final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Type;
    using type = T;
};

// ── Refinement (dim::Refinement) ─────────────────────────────────────
template <typename Pred>
struct refined_with final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Refinement;
    using predicate = Pred;
};

// ── Usage (dim::Usage) ────────────────────────────────────────────────
//
// Renamed `capability` → `capability_usage` (D2 / FIXY-A-PLUS-2) to
// disambiguate from `effects::cap::Capability` permission tokens.
struct affine           final : grant_base { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct copy             final : grant_base { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct ghost            final : grant_base { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct borrow           final : grant_base { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct capability_usage final : grant_base { static constexpr dim::DimAxis relaxes = dim::Usage; };

// ── Effect (dim::Effect) ──────────────────────────────────────────────
//
// `with<Effects...>` opens a multi-effect row.  Engages dim::Effect.
// **A4 fix**: the empty pack `with<>` is structurally degenerate
// (engages Effect with the empty row, which IS the strict default).
// The static_assert below forces authors to use
// `accept_default_strict_for<dim::Effect>` for the empty-row case.
template <::crucible::effects::Effect... Es>
struct with final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Effect;
    static_assert(sizeof...(Es) > 0,
        "grant::with<> with empty effect pack is structurally degenerate "
        "(engages Effect with the empty row, which IS the strict default). "
        "Use accept_default_strict_for<dim::Effect> to make the intent "
        "explicit at the callsite.");
};

using with_alloc = with<::crucible::effects::Effect::Alloc>;
using with_io    = with<::crucible::effects::Effect::IO>;
using with_bg    = with<::crucible::effects::Effect::Bg>;
using with_block = with<::crucible::effects::Effect::Block>;
using with_init  = with<::crucible::effects::Effect::Init>;
using with_test  = with<::crucible::effects::Effect::Test>;

// ── Security (dim::Security) ──────────────────────────────────────────
template <typename Policy>
struct declassify final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Security;
    using policy = Policy;
};

struct upgrade_to_secret final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Security;
};

// ── Protocol (dim::Protocol) ─────────────────────────────────────────
template <typename Proto>
struct protocol_session final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Protocol;
    using protocol = Proto;
};

// ── Lifetime (dim::Lifetime) ─────────────────────────────────────────
template <auto RegionTag>
struct lifetime_region final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Lifetime;
};

// ── Provenance (dim::Provenance) ─────────────────────────────────────
//
// **A2 fix**: `sanitize<TaintClass>` is now mapped to dim::Provenance
// (was incorrectly dim::Trust pre-A-PLUS-2).  Sanitization is a
// provenance-tag change (FromUser → Sanitized).  Per FX §6.
template <typename SourceTag>
struct from_source final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Provenance;
    using source_tag = SourceTag;
};

template <typename TaintClass>
struct sanitize final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Provenance;
    using taint_class = TaintClass;
};

// ── Trust (dim::Trust) ────────────────────────────────────────────────
//
// `trust_assumed<auto Rationale>` — relaxes Trust from Verified to
// Unverified at the binding level with an embedded rationale literal.
// `trust_assumed_for<TaintClass>` — companion to sanitize<TaintClass>
// for the rare standalone trust-assumed case (sanitize engages
// Provenance; trust_assumed_for engages Trust).
template <auto Rationale>
struct trust_assumed final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Trust;
};

template <typename TaintClass>
struct trust_assumed_for final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Trust;
    using taint_class = TaintClass;
};

// ── Representation (dim::Representation) ─────────────────────────────
struct repr_c       final : grant_base { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_packed  final : grant_base { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_aligned final : grant_base { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_simd    final : grant_base { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_atomic  final : grant_base { static constexpr dim::DimAxis relaxes = dim::Representation; };

template <typename V>
struct vendor final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    using vendor_tag = V;
};

template <typename RecipeTier>
struct tier final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    using recipe_tier = RecipeTier;
};

// ── Observability (dim::Observability) ───────────────────────────────
struct observability_visible final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Observability;
};

// ── Complexity (dim::Complexity) ─────────────────────────────────────
struct complexity_constant  final : grant_base { static constexpr dim::DimAxis relaxes = dim::Complexity; };
struct complexity_unbounded final : grant_base { static constexpr dim::DimAxis relaxes = dim::Complexity; };

template <auto N>
struct complexity_linear final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Complexity;
};

template <auto N>
struct complexity_quadratic final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Complexity;
};

// ── Precision (dim::Precision) ───────────────────────────────────────
struct precision_f32 final : grant_base { static constexpr dim::DimAxis relaxes = dim::Precision; };
struct precision_f64 final : grant_base { static constexpr dim::DimAxis relaxes = dim::Precision; };
struct reassociate   final : grant_base { static constexpr dim::DimAxis relaxes = dim::Precision; };

template <auto Bound>
struct precision_higham final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Precision;
};

// ── Space (dim::Space) ───────────────────────────────────────────────
struct space_unbounded final : grant_base { static constexpr dim::DimAxis relaxes = dim::Space; };

template <auto N>
struct space_bounded final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Space;
};

// ── Overflow (dim::Overflow) ─────────────────────────────────────────
struct overflow_wrap     final : grant_base { static constexpr dim::DimAxis relaxes = dim::Overflow; };
struct overflow_saturate final : grant_base { static constexpr dim::DimAxis relaxes = dim::Overflow; };
struct overflow_widen    final : grant_base { static constexpr dim::DimAxis relaxes = dim::Overflow; };

// ── Mutation (dim::Mutation) ─────────────────────────────────────────
struct mutable_in_place  final : grant_base { static constexpr dim::DimAxis relaxes = dim::Mutation; };
struct append_only       final : grant_base { static constexpr dim::DimAxis relaxes = dim::Mutation; };
struct monotonic_advance final : grant_base { static constexpr dim::DimAxis relaxes = dim::Mutation; };

// ── Reentrancy (dim::Reentrancy) ─────────────────────────────────────
struct reentrant final : grant_base { static constexpr dim::DimAxis relaxes = dim::Reentrancy; };
struct coroutine final : grant_base { static constexpr dim::DimAxis relaxes = dim::Reentrancy; };

// ── Size (dim::Size) ─────────────────────────────────────────────────
struct productive final : grant_base { static constexpr dim::DimAxis relaxes = dim::Size; };

template <auto Depth>
struct sized final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Size;
};

// ── Version (dim::Version) ───────────────────────────────────────────
template <std::uint32_t V>
struct version final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Version;
    static constexpr std::uint32_t value = V;
};

// ── Staleness (dim::Staleness) ───────────────────────────────────────
template <auto TauMax>
struct stale_to final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Staleness;
};

}  // namespace grant

// ═════════════════════════════════════════════════════════════════════
// ── IsGrantTag concept — tight discrimination of fixy grants ───────
// ═════════════════════════════════════════════════════════════════════
//
// **A5 tighten**: was `requires { T::relaxes; }` which any random
// struct could satisfy.  Now requires std::derived_from<T, grant_base>
// AS WELL AS the relaxes member of correct type.  Combined with the
// `final` discipline above:
//
//   - Random user struct with `relaxes` field but NOT derived from
//     grant_base → rejected by IsGrantTag.
//   - User struct derived from grant_base → accepted (legitimate
//     extension path).
//   - User struct derived from an existing FINAL grant tag → compile
//     error at the inheritance site (cannot derive from final).

template <typename T>
concept IsGrantTag =
    std::derived_from<std::remove_cvref_t<T>, grant_base> &&
    requires {
        { std::remove_cvref_t<T>::relaxes } -> std::convertible_to<dim::DimAxis>;
    };

// ── Sanity self-tests ────────────────────────────────────────────────
namespace detail {

static_assert(IsGrantTag<grant::copy>);
static_assert(IsGrantTag<accept_default_strict_for<dim::Type>>);
static_assert(IsGrantTag<grant::with_io>);
static_assert(!IsGrantTag<int>);
static_assert(!IsGrantTag<grant_base>);  // marker base alone has no relaxes

// EBO-collapse pin
static_assert(sizeof(grant::copy) == 1);
static_assert(sizeof(accept_default_strict_for<dim::Type>) == 1);

// NotInherited witness for every shipped grant kind
static_assert(::crucible::safety::NotInherited<grant::copy>);
static_assert(::crucible::safety::NotInherited<grant::affine>);
static_assert(::crucible::safety::NotInherited<grant::with<::crucible::effects::Effect::IO>>);
static_assert(::crucible::safety::NotInherited<grant::declassify<int>>);

}  // namespace detail

}  // namespace crucible::fixy
