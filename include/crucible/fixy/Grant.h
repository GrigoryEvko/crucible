#pragma once

// ── crucible::fixy — Grant.h (FIXY-A3a) ────────────────────────────────
//
// The explicit-grant tag library — empty-state phantom tag types that
// declare a fixy binding's relaxation choices on each dim.  Every tag
// in this header is a ZERO-STATE struct carrying a single `relaxes`
// member that names which `fixy::dim::DimAxis` the tag engages.  The
// fixy/Reject.h `IsAccepted` concept reads the `relaxes` member of
// every tag in the `Grants...` pack to decide engagement on a per-dim
// basis.
//
// **Phase A scope.**  The minimal catalog needed to write every
// positive-compile / neg-compile / cheat-probe fixture in FIXY-A5,
// A6, A7.  The broader relaxation surface (15+ additional tags
// covering Lifetime / Provenance / Mutation / Reentrancy / Size /
// Version / Representation / Complexity / Space) lands in Phase B
// alongside the production Fn aggregator.  For Phase A, every dim
// MUST be reachable via at least `accept_default_strict_for<D>` —
// that is the universal acknowledgement tag.
//
// **Two grant kinds.**
//
//   (1) **Explicit-accept tags** — `accept_default_strict_for<D>`.
//       The author has read the discipline AND chose the substrate's
//       strict default for dim D.  Engagement: explicit.  Result: D
//       composes with the strict default from fixy/Default.h.
//
//   (2) **Relaxation tags** — `grant::copy`, `grant::with<...>`,
//       `grant::declassify<Policy>`, etc.  The author has read the
//       discipline AND chose a non-strict grade for dim D, with the
//       rationale embedded in the tag type's name (or template arg).
//       Engagement: explicit.  Result: D composes with the relaxed
//       grade (Phase B Fn aggregator maps the tag to the substrate
//       Fn template arg).
//
// Both kinds expose the same surface:
//
//   `static constexpr dim::DimAxis relaxes = D;`
//
// IsAccepted reads only this field.  Engagement is "any tag in the
// Grants pack with `relaxes == D` counts D as engaged."  See
// fixy/Reject.h for the conjunction.
//
// **Cheat resistance.**  The engagement check is per-EXACT-type, not
// inheritance-based — see fixy/Reject.h FIXY-A7 cheat probe.  A user
// who inherits from `accept_default_strict_for<dim::Usage>` does NOT
// thereby engage dim::Usage in a `Grants...` pack containing the
// derived type, because the IsAccepted concept tests `T::relaxes`
// per-tag-instance, not subobject membership.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — every tag is an empty struct; no member to leave
//                uninitialized.
//   TypeSafe   — `relaxes` is `safety::DimensionAxis` (strong enum);
//                cross-dim confusion is a compile-time mismatch.
//   NullSafe   — zero-state; no pointers.
//   MemSafe    — zero-state; no resource.
//   BorrowSafe — pure metadata.
//   ThreadSafe — pure compile-time material.
//   LeakSafe   — zero-state.
//   DetSafe    — bit-identical across compiles.
//
// ── Runtime cost ──────────────────────────────────────────────────────
//
// Zero.  Every tag has `sizeof == 1` (smallest legal C++ object) but
// is empty-base-optimizable to 0 bytes when embedded in `Fn<...>` or
// when held in a tuple alongside non-empty members.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §3.1        — grant tag pattern + stance
//   misc/fixy.md §24.1                  — per-dim relaxation catalog
//   fixy/Dim.h                          — dim::DimAxis identity layer
//   fixy/Reject.h                       — IsAccepted concept consumer

#include <crucible/effects/Capabilities.h>
#include <crucible/fixy/Dim.h>

#include <type_traits>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── Universal acknowledgement: accept_default_strict_for<D> ────────
// ═════════════════════════════════════════════════════════════════════
//
// One tag covers all 20 dims via a non-type template parameter.  The
// member `relaxes == D` so the engagement check can match.  This is
// the load-bearing grant kind: most fixy bindings will reach for it
// on 14-19 of the 20 dims and use a relaxation tag for the remaining
// 1-6.  Empty struct (sizeof == 1, EBO-collapsible to 0).

template <dim::DimAxis D>
struct accept_default_strict_for {
    static constexpr dim::DimAxis relaxes = D;
    static constexpr bool is_explicit_accept = true;
};

static_assert(std::is_empty_v<accept_default_strict_for<dim::Type>>,
    "accept_default_strict_for must be zero-state for EBO collapse.");

// ═════════════════════════════════════════════════════════════════════
// ── Relaxation tags — minimal Phase A catalog ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace grant {

// ── Type (dim::Type) ──────────────────────────────────────────────────
//
// `typed<T>` — declares the binding's value type explicitly.  Without
// this OR `accept_default_strict_for<dim::Type>`, fixy refuses to
// invent a type for the author.
template <typename T>
struct typed {
    static constexpr dim::DimAxis relaxes = dim::Type;
    using type = T;
};

// ── Refinement (dim::Refinement) ─────────────────────────────────────
//
// `refined_with<Pred>` — engages dim::Refinement with a non-True
// predicate.  Pred must satisfy the Fn::pred convention (static
// `check(T const&) noexcept -> bool`).  No runtime cost.
template <typename Pred>
struct refined_with {
    static constexpr dim::DimAxis relaxes = dim::Refinement;
    using predicate = Pred;
};

// ── Usage (dim::Usage) ────────────────────────────────────────────────
struct affine     { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct copy       { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct ghost      { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct borrow     { static constexpr dim::DimAxis relaxes = dim::Usage; };
struct capability { static constexpr dim::DimAxis relaxes = dim::Usage; };

// ── Effect (dim::Effect) ──────────────────────────────────────────────
//
// `with<Effects...>` opens a multi-effect row.  Engages dim::Effect.
// The variadic effect pack admits the empty case which is degenerate
// (engages Effect with an empty row, i.e. the strict default Tot) —
// the author should use accept_default_strict_for<dim::Effect> for
// the empty-row case to make intent explicit at the callsite.
template <::crucible::effects::Effect... Es>
struct with {
    static constexpr dim::DimAxis relaxes = dim::Effect;
};

using with_alloc = with<::crucible::effects::Effect::Alloc>;
using with_io    = with<::crucible::effects::Effect::IO>;
using with_bg    = with<::crucible::effects::Effect::Bg>;
using with_block = with<::crucible::effects::Effect::Block>;
using with_init  = with<::crucible::effects::Effect::Init>;
using with_test  = with<::crucible::effects::Effect::Test>;

// ── Security (dim::Security) ──────────────────────────────────────────
//
// `declassify<Policy>` — relaxes Security from Classified down to
// Public (or Unclassified) via a named declassification policy.  The
// Policy type is the audit trail: review reads `Policy` and checks
// the documented justification matches the call site.
template <typename Policy>
struct declassify {
    static constexpr dim::DimAxis relaxes = dim::Security;
    using policy = Policy;
};

struct upgrade_to_secret {
    static constexpr dim::DimAxis relaxes = dim::Security;
};

// ── Protocol (dim::Protocol) ─────────────────────────────────────────
//
// `protocol_session<Proto>` — binds the function to a session-typed
// protocol.  Phase B will resolve Proto into the substrate's
// sessions/Session.h type machinery; for Phase A engagement is
// sufficient.
template <typename Proto>
struct protocol_session {
    static constexpr dim::DimAxis relaxes = dim::Protocol;
    using protocol = Proto;
};

// ── Lifetime (dim::Lifetime) ─────────────────────────────────────────
//
// `lifetime_region<auto Tag>` — binds the value's lifetime to a named
// region.  The auto-typed template arg admits literal-typed tags
// (e.g., string literal NTTPs) so the audit trail is in the type.
template <auto RegionTag>
struct lifetime_region {
    static constexpr dim::DimAxis relaxes = dim::Lifetime;
};

// ── Provenance (dim::Provenance) ─────────────────────────────────────
//
// `from_source<SourceTag>` — switches Provenance from FromInternal
// (substrate strict default) to an explicit source.  Common sources
// include FromUser / FromExternal / FromDb / Sanitized — see
// safety/Tagged.h source::* family.
template <typename SourceTag>
struct from_source {
    static constexpr dim::DimAxis relaxes = dim::Provenance;
    using source_tag = SourceTag;
};

// ── Trust (dim::Trust) ────────────────────────────────────────────────
//
// `trust_assumed<auto Rationale>` — relaxes Trust from Verified to
// Unverified at the binding level with an embedded rationale literal
// for audit.  `sanitize<TaintClass>` re-elevates trust by declaring
// a sanitization stage.
template <auto Rationale>
struct trust_assumed {
    static constexpr dim::DimAxis relaxes = dim::Trust;
};

template <typename TaintClass>
struct sanitize {
    static constexpr dim::DimAxis relaxes = dim::Trust;
    using taint_class = TaintClass;
};

// ── Representation (dim::Representation) ─────────────────────────────
//
// Substrate `ReprKind` has 6 values (Opaque/C/Packed/Aligned/Simd/
// Atomic).  Phase A ships one tag per non-default value.
struct repr_c       { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_packed  { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_aligned { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_simd    { static constexpr dim::DimAxis relaxes = dim::Representation; };
struct repr_atomic  { static constexpr dim::DimAxis relaxes = dim::Representation; };

// `vendor<V>` — sub-classification of Representation: pin Mimic
// backend by vendor (NV / AM / Intel / ...).  The V parameter is the
// vendor-tag type (e.g., `mimic::nv::Vendor`).  Engages Representation
// (Phase B may split into a dedicated Vendor dim if needed).
template <typename V>
struct vendor {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    using vendor_tag = V;
};

// `tier<RecipeTier>` — pin NumericalRecipe tier (BITEXACT_STRICT /
// BITEXACT_TC / ORDERED / UNORDERED).  Engages Representation; Phase
// B's Forge integration may route through a dedicated NumericalTier
// dim if subsequent design adds one.
template <typename RecipeTier>
struct tier {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    using recipe_tier = RecipeTier;
};

// ── Observability (dim::Observability) ───────────────────────────────
//
// Observability is DERIVED from EffectRow at the consumer site, so
// engaging dim::Effect with `with<...>` implicitly engages
// Observability.  But Reject.h's IsAccepted requires explicit
// per-dim engagement — so the author writes one of:
//   (a) accept_default_strict_for<dim::Observability>
//   (b) `observability_visible` (declare observable side-effects)
struct observability_visible {
    static constexpr dim::DimAxis relaxes = dim::Observability;
};

// ── Complexity (dim::Complexity) ─────────────────────────────────────
struct complexity_constant  { static constexpr dim::DimAxis relaxes = dim::Complexity; };
struct complexity_unbounded { static constexpr dim::DimAxis relaxes = dim::Complexity; };

template <auto N>
struct complexity_linear {
    static constexpr dim::DimAxis relaxes = dim::Complexity;
};

template <auto N>
struct complexity_quadratic {
    static constexpr dim::DimAxis relaxes = dim::Complexity;
};

// ── Precision (dim::Precision) ───────────────────────────────────────
struct precision_f32 { static constexpr dim::DimAxis relaxes = dim::Precision; };
struct precision_f64 { static constexpr dim::DimAxis relaxes = dim::Precision; };
struct reassociate   { static constexpr dim::DimAxis relaxes = dim::Precision; };

template <auto Bound>
struct precision_higham {
    static constexpr dim::DimAxis relaxes = dim::Precision;
};

// ── Space (dim::Space) ───────────────────────────────────────────────
struct space_unbounded { static constexpr dim::DimAxis relaxes = dim::Space; };

template <auto N>
struct space_bounded {
    static constexpr dim::DimAxis relaxes = dim::Space;
};

// ── Overflow (dim::Overflow) ─────────────────────────────────────────
struct overflow_wrap     { static constexpr dim::DimAxis relaxes = dim::Overflow; };
struct overflow_saturate { static constexpr dim::DimAxis relaxes = dim::Overflow; };
struct overflow_widen    { static constexpr dim::DimAxis relaxes = dim::Overflow; };

// ── Mutation (dim::Mutation) ─────────────────────────────────────────
struct mutable_in_place   { static constexpr dim::DimAxis relaxes = dim::Mutation; };
struct append_only        { static constexpr dim::DimAxis relaxes = dim::Mutation; };
struct monotonic_advance  { static constexpr dim::DimAxis relaxes = dim::Mutation; };

// ── Reentrancy (dim::Reentrancy) ─────────────────────────────────────
struct reentrant { static constexpr dim::DimAxis relaxes = dim::Reentrancy; };
struct coroutine { static constexpr dim::DimAxis relaxes = dim::Reentrancy; };

// ── Size (dim::Size) ─────────────────────────────────────────────────
struct productive { static constexpr dim::DimAxis relaxes = dim::Size; };

template <auto Depth>
struct sized {
    static constexpr dim::DimAxis relaxes = dim::Size;
};

// ── Version (dim::Version) ───────────────────────────────────────────
template <std::uint32_t V>
struct version {
    static constexpr dim::DimAxis relaxes = dim::Version;
    static constexpr std::uint32_t value = V;
};

// ── Staleness (dim::Staleness) ───────────────────────────────────────
template <auto TauMax>
struct stale_to {
    static constexpr dim::DimAxis relaxes = dim::Staleness;
};

}  // namespace grant

// ═════════════════════════════════════════════════════════════════════
// ── Grant concept — structural detection of any fixy grant tag ─────
// ═════════════════════════════════════════════════════════════════════
//
// `IsGrantTag<T>` is true iff T exposes a `static constexpr
// dim::DimAxis relaxes` member of the substrate enum type.  This is
// used by Reject.h to validate that every entry in a `Grants...` pack
// is a real grant tag (not arbitrary garbage), and by examples /
// docs to inspect packs at compile time.

template <typename T>
concept IsGrantTag = requires {
    { T::relaxes } -> std::convertible_to<dim::DimAxis>;
};

}  // namespace crucible::fixy
