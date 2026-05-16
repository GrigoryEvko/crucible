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

#include <crucible/algebra/lattices/ToleranceLattice.h>
#include <crucible/algebra/lattices/VendorLattice.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/fixy/Dim.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

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
//
// FIXY-G9: grant_base carries the default proof-relevance witness type
// (Asserted<UnnamedRationale>).  Evidenced variants `cg::*_e<W>`
// override `witness_t` to declare a stronger witness tier.  Inheriting
// `witness_t` through the base lets the entire grant catalog default
// to "Asserted" without per-grant edits — only the evidenced wrappers
// override.

struct grant_base {
    using witness_t = ::crucible::safety::witness::DefaultWitness;
};

// ═════════════════════════════════════════════════════════════════════
// ── Universal acknowledgement: accept_default_strict_for<D> ────────
// ═════════════════════════════════════════════════════════════════════
//
// One template covers all 20 dims via non-type template parameter.
// `final` closes the inheritance-bypass cheat (FIXY-A-PLUS-1).
// EBO-collapsible to 0 bytes when embedded; standalone sizeof == 1.

template <dim::DimAxis D>
struct accept_default_strict_for final : grant_base {
    // FIXY-AUDIT-NTTP: reject out-of-range NTTPs (e.g.,
    // `accept_default_strict_for<static_cast<dim::DimAxis>(99)>`).
    // The enum class's uint8_t underlying type admits 0..255, but only
    // 0..19 name real dims; an engagement tag pointing at a phantom
    // value silently fails to engage anything, which would mask
    // authoring bugs.  Pin at instantiation so the compiler diagnostic
    // names the offending template argument.
    static_assert(dim::is_valid_axis_v<D>,
        "fixy::accept_default_strict_for<D> instantiated with a "
        "DimAxis value outside the 20-enumerator range.  Use one of "
        "dim::Type ... dim::Staleness — out-of-range casts engage no "
        "real dim and silently mask an authoring bug.");
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

// ── Typed-NTTP vendor / recipe tier (CNTP/Forge/Mimic vocab) ─────────
//
// The opaque `vendor<V>` / `tier<R>` forms above admit arbitrary tag
// types — useful for unique-identity tags from a downstream crate but
// silent if the author forgets to thread a meaningful identity.  The
// typed-NTTP variants below carry the substrate's algebra-lattice
// enum directly, so a `grant::vendor_backend<NV>` is round-trippable
// through the federation cache key without naming an external tag.
//
// `IsMimicVendor` (CLAUDE.md taxonomy) keeps the strict-tier semantic
// — None / Portable still admit through, but the kernel-emit path
// gates on `backend != None` separately at the Mimic level.

using ::crucible::algebra::lattices::Tolerance;
using ::crucible::algebra::lattices::VendorBackend;

template <VendorBackend Backend>
struct vendor_backend final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    static constexpr VendorBackend value = Backend;
};

template <Tolerance T>
struct recipe_tier final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    static constexpr Tolerance value = T;
};

// Convenience aliases — one per shipped vendor / tier.
using vendor_cpu      = vendor_backend<VendorBackend::CPU>;
using vendor_nv       = vendor_backend<VendorBackend::NV>;
using vendor_am       = vendor_backend<VendorBackend::AMD>;
using vendor_tpu      = vendor_backend<VendorBackend::TPU>;
using vendor_trn      = vendor_backend<VendorBackend::TRN>;
using vendor_cer      = vendor_backend<VendorBackend::CER>;
using vendor_portable = vendor_backend<VendorBackend::Portable>;

using tier_relaxed    = recipe_tier<Tolerance::RELAXED>;
using tier_ulp_int8   = recipe_tier<Tolerance::ULP_INT8>;
using tier_ulp_fp8    = recipe_tier<Tolerance::ULP_FP8>;
using tier_ulp_fp16   = recipe_tier<Tolerance::ULP_FP16>;
using tier_ulp_fp32   = recipe_tier<Tolerance::ULP_FP32>;
using tier_ulp_fp64   = recipe_tier<Tolerance::ULP_FP64>;
using tier_bitexact   = recipe_tier<Tolerance::BITEXACT>;

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
    static_assert(std::is_integral_v<decltype(N)>,
        "grant::complexity_linear<N> requires an integral N.");
    static_assert(N > 0,
        "grant::complexity_linear<N> requires N > 0.  Use grant::"
        "complexity_constant for O(1).");
    static constexpr auto value = N;
};

template <auto N>
struct complexity_quadratic final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Complexity;
    static_assert(std::is_integral_v<decltype(N)>,
        "grant::complexity_quadratic<N> requires an integral N.");
    static_assert(N > 0,
        "grant::complexity_quadratic<N> requires N > 0.  Use grant::"
        "complexity_constant for O(1).");
    static constexpr auto value = N;
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
    // FIXY-AUDIT-NTTP: N must be a positive integral byte bound.  Zero
    // or negative would silently collapse to "no allocation permitted"
    // which IS the strict default (space::Zero) — engaging Space with
    // a degenerate bound masks the author's intent.
    static_assert(std::is_integral_v<decltype(N)>,
        "grant::space_bounded<N> requires an integral N.");
    static_assert(N > 0,
        "grant::space_bounded<N> requires N > 0.  Use accept_default_"
        "strict_for<dim::Space> for the zero-byte (stack-only) case.");
    static constexpr auto value = N;
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
    static_assert(std::is_integral_v<decltype(Depth)>,
        "grant::sized<Depth> requires an integral Depth.");
    static_assert(Depth > 0,
        "grant::sized<Depth> requires Depth > 0.  Use accept_default_"
        "strict_for<dim::Size> for unstated-depth bindings.");
    static constexpr auto value = Depth;
};

// ── Version (dim::Version) ───────────────────────────────────────────
template <std::uint32_t V>
struct version final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Version;
    static constexpr std::uint32_t value = V;
    // FIXY-AUDIT-NTTP: V=0 is structurally meaningless (substrate strict
    // default is 1; a literal v=0 in a federation-cache key would
    // collide with "unset").  Authors who want strict default use
    // accept_default_strict_for<dim::Version>.
    static_assert(V > 0,
        "grant::version<V> requires V > 0.  Use accept_default_strict_"
        "for<dim::Version> for the strict default (V=1).");
};

// ── Forge phase identity (Provenance axis, CNTP/Forge vocab) ─────────
//
// Forge runs a 12-phase pipeline (FORGE.md §5: INGEST → ANALYZE →
// REWRITE → FUSE → LOWER → TILE → MEMPLAN → COMPILE → SCHEDULE → EMIT
// → DISTRIBUTE → VALIDATE).  A binding produced INSIDE a phase carries
// that phase's identity in its provenance, so cross-phase composition
// (mint_pipeline) can verify the phase ORDER at the type level.
//
// ForgePhase is a closed enumeration (12 values) under the Provenance
// dim — distinct from generic `from_source<S>` which admits arbitrary
// source tags.  Pinning to an enum gives Forge code a single greppable
// surface and makes phase-ordering verification a compile-time check.

enum class ForgePhase : std::uint8_t {
    Ingest     = 0,
    Analyze    = 1,
    Rewrite    = 2,
    Fuse       = 3,
    Lower      = 4,
    Tile       = 5,
    MemPlan    = 6,
    Compile    = 7,
    Schedule   = 8,
    Emit       = 9,
    Distribute = 10,
    Validate   = 11,
};

template <ForgePhase P>
struct forge_phase final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Provenance;
    static constexpr ForgePhase value = P;
};

// ── CNTP transport-tier identity (Representation axis) ───────────────
//
// CNTP layers compose across heterogeneous transports (AF_XDP, RDMA,
// TCP, KTLS, QUIC, RoCEv2).  Each layer engages Representation through
// transport_tier; the typed NTTP makes the transport visible in the
// signature so receive-side trust posture (Sanitized after CRC vs
// Sanitized after AEAD) can gate on it.
//
// Like ForgePhase, this is a closed enumeration — adding a transport
// is a single-edit story (enum addition + neg-compile fixture per
// HS14 if the new value affects a resolver decision).

enum class TransportTier : std::uint8_t {
    Loopback = 0,
    Tcp      = 1,
    AfXdp    = 2,
    Rdma     = 3,
    RoceV2   = 4,
    Ktls     = 5,
    Quic     = 6,
    Wireguard = 7,
};

template <TransportTier T>
struct transport_tier final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    static constexpr TransportTier value = T;
};

// ── Staleness (dim::Staleness) ───────────────────────────────────────
template <auto TauMax>
struct stale_to final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Staleness;
    static_assert(std::is_integral_v<decltype(TauMax)>,
        "grant::stale_to<TauMax> requires an integral TauMax (the "
        "maximum allowable staleness in implementation-defined units).");
    static_assert(TauMax > 0,
        "grant::stale_to<TauMax> requires TauMax > 0.  Use accept_"
        "default_strict_for<dim::Staleness> for Fresh (τ=0).");
    static constexpr auto value = TauMax;
};

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-G9: Evidenced grant variants ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Every shipped grant has a parallel `*_e<W>` variant carrying a
// witness type W.  The base form (`grant::copy`, `grant::reentrant`,
// ...) inherits `witness_t = DefaultWitness` from grant_base; the
// evidenced form (`grant::copy_e<Tested<id>>`, ...) overrides
// `witness_t = W` to declare a stronger proof-relevance tier.
//
// The evidenced form is a DISTINCT grant tag (new identity, new
// final-derivation slot), not a subclass of the base — the base is
// `final`.  Each evidenced variant re-states the `relaxes` axis so
// engages_dim_v / IsGrantTag continue to work without change.
//
// The macro CRUCIBLE_FIXY_EVIDENCED_VARIANT generates the witness-
// overriding form for the simplest case (zero-parameter base).
// Parametric grants (template <auto N> struct sized) get hand-rolled
// variants below — the macro handles the bulk.

#define CRUCIBLE_FIXY_EVIDENCED_VARIANT(base_name, dim_value)         \
    template <::crucible::safety::witness::IsWitness W>               \
    struct base_name##_e final : grant_base {                         \
        static constexpr dim::DimAxis relaxes = dim_value;            \
        using witness_t = W;                                          \
    }

// ── Usage ─────────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(affine,           dim::Usage);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(copy,             dim::Usage);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(ghost,            dim::Usage);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(borrow,           dim::Usage);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(capability_usage, dim::Usage);

// ── Security ──────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(upgrade_to_secret, dim::Security);

// ── Representation ────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(repr_c,       dim::Representation);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(repr_packed,  dim::Representation);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(repr_aligned, dim::Representation);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(repr_simd,    dim::Representation);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(repr_atomic,  dim::Representation);

// ── Observability ─────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(observability_visible, dim::Observability);

// ── Complexity ────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(complexity_constant,  dim::Complexity);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(complexity_unbounded, dim::Complexity);

// ── Precision ─────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(precision_f32, dim::Precision);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(precision_f64, dim::Precision);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(reassociate,   dim::Precision);

// ── Space ─────────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(space_unbounded, dim::Space);

// ── Overflow ──────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(overflow_wrap,     dim::Overflow);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(overflow_saturate, dim::Overflow);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(overflow_widen,    dim::Overflow);

// ── Mutation ──────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(mutable_in_place,  dim::Mutation);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(append_only,       dim::Mutation);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(monotonic_advance, dim::Mutation);

// ── Reentrancy ────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(reentrant, dim::Reentrancy);
CRUCIBLE_FIXY_EVIDENCED_VARIANT(coroutine, dim::Reentrancy);

// ── Size ──────────────────────────────────────────────────────────────
CRUCIBLE_FIXY_EVIDENCED_VARIANT(productive, dim::Size);

#undef CRUCIBLE_FIXY_EVIDENCED_VARIANT

// ── Parametric evidenced variants (hand-rolled) ──────────────────────

// Typed-NTTP vendor / recipe / transport / forge phase with witness.
template <VendorBackend Backend, ::crucible::safety::witness::IsWitness W>
struct vendor_backend_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    static constexpr VendorBackend value = Backend;
    using witness_t = W;
};

template <Tolerance T, ::crucible::safety::witness::IsWitness W>
struct recipe_tier_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    static constexpr Tolerance value = T;
    using witness_t = W;
};

template <TransportTier T, ::crucible::safety::witness::IsWitness W>
struct transport_tier_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Representation;
    static constexpr TransportTier value = T;
    using witness_t = W;
};

template <ForgePhase P, ::crucible::safety::witness::IsWitness W>
struct forge_phase_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Provenance;
    static constexpr ForgePhase value = P;
    using witness_t = W;
};

// with<Es...> with witness (variadic effects need their own template).
template <::crucible::safety::witness::IsWitness W,
          ::crucible::effects::Effect... Es>
struct with_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Effect;
    using witness_t = W;
    static_assert(sizeof...(Es) > 0,
        "grant::with_e<W> with empty effect pack is structurally degenerate. "
        "Use accept_default_strict_for<dim::Effect> for the strict-default case.");
};

// declassify<Policy> with witness.
template <typename Policy, ::crucible::safety::witness::IsWitness W>
struct declassify_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Security;
    using policy = Policy;
    using witness_t = W;
};

// from_source<SourceTag> with witness.
template <typename SourceTag, ::crucible::safety::witness::IsWitness W>
struct from_source_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Provenance;
    using source_tag = SourceTag;
    using witness_t = W;
};

// sanitize<TaintClass> with witness.
template <typename TaintClass, ::crucible::safety::witness::IsWitness W>
struct sanitize_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Provenance;
    using taint_class = TaintClass;
    using witness_t = W;
};

// trust_assumed_for<TaintClass> with witness.
template <typename TaintClass, ::crucible::safety::witness::IsWitness W>
struct trust_assumed_for_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Trust;
    using taint_class = TaintClass;
    using witness_t = W;
};

// lifetime_region<auto RegionTag> with witness.
template <auto RegionTag, ::crucible::safety::witness::IsWitness W>
struct lifetime_region_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Lifetime;
    using witness_t = W;
};

// refined_with<Pred> with witness.
template <typename Pred, ::crucible::safety::witness::IsWitness W>
struct refined_with_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Refinement;
    using predicate = Pred;
    using witness_t = W;
};

// space_bounded<N> with witness.
template <auto N, ::crucible::safety::witness::IsWitness W>
struct space_bounded_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Space;
    static_assert(std::is_integral_v<decltype(N)>,
        "grant::space_bounded_e<N, W> requires an integral N.");
    static_assert(N > 0,
        "grant::space_bounded_e<N, W> requires N > 0.");
    static constexpr auto value = N;
    using witness_t = W;
};

// sized<Depth> with witness.
template <auto Depth, ::crucible::safety::witness::IsWitness W>
struct sized_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Size;
    static_assert(std::is_integral_v<decltype(Depth)>,
        "grant::sized_e<Depth, W> requires an integral Depth.");
    static_assert(Depth > 0,
        "grant::sized_e<Depth, W> requires Depth > 0.");
    static constexpr auto value = Depth;
    using witness_t = W;
};

// version<V> with witness.
template <std::uint32_t V, ::crucible::safety::witness::IsWitness W>
struct version_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Version;
    static constexpr std::uint32_t value = V;
    using witness_t = W;
    static_assert(V > 0, "grant::version_e<V, W> requires V > 0.");
};

// stale_to<TauMax> with witness.
template <auto TauMax, ::crucible::safety::witness::IsWitness W>
struct stale_to_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Staleness;
    static_assert(std::is_integral_v<decltype(TauMax)>,
        "grant::stale_to_e<TauMax, W> requires an integral TauMax.");
    static_assert(TauMax > 0,
        "grant::stale_to_e<TauMax, W> requires TauMax > 0.");
    static constexpr auto value = TauMax;
    using witness_t = W;
};

// typed<T> with witness.
template <typename T, ::crucible::safety::witness::IsWitness W>
struct typed_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Type;
    using type = T;
    using witness_t = W;
};

// trust_assumed<auto Rationale> with witness.
template <auto Rationale, ::crucible::safety::witness::IsWitness W>
struct trust_assumed_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Trust;
    using witness_t = W;
};

// protocol_session<Proto> with witness.
template <typename Proto, ::crucible::safety::witness::IsWitness W>
struct protocol_session_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Protocol;
    using protocol = Proto;
    using witness_t = W;
};

// precision_higham<Bound> with witness.
template <auto Bound, ::crucible::safety::witness::IsWitness W>
struct precision_higham_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Precision;
    using witness_t = W;
};

// complexity_linear<N> / complexity_quadratic<N> with witness.
template <auto N, ::crucible::safety::witness::IsWitness W>
struct complexity_linear_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Complexity;
    static_assert(std::is_integral_v<decltype(N)>,
        "grant::complexity_linear_e<N, W> requires integral N.");
    static_assert(N > 0,
        "grant::complexity_linear_e<N, W> requires N > 0.");
    static constexpr auto value = N;
    using witness_t = W;
};

template <auto N, ::crucible::safety::witness::IsWitness W>
struct complexity_quadratic_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Complexity;
    static_assert(std::is_integral_v<decltype(N)>,
        "grant::complexity_quadratic_e<N, W> requires integral N.");
    static_assert(N > 0,
        "grant::complexity_quadratic_e<N, W> requires N > 0.");
    static constexpr auto value = N;
    using witness_t = W;
};

}  // namespace grant

// accept_default_strict_for_e<D, W> — strict-ack with witness override.
// Lives at fixy:: level alongside accept_default_strict_for<D>.
template <dim::DimAxis D, ::crucible::safety::witness::IsWitness W>
struct accept_default_strict_for_e final : grant_base {
    static_assert(dim::is_valid_axis_v<D>,
        "fixy::accept_default_strict_for_e<D, W> instantiated with a "
        "DimAxis value outside the 20-enumerator range.");
    static constexpr dim::DimAxis relaxes = D;
    static constexpr bool is_explicit_accept = true;
    using witness_t = W;
};

static_assert(::crucible::safety::NotInherited<accept_default_strict_for_e<dim::Type,
    ::crucible::safety::witness::Tested<0>>>);

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

// New typed-NTTP CNTP/Forge/Mimic vocab — empty + final + grant_base.
static_assert(IsGrantTag<grant::vendor_nv>);
static_assert(IsGrantTag<grant::tier_bitexact>);
static_assert(IsGrantTag<grant::transport_tier<grant::TransportTier::AfXdp>>);
static_assert(IsGrantTag<grant::forge_phase<grant::ForgePhase::Ingest>>);
static_assert(sizeof(grant::vendor_nv) == 1);
static_assert(sizeof(grant::tier_bitexact) == 1);
static_assert(sizeof(grant::transport_tier<grant::TransportTier::Tcp>) == 1);
static_assert(sizeof(grant::forge_phase<grant::ForgePhase::Lower>) == 1);
static_assert(::crucible::safety::NotInherited<grant::vendor_nv>);
static_assert(::crucible::safety::NotInherited<grant::tier_bitexact>);
static_assert(::crucible::safety::NotInherited<grant::transport_tier<grant::TransportTier::Quic>>);
static_assert(::crucible::safety::NotInherited<grant::forge_phase<grant::ForgePhase::Emit>>);

// Typed-NTTP values round-trip through the tag's `value` member.
static_assert(grant::vendor_nv::value      == grant::VendorBackend::NV);
static_assert(grant::tier_bitexact::value  == grant::Tolerance::BITEXACT);
static_assert(grant::transport_tier<grant::TransportTier::Rdma>::value
              == grant::TransportTier::Rdma);
static_assert(grant::forge_phase<grant::ForgePhase::Validate>::value
              == grant::ForgePhase::Validate);

// All shipped typed-NTTP tags engage dim::Representation (vendor/tier/
// transport) or dim::Provenance (forge_phase) — confirms relaxes axis.
static_assert(grant::vendor_nv::relaxes      == dim::Representation);
static_assert(grant::tier_bitexact::relaxes  == dim::Representation);
static_assert(grant::transport_tier<grant::TransportTier::Tcp>::relaxes
              == dim::Representation);
static_assert(grant::forge_phase<grant::ForgePhase::Ingest>::relaxes
              == dim::Provenance);

// FIXY-G9: every grant tag carries witness_t.  Bare form defaults to
// DefaultWitness (Asserted<UnnamedRationale>); evidenced *_e<W> form
// overrides to the specified W.
static_assert(std::is_same_v<typename grant::copy::witness_t,
                             ::crucible::safety::witness::DefaultWitness>);
static_assert(std::is_same_v<typename grant::reentrant::witness_t,
                             ::crucible::safety::witness::DefaultWitness>);
static_assert(std::is_same_v<typename grant::vendor_nv::witness_t,
                             ::crucible::safety::witness::DefaultWitness>);
static_assert(std::is_same_v<typename accept_default_strict_for<dim::Type>::witness_t,
                             ::crucible::safety::witness::DefaultWitness>);

// Evidenced variants override witness_t.
using TestedW   = ::crucible::safety::witness::Tested<42>;
using CrossValW = ::crucible::safety::witness::CrossValidated<7>;

static_assert(std::is_same_v<typename grant::copy_e<TestedW>::witness_t, TestedW>);
static_assert(std::is_same_v<typename grant::reentrant_e<TestedW>::witness_t, TestedW>);
static_assert(std::is_same_v<typename grant::mutable_in_place_e<CrossValW>::witness_t,
                             CrossValW>);
static_assert(std::is_same_v<typename grant::vendor_backend_e<grant::VendorBackend::NV,
                                                              TestedW>::witness_t,
                             TestedW>);
static_assert(std::is_same_v<typename grant::with_e<TestedW,
                                                    ::crucible::effects::Effect::IO>::witness_t,
                             TestedW>);

// Evidenced variants preserve relaxes axis.
static_assert(grant::copy_e<TestedW>::relaxes == dim::Usage);
static_assert(grant::reentrant_e<TestedW>::relaxes == dim::Reentrancy);
static_assert(grant::vendor_backend_e<grant::VendorBackend::NV, TestedW>::relaxes
              == dim::Representation);

// Evidenced variants are IsGrantTag.
static_assert(IsGrantTag<grant::copy_e<TestedW>>);
static_assert(IsGrantTag<grant::reentrant_e<TestedW>>);
static_assert(IsGrantTag<grant::with_e<TestedW, ::crucible::effects::Effect::IO>>);

// Evidenced variants are EBO-collapsible (sizeof == 1 standalone).
static_assert(sizeof(grant::copy_e<TestedW>) == 1);
static_assert(sizeof(grant::reentrant_e<TestedW>) == 1);
static_assert(sizeof(grant::with_e<TestedW, ::crucible::effects::Effect::IO>) == 1);

// Evidenced variants are `final` (cheat-bypass closure).
static_assert(::crucible::safety::NotInherited<grant::copy_e<TestedW>>);
static_assert(::crucible::safety::NotInherited<grant::reentrant_e<TestedW>>);

}  // namespace detail

}  // namespace crucible::fixy
