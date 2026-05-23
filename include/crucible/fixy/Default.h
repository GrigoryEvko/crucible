#pragma once

// ── crucible::fixy — Default.h — per-dim strict defaults ───────────
//
// Clean reimplementation per misc/16_05_2026_fixy.md §4.
//
// THIS HEADER MUST NOT DEFINE STRICT DEFAULTS LOCALLY.  The 20-axis
// strict defaults are owned by `safety/Fn.h::Fn<Type, ...>`'s template
// parameter defaults (P0-1, shipped via #1095).  This header is a
// per-dim projection that maps `DimensionAxis::X` → the default value
// or type used by `Fn<Type, ...>` on that axis.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::fn::Fn<int>           — the canonical default instantiation
//
// (FIXY-V-006 — all "(DimensionAxis::Name = N)" parentheticals below
// quote the SUBSTRATE enumerator's underlying value from
// safety/DimensionTraits.h, not the historical FX dim spec numbering.
// FX dim 12 (Clock Domain) and FX dim 17 (FP Order) were dropped per
// fixy.md §24.1, so the substrate ordering compacts 0..23 without gaps;
// see `scripts/check-fixy-dim-prose.sh` for the CI grep guard that
// rejects re-introduction of "(dim NN)" FX-only spellings.)
//
//   safety::fn::pred::True        — Refinement (DimensionAxis::Refinement = 1) strict default
//   safety::fn::UsageMode::Linear — Usage (DimensionAxis::Usage = 2) strict default
//   safety::effects::Row<>        — Effect (DimensionAxis::Effect = 3) strict default
//   safety::fn::SecLevel::Classified — Security (DimensionAxis::Security = 4) strict default
//   safety::fn::proto::None       — Protocol (DimensionAxis::Protocol = 5) strict default
//   safety::fn::lifetime::Static  — Lifetime (DimensionAxis::Lifetime = 6) strict default
//   safety::source::FromInternal  — Provenance (DimensionAxis::Provenance = 7) strict default
//   safety::trust::Verified       — Trust (DimensionAxis::Trust = 8) strict default
//   safety::fn::ReprKind::Opaque  — Representation (DimensionAxis::Representation = 9) strict default
//   safety::fn::cost::Unstated    — Complexity (DimensionAxis::Complexity = 11) strict default
//   safety::fn::precision::Exact  — Precision (DimensionAxis::Precision = 12) strict default
//   safety::fn::space::Zero       — Space (DimensionAxis::Space = 13) strict default
//   safety::fn::OverflowMode::Trap — Overflow (DimensionAxis::Overflow = 14) strict default
//   safety::fn::MutationMode::Immutable — Mutation (DimensionAxis::Mutation = 15) strict default
//   safety::fn::ReentrancyMode::NonReentrant — Reentrancy (DimensionAxis::Reentrancy = 16)
//   safety::fn::size_pol::Unstated — Size (DimensionAxis::Size = 17) strict default
//   safety::fn::stale::Fresh      — Staleness (DimensionAxis::Staleness = 19) strict default
//   safety::fn::sync::Unconstrained — Synchronization (DimensionAxis::Synchronization = 20) strict default
//   safety::fn::regime::Unconstrained — Regime (DimensionAxis::Regime = 21) strict default
//
// 25 of 27 axes carry a strict default at the substrate level
// (refreshed by FIXY-U-134 + FIXY-V-088/097 + FIXY-V-238 to track the
// Synchronization / Regime / FpMode / SyscallSurface / ControlFlow /
// CallShape / StackUse / GlobalState / Stdio extensions).  Two axes are
// caller-supplied:
//
//   DimensionAxis::Type      — there is no "default function type";
//                              every binding names its own type.
//   DimensionAxis::Version   — Fn<>'s u32 default is 1, which IS the
//                              strict default; we project it as a
//                              value here.
//
// (Observability — DimensionAxis::Observability = 10 in the substrate
//  (FX dim 11) — is DERIVED from the EffectRow per fixy.md §24.1; it
//  has no slot in the substrate's Fn<...> template pack, so it does
//  not appear in `strict_default_for` either.)
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Every `strict_default_for<D>` specialization aliases an
// existing substrate name.  If a future P-row adds a 21st dimension
// to safety::DimensionAxis, this header MUST grow one new
// specialization that aliases the substrate's new default.
//
// ── Why this header exists at all ──────────────────────────────────
//
// `fixy/Fn.h`'s `detail::resolve` namespace needs a per-dim
// metafunction that maps `DimensionAxis::X` to "the value or type
// that `Fn<>` would carry on axis X if the user wrote
// `Grant::accept_default_strict_for<X>`".  Without this projection,
// the resolver would need to switch on DimensionAxis at every per-
// axis slot — bloat that defeats the "one source of truth"
// discipline.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — every specialization aliases a substrate type or value;
//              no parallel definitions to drift.
//   InitSafe — no fields, no state.
//   DetSafe  — every member is constexpr.
//
// ── Runtime cost ───────────────────────────────────────────────────
//
// Zero.  All members are constexpr; consumers (`fixy/Fn.h`'s
// `detail::resolve` namespace) compile to the substrate's default
// values directly.
//
// ── Self-test ──────────────────────────────────────────────────────
//
// Two assertions ride this header:
//   1. Every DimensionAxis enumerator must have a strict_default_for
//      specialization (reflection-driven coverage).
//   2. The aliased values + types must round-trip against
//      safety::fn::Fn<int>'s defaults (so when the substrate's
//      defaults change, this header fires a clear error rather than
//      silently drifting).

#include <crucible/fixy/Dim.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Tagged.h>
#include <crucible/effects/EffectRow.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::fixy {

// ─── strict_default_for<D> — primary template (intentionally empty) ─
//
// Per-dim specializations below.  An un-specialized invocation fires
// `value_or_type` as a substitution failure, surfaced by the
// reflection-driven coverage check at the bottom of this header.

template <dim::DimensionAxis D>
struct strict_default_for;

// ─── Per-dim specializations ───────────────────────────────────────
//
// Each specialization aliases the substrate's existing default; no
// independent definition.  Members:
//
//   `type`      — for type-valued axes (Refinement / Effect / etc.)
//   `value_type` — the enum type for enum-valued axes
//   `value`     — the strict default value (for enum-valued axes)
//
// A given specialization exposes either `type` OR `(value_type, value)`,
// not both, matching the substrate's axis classification.

template <>
struct strict_default_for<dim::DimensionAxis::Type> {
    // Caller-supplied; no strict default.  Engagement on this axis
    // requires the caller to name the Type explicitly via
    // `fixy::fn<Type, ...>`.  No `type` / `value` member.
    static constexpr bool caller_supplied = true;
};

template <>
struct strict_default_for<dim::DimensionAxis::Refinement> {
    using type = safety::fn::pred::True;
};

template <>
struct strict_default_for<dim::DimensionAxis::Usage> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Linear;
};

template <>
struct strict_default_for<dim::DimensionAxis::Effect> {
    using type = effects::Row<>;
};

template <>
struct strict_default_for<dim::DimensionAxis::Security> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Classified;
};

template <>
struct strict_default_for<dim::DimensionAxis::Protocol> {
    using type = safety::fn::proto::None;
};

template <>
struct strict_default_for<dim::DimensionAxis::Lifetime> {
    using type = safety::fn::lifetime::Static;
};

template <>
struct strict_default_for<dim::DimensionAxis::Provenance> {
    using type = safety::source::FromInternal;
};

template <>
struct strict_default_for<dim::DimensionAxis::Trust> {
    using type = safety::trust::Verified;
};

template <>
struct strict_default_for<dim::DimensionAxis::Representation> {
    using value_type = safety::fn::ReprKind;
    static constexpr value_type value = safety::fn::ReprKind::Opaque;
};

template <>
struct strict_default_for<dim::DimensionAxis::Observability> {
    // ── fixy-M-08: documented "half-engaged" duality ──────────────
    //
    // Observability is a DERIVED axis with a dual nature that prior
    // doc-blocks left implicit (the source of the M-08 critique):
    //
    //   HALF-1 (engagement-required): the Grants pack MUST contain
    //          `accept_default_strict_for<Observability>` for
    //          IsAccepted to fire — the engagement marker is the
    //          author's structural witness "I have considered the
    //          Observability axis."  fixy-A4-026 + Reject.h:1091
    //          pin this in the engagement walk.
    //
    //   HALF-2 (payload-derived):     the marker carries NO
    //          independent semantic content.  Observability's strict
    //          default is ALIASED from Effect's strict default —
    //          when a binding accepts the strict default for both
    //          Effect and Observability, the resolved type is the
    //          SAME (`Row<>`, the empty effect row).  There is no
    //          independent slot in the `Fn<...>` pack.
    //
    // The duality is DELIBERATE — Observability tracks the
    // observability footprint of the binding's effects (Pure / Quiet
    // / Loud), which is fully determined by the Effect row.  Forcing
    // the engagement marker prevents authors from silently inheriting
    // observability semantics they did not consider; aliasing the
    // payload prevents independent specification.
    //
    // If a future redesign promotes Observability to a stand-alone
    // axis (independent payload, e.g. an ObservabilityKind enum that
    // can disagree with Effect), the `derived_from` alias here will
    // be removed AND the engagement-required witness in Reject.h
    // will be reused for the new payload.  Do NOT silently drop the
    // engagement requirement.
    //
    // FIXY-AUDIT-A9: expose a `type` alias that pre-resolves to the
    // Effect axis's strict default.  Without it, consumers reaching
    // for `strict_default_for<Observability>::type` hit substitution
    // failure (only `derived_from` was visible).
    using derived_from = strict_default_for<dim::DimensionAxis::Effect>;
    using type         = typename derived_from::type;
};

// FIXY-AUDIT-A4: positive static_assert pinning Observability's
// derived alias to Effect's strict default.  If Effect's default ever
// drifts (the substrate adds an effect, the Row<>'s identity changes,
// etc.), this regression fires here at the fixy/Default.h definition
// site rather than as an opaque substitution failure deep inside a
// downstream resolver.
static_assert(std::is_same_v<
    typename strict_default_for<dim::DimensionAxis::Observability>::type,
    typename strict_default_for<dim::DimensionAxis::Effect>::type>,
    "Observability's derived type must round-trip to Effect's strict default.");

// fixy-M-08: structural witness for the half-engaged duality.  HALF-2
// (payload-derived) is asserted above; this assertion locks HALF-1
// (the `derived_from` alias must EXIST — its presence is the type-
// level signal that the engagement walk MUST require an explicit
// marker on this axis).  If a future redesign removes `derived_from`
// (making Observability a stand-alone axis), this static_assert
// fires and the maintainer is forced to update the engagement walk
// and the Reject.h:1091 witness in lockstep.
static_assert(
    requires { typename strict_default_for<dim::DimensionAxis::Observability>::derived_from; },
    "fixy-M-08: Observability's `derived_from` alias must exist — "
    "its presence pins the half-engaged duality (engagement-required "
    "+ payload-derived).  Removing the alias requires updating the "
    "engagement walk in Reject.h (fixy-A4-026 witness at line 1091) "
    "AND the doc-block above in lockstep.");

template <>
struct strict_default_for<dim::DimensionAxis::Complexity> {
    using type = safety::fn::cost::Unstated;
};

template <>
struct strict_default_for<dim::DimensionAxis::Precision> {
    using type = safety::fn::precision::Exact;
};

template <>
struct strict_default_for<dim::DimensionAxis::Space> {
    using type = safety::fn::space::Zero;
};

template <>
struct strict_default_for<dim::DimensionAxis::Overflow> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Trap;
};

template <>
struct strict_default_for<dim::DimensionAxis::Mutation> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Immutable;
};

template <>
struct strict_default_for<dim::DimensionAxis::Reentrancy> {
    using value_type = safety::fn::ReentrancyMode;
    static constexpr value_type value = safety::fn::ReentrancyMode::NonReentrant;
};

template <>
struct strict_default_for<dim::DimensionAxis::Size> {
    using type = safety::fn::size_pol::Unstated;
};

template <>
struct strict_default_for<dim::DimensionAxis::Version> {
    using value_type = std::uint32_t;
    static constexpr value_type value = 1u;
};

template <>
struct strict_default_for<dim::DimensionAxis::Staleness> {
    using type = safety::fn::stale::Fresh;
};

// FIXY-AUDIT-A3-008: Synchronization (DimensionAxis::Synchronization = 20,
// Crucible extension 2026-05-18) is a WRAPPER-ONLY axis — safety::Wait<Strategy, T> and
// safety::MemOrder<Tag, T> hold the sync discipline at the value site,
// not as an Fn<...> aggregator slot.  No Fn<int> alias to round-trip
// against (see `type_defaults_match_substrate` below — Synchronization
// is deliberately absent from that check, parallel to Observability).
// The strict default is `safety::fn::sync::Unconstrained` — meaning
// "the binding makes no claim about wait strategy / memory order at
// this scope; if any value flowing through is wrapped, its wrapper
// carries the discipline."
template <>
struct strict_default_for<dim::DimensionAxis::Synchronization> {
    using type = safety::fn::sync::Unconstrained;
};

// FIXY-AUDIT-A3-009: Regime (DimensionAxis::Regime = 21, Crucible
// extension 2026-05-18) is a WRAPPER-ONLY axis — safety::HotPath<Tier, T> holds the operating-
// regime tier (Hot / Warm / Cold) at the value site, not as an Fn<...>
// aggregator slot.  No Fn<int> alias to round-trip against (parallel
// to Observability and Synchronization).  The strict default is
// `safety::fn::regime::Unconstrained` — meaning "the binding makes no
// claim about operating regime at this scope; the wrapper, if any,
// lives on the value itself."  HotPath defaults to Cold per its
// substrate, so Unconstrained at the binding level means "use the
// value's wrapper grade, or Cold-by-default if unwrapped."
template <>
struct strict_default_for<dim::DimensionAxis::Regime> {
    using type = safety::fn::regime::Unconstrained;
};

// FIXY-V-088: FpMode (DimensionAxis::FpMode = 22, Crucible extension
// 2026-05-22) is a WRAPPER-ONLY axis — the FpMode taxonomy (11 sub-axes: Rounding / Ftz
// / Contract / TrapMask / Denormal / NanPolicy / InfPolicy /
// ComplexLayout / LibmPolicy / Reassociate / FpConstant; see
// algebra/lattices/FpModeLattice.h) is held at the value site by a
// forge-emitted wrapper (V-089/090/091/092/093), not as an Fn<...>
// aggregator slot.  No Fn<int> alias to round-trip against (parallel
// to Observability / Synchronization / Regime).  The strict default
// is `safety::fn::fp_mode::Unconstrained` — meaning "the binding
// makes no claim about FP-evaluation policy at this scope; if any
// value flowing through carries a wrapper, that wrapper carries the
// discipline."  Tier-S Semiring: composition is par=join (strictest-
// wins) at every cross-axis site under V-091.
template <>
struct strict_default_for<dim::DimensionAxis::FpMode> {
    using type = safety::fn::fp_mode::Unconstrained;
};

// FIXY-V-097: SyscallSurface (DimensionAxis::SyscallSurface = 23,
// Crucible extension 2026-05-22) is a WRAPPER-ONLY axis — the syscall-family taxonomy
// (`SyscallFamilyLattice` per algebra/lattices/SyscallFamilyLattice.h:
// NoSyscall / VdsoOnly / ReadOnlyState / FileMutation / MemoryMapping
// / ThreadSync / NetworkIo / ProcessControl / Privilege) is held at
// the value site by V-098+'s forge-emitted wrapper, not as an
// Fn<...> aggregator slot.  No Fn<int> alias to round-trip against
// (parallel to Observability / Synchronization / Regime / FpMode).
// The strict default is `safety::fn::syscall::Unconstrained` —
// meaning "the binding makes no claim about syscall surface at this
// scope; if any value flowing through carries a wrapper, that wrapper
// carries the surface pin."  Tier-S Semiring: composition is
// par=join along the chain NoSyscall ⊏ ... ⊏ Privilege (subset-
// inclusion on the syscall set), matching Met(X) effect-row union
// at finer granularity.
template <>
struct strict_default_for<dim::DimensionAxis::SyscallSurface> {
    using type = safety::fn::syscall::Unconstrained;
};

// FIXY-V-238: ControlFlow / CallShape / StackUse / GlobalState / Stdio
// (DimensionAxis 24-28, Crucible extension 2026-05-23) are WRAPPER-ONLY
// axes — the function-behavior taxonomy (ControlFlowLattice
// Pure ⊏ AbortOnly ⊏ ThrowOnly ⊏ MayLongjmp ⊏ MaySignal;
// CallShapeLattice Direct ⊏ BoundedRecurses<N> ⊏ Indirect ⊏ Virtual ⊏
// Unbounded; plus StackUse / GlobalState / Stdio chains) is held at the
// value site by V-239/V-240/V-241's lattices + V-242's safety::* Graded
// wrappers, NOT as an Fn<...> aggregator slot.  No Fn<int> alias to
// round-trip against (parallel to Observability / Synchronization /
// Regime / FpMode / SyscallSurface).  Each strict default is the
// matching `safety::fn::<ns>::Unconstrained` — "the binding makes no
// claim about this behavior at this scope; if any value flowing through
// carries a wrapper, that wrapper carries the discipline."  Tier-S
// Semiring: composition is par=join (strictest-wins) along each chain.
template <>
struct strict_default_for<dim::DimensionAxis::ControlFlow> {
    using type = safety::fn::control_flow::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::CallShape> {
    using type = safety::fn::call_shape::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::StackUse> {
    using type = safety::fn::stack_use::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::GlobalState> {
    using type = safety::fn::global_state::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::Stdio> {
    using type = safety::fn::stdio::Unconstrained;
};

// FIXY-V-253: HwInstruction / BarrierStrength / SimdIsa (DimensionAxis
// 29-31, Crucible extension 2026-05-23) are WRAPPER-ONLY HW axes — the
// capability taxonomy (HwInstructionLattice NoneAllowed ⊏ Scalar ⊏
// Vectorizable ⊏ NonDeterministicTsc ⊏ PrivilegedMsr; BarrierStrength
// None ⊏ .. ⊏ FullFence; SimdIsa Tier-L non-distributive x86×ARM trunk)
// is held at the value site by V-250/251/252's lattices + V-254/255/256's
// safety::* Graded wrappers, NOT as an Fn<...> aggregator slot.  Each
// strict default is the matching `safety::fn::<ns>::Unconstrained`.
// HwInstruction / BarrierStrength are Tier-S (par=join, strictest-wins);
// SimdIsa is Tier-L (the second Tier-L axis, peer to Representation).
template <>
struct strict_default_for<dim::DimensionAxis::HwInstruction> {
    using type = safety::fn::hw_instruction::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::BarrierStrength> {
    using type = safety::fn::barrier_strength::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::SimdIsa> {
    using type = safety::fn::simd_isa::Unconstrained;
};

// FIXY-V-266: MemoryScope (DimensionAxis 32, Crucible extension
// 2026-05-23) is a WRAPPER-ONLY Tier-L axis — the memory-visibility-scope
// taxonomy (MemoryScopeLattice, accel Thread⊏Warp⊏Cta⊏Cluster⊏Gpu × ARM
// Inner(ISH)⊏Outer(OSH), joined at Thread(⊥)/System(⊤), non-distributive)
// is held at the value site by V-265's lattice + V-267's safety::ScopedFence
// Graded wrapper, NOT as an Fn<...> aggregator slot.  Third Tier-L axis,
// peer to Representation + SimdIsa.
template <>
struct strict_default_for<dim::DimensionAxis::MemoryScope> {
    using type = safety::fn::memory_scope::Unconstrained;
};

// ─── has_strict_default — predicate concept ────────────────────────
//
// True iff a specialization exists for D AND it exposes either `type`
// or `(value_type, value)` (i.e., it isn't the Type-axis "caller-
// supplied" sentinel).  Used by Reject.h to decide whether
// `accept_default_strict_for<Type>` is a coherent grant (it isn't —
// callers must NAME the type, not "accept" it).

template <dim::DimensionAxis D>
concept HasStrictDefault =
    requires { typename strict_default_for<D>::type; }
    || requires {
        typename strict_default_for<D>::value_type;
        { strict_default_for<D>::value };
    };

// ─── HasDerivedDefault — Observability is the only derived axis ────
//
// True iff the dim's strict default is computed from another dim
// (Observability ← Effect).  Reject.h treats `accept_default` on a
// derived axis the same as a primary axis — it's the author saying
// "I accept whatever the upstream dim resolves to."

template <dim::DimensionAxis D>
concept HasDerivedDefault =
    requires { typename strict_default_for<D>::derived_from; };

template <dim::DimensionAxis D>
concept IsCallerSupplied =
    requires { { strict_default_for<D>::caller_supplied } -> std::convertible_to<bool>; }
    && strict_default_for<D>::caller_supplied;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — reflection-driven coverage ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Iterate every DimensionAxis enumerator, verify EXACTLY ONE of the
// three concept slots is satisfied: caller-supplied, has-strict-default,
// or has-derived-default.  An un-specialized dim fails all three and
// the assertion fires with the offending enumerator name.

namespace detail::default_coverage {

[[nodiscard]] consteval bool every_axis_resolves() noexcept {
    static constexpr auto resolve_axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : resolve_axes) {
        constexpr auto axis_v = [:en:];
        constexpr bool has_caller    = IsCallerSupplied<axis_v>;
        constexpr bool has_strict    = HasStrictDefault<axis_v>;
        constexpr bool has_derived   = HasDerivedDefault<axis_v>;
        // FIXY-AUDIT-A9: a derived axis may ALSO expose `type` (pre-
        // resolved through `derived_from`) — those are not two
        // independent engagements but a derived axis publishing its
        // upstream value for downstream consumers.  Count strict as
        // an independent slot only when no derived marker is present.
        constexpr int  strict_indep  = (has_strict && !has_derived) ? 1 : 0;
        constexpr int  match_count   = (has_caller ? 1 : 0)
                                     + strict_indep
                                     + (has_derived ? 1 : 0);
        if (match_count != 1) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Round-trip self-test: the type-valued axes' aliases must match
// safety::fn::Fn<int>'s default member types.  If the substrate
// changes (e.g., Fn<>'s Refinement default becomes something other
// than pred::True), this fires the regression at fixy/Default.h
// rather than at an opaque resolver substitution failure inside
// `fixy/Fn.h`'s `detail::resolve` namespace.

[[nodiscard]] consteval bool type_defaults_match_substrate() noexcept {
    using DF = safety::fn::Fn<int>;
    return  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Refinement>::type, DF::refinement_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Effect>::type,     DF::effect_row_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Protocol>::type,   DF::protocol_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Lifetime>::type,   DF::lifetime_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Provenance>::type, DF::source_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Trust>::type,      DF::trust_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Complexity>::type, DF::cost_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Precision>::type,  DF::precision_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Space>::type,      DF::space_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Size>::type,       DF::size_t_>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Staleness>::type,  DF::staleness_t>;
}

[[nodiscard]] consteval bool enum_defaults_match_substrate() noexcept {
    using DF = safety::fn::Fn<int>;
    return  strict_default_for<dim::DimensionAxis::Usage>::value          == DF::usage_v
        &&  strict_default_for<dim::DimensionAxis::Security>::value       == DF::security_v
        &&  strict_default_for<dim::DimensionAxis::Representation>::value == DF::repr_v
        &&  strict_default_for<dim::DimensionAxis::Overflow>::value       == DF::overflow_v
        &&  strict_default_for<dim::DimensionAxis::Mutation>::value       == DF::mutation_v
        &&  strict_default_for<dim::DimensionAxis::Reentrancy>::value     == DF::reentrancy_v
        &&  strict_default_for<dim::DimensionAxis::Version>::value        == DF::version_v;
}

}  // namespace detail::default_coverage

static_assert(detail::default_coverage::every_axis_resolves(),
    "fixy::Default — at least one DimensionAxis enumerator does not "
    "have a strict_default_for specialization (or has multiple "
    "conflicting role markers).  Each axis must classify as exactly "
    "ONE of: caller-supplied (Type), has-strict-default (most axes), "
    "or has-derived-default (Observability).  Add the missing "
    "specialization, then re-run.");

static_assert(detail::default_coverage::type_defaults_match_substrate(),
    "fixy::Default — a type-valued strict-default aliased here has "
    "drifted from safety::fn::Fn<int>'s shipped default.  Likely "
    "cause: the substrate's per-axis default was changed without "
    "updating fixy/Default.h alongside.  Re-align fixy/Default.h's "
    "specialization with the substrate's authoritative default in "
    "safety/Fn.h's class template parameter list.");

static_assert(detail::default_coverage::enum_defaults_match_substrate(),
    "fixy::Default — an enum-valued strict-default aliased here has "
    "drifted from safety::fn::Fn<int>'s shipped default.  Same fix "
    "as the type-defaults assertion above.");

}  // namespace crucible::fixy
