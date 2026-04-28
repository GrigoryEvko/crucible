// ═══════════════════════════════════════════════════════════════════
// test_migration_verification — MIGRATE-12 consolidated harness
//
// Why this exists:
//   The Graded foundation refactor (misc/25_04_2026.md §2) collapsed
//   eight independent wrapper templates into thin aliases over a
//   single `Graded<Modality, Lattice, T>` substrate.  Each per-header
//   self-test verifies its OWN wrapper.  None verifies the *uniform*
//   contract: that every migrated wrapper exposes the same diagnostic
//   surface, preserves sizeof, and composes freely with every other
//   migrated wrapper.
//
//   This TU is the single place that asserts those cross-cutting
//   properties.  It catches:
//
//   - Asymmetric forwarder coverage (e.g. Linear gains lattice_name()
//     but Tagged silently doesn't — the per-header self-tests would
//     never notice).
//   - sizeof regression after a Graded refactor that broke EBO on
//     one wrapper but not the others.
//   - Cross-composition failures (e.g. Tagged<Linear<int>, Tag> fails
//     to instantiate after a Graded constraint change).
//   - Drift between the migration map (25_04_2026 §2.3) and the
//     actual `using` aliases shipped in safety/*.h.
//
// Coverage discipline:
//   When a new MIGRATE-N task lands a new wrapper, add it to the
//   COVERAGE MATRIX section below.  The static_asserts fire at
//   TU-include time under the test target's full warning matrix.
//
// Trust boundary:
//   The per-header self-tests (Linear.h, Refined.h, Tagged.h, Secret.h,
//   Mutation.h, Stale.h, TimeOrdered.h) remain authoritative for
//   wrapper-local behavior (consume / peek / contracts / construction).
//   This harness is strictly cross-cutting.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/algebra/GradedTrait.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/TimeOrdered.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;

// Witness predicate for Refined (in scope of Refined.h's predicates,
// but we redeclare a local one for isolation).
struct PositiveCheck {
    constexpr bool operator()(int x) const noexcept { return x > 0; }
};
inline constexpr PositiveCheck positive_local{};

// Witness tag for Tagged.
struct VerificationTag {};

// ── COVERAGE MATRIX — sizeof preservation ──────────────────────────
//
// Each migrated wrapper must collapse to its underlying T's storage
// for the stateless wrappers (Linear / Refined / Tagged / Secret).
// Stateful wrappers (Monotonic, AppendOnly) collapse to the storage
// of their underlying container or value-and-grade combine cell.
//
// Stale stores T + a uint64_t staleness counter.
// TimeOrdered<T, N> stores T + N * uint64_t vector clock.

static_assert(sizeof(Linear<int>)                   == sizeof(int));
static_assert(sizeof(Linear<long long>)             == sizeof(long long));
static_assert(sizeof(Linear<void*>)                 == sizeof(void*));

static_assert(sizeof(Refined<positive_local, int>)  == sizeof(int));

// SealedRefined shares Refined's substrate (BoolLattice<P>); the
// "sealed" property is API-surface only (no into() rvalue extractor).
// Layout is identical to Refined and to bare T.
static_assert(sizeof(SealedRefined<positive_local, int>) == sizeof(int));

static_assert(sizeof(Tagged<int, VerificationTag>)  == sizeof(int));
static_assert(sizeof(Tagged<long, VerificationTag>) == sizeof(long));

static_assert(sizeof(Secret<int>)                   == sizeof(int));
static_assert(sizeof(Secret<long long>)             == sizeof(long long));

// NumericalTier<T_at, T> — regime-1 EBO collapse via the
// ToleranceLattice::At<T_at> singleton sub-lattice (FOUND-G01).
// Witnessed across the tier spectrum at sizeof(double) since
// recipe-tiered values production-typically wrap floating-point
// payloads.
static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, double>) == sizeof(double));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP16, double>) == sizeof(double));
static_assert(sizeof(NumericalTier<Tolerance::RELAXED,  long long>)
                                                                 == sizeof(long long));

// Consistency<Level, T> — regime-1 EBO collapse via the
// ConsistencyLattice::At<Level> singleton sub-lattice (FOUND-G05).
static_assert(sizeof(Consistency<Consistency_v::STRONG,            int>)    == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::CAUSAL_PREFIX,     double>) == sizeof(double));
static_assert(sizeof(Consistency<Consistency_v::EVENTUAL,          long long>)
                                                                            == sizeof(long long));

// OpaqueLifetime<Scope, T> — regime-1 EBO collapse via the
// LifetimeLattice::At<Scope> singleton sub-lattice (FOUND-G09).
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET,   int>)    == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_PROGRAM, double>) == sizeof(double));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_REQUEST, long long>)
                                                                      == sizeof(long long));

// Monotonic<T, std::less<T>> collapses value+grade into one T cell
// via Graded's specialization for `T == element_type`.
static_assert(sizeof(Monotonic<std::uint32_t>)      == sizeof(std::uint32_t));
static_assert(sizeof(Monotonic<std::uint64_t>)      == sizeof(std::uint64_t));

// SharedPermission<Tag> is a façade migration (regime-5, MIGRATE-7):
// the proof token stays an empty class (sizeof 1, EBO-collapsible to
// 0).  Runtime fractional-share state lives in SharedPermissionPool,
// not at the SharedPermission instance.  See Permission.h's MIGRATE-7
// audit block for the full rationale.
static_assert(sizeof(SharedPermission<VerificationTag>) == 1);

// ── COVERAGE MATRIX — value_type_name forwarder uniformity ─────────
//
// Every migrated wrapper exposes value_type_name() forwarded from
// Graded::value_type_name().  The string is reflection-derived so
// per the gcc16_c26_reflection_gotchas memory rule, use ends_with
// over == (display_string_of is TU-context-fragile for primitive
// types — could be "int" or "::int" depending on TU surroundings).

static_assert(Linear<int>::value_type_name().ends_with("int"));
static_assert(Refined<positive_local, int>::value_type_name().ends_with("int"));
static_assert(SealedRefined<positive_local, int>::value_type_name().ends_with("int"));
static_assert(Tagged<int, VerificationTag>::value_type_name().ends_with("int"));
static_assert(Secret<int>::value_type_name().ends_with("int"));
static_assert(NumericalTier<Tolerance::BITEXACT, int>::value_type_name().ends_with("int"));
static_assert(Consistency<Consistency_v::STRONG, int>::value_type_name().ends_with("int"));
static_assert(OpaqueLifetime<Lifetime_v::PER_FLEET, int>::value_type_name().ends_with("int"));
static_assert(Monotonic<std::uint64_t>::value_type_name().ends_with("uint64_t")
           || Monotonic<std::uint64_t>::value_type_name().ends_with("long unsigned int"));
static_assert(Stale<int>::value_type_name().ends_with("int"));
static_assert(TimeOrdered<int, 4>::value_type_name().ends_with("int"));
// SharedPermission's value_type IS the Tag (phantom region label); the
// reflection-derived name ends with the local tag struct's name.
static_assert(SharedPermission<VerificationTag>::value_type_name()
                                            .ends_with("VerificationTag"));

// ── COVERAGE MATRIX — public graded_type alias (GRADED-TRAIT-1) ────
//
// Every migrated wrapper exposes `graded_type` in its public section
// so external code can introspect the migration mapping.  These
// static_asserts are unevaluated `using` checks — if the alias were
// private, accessing it via `Wrapper::graded_type` would be ill-formed
// and the static_assert would fail to compile.  This catches accidental
// regressions where a future refactor moves the alias back to private.

static_assert(!std::is_void_v<typename Linear<int>::graded_type>);
static_assert(!std::is_void_v<typename Refined<positive_local, int>::graded_type>);
static_assert(!std::is_void_v<typename SealedRefined<positive_local, int>::graded_type>);
static_assert(!std::is_void_v<typename Tagged<int, VerificationTag>::graded_type>);
static_assert(!std::is_void_v<typename Secret<int>::graded_type>);
static_assert(!std::is_void_v<typename NumericalTier<Tolerance::BITEXACT, int>::graded_type>);
static_assert(!std::is_void_v<typename Consistency<Consistency_v::STRONG, int>::graded_type>);
static_assert(!std::is_void_v<typename OpaqueLifetime<Lifetime_v::PER_FLEET, int>::graded_type>);
static_assert(!std::is_void_v<typename Monotonic<std::uint64_t>::graded_type>);
static_assert(!std::is_void_v<typename AppendOnly<int>::graded_type>);
static_assert(!std::is_void_v<typename Stale<int>::graded_type>);
static_assert(!std::is_void_v<typename TimeOrdered<int, 4>::graded_type>);
static_assert(!std::is_void_v<typename SharedPermission<VerificationTag>::graded_type>);

// ── COVERAGE MATRIX — GradedWrapper concept (GRADED-TRAIT-2) ───────
//
// Single fold-asserts that every migrated wrapper satisfies the
// uniform GradedWrapper contract.  This is the structural promotion
// of the per-wrapper checks above into one concept that future
// MIGRATE-N work cannot bypass.
//
// If a future refactor breaks any forwarder (lattice_name/
// value_type_name returning the wrong type, missing graded_type,
// etc.), the concept rejects the wrapper and the static_assert
// names the offending type.

using namespace ::crucible::algebra;

static_assert(GradedWrapper<Linear<int>>);
static_assert(GradedWrapper<Refined<positive_local, int>>);
static_assert(GradedWrapper<SealedRefined<positive_local, int>>);
static_assert(GradedWrapper<Tagged<int, VerificationTag>>);
static_assert(GradedWrapper<Secret<int>>);
static_assert(GradedWrapper<NumericalTier<Tolerance::BITEXACT, int>>);
static_assert(GradedWrapper<NumericalTier<Tolerance::ULP_FP16, double>>);
static_assert(GradedWrapper<Consistency<Consistency_v::STRONG, int>>);
static_assert(GradedWrapper<Consistency<Consistency_v::CAUSAL_PREFIX, double>>);
static_assert(GradedWrapper<OpaqueLifetime<Lifetime_v::PER_FLEET, int>>);
static_assert(GradedWrapper<OpaqueLifetime<Lifetime_v::PER_REQUEST, double>>);
static_assert(GradedWrapper<Monotonic<std::uint64_t>>);
static_assert(GradedWrapper<AppendOnly<int>>);
static_assert(GradedWrapper<Stale<int>>);
static_assert(GradedWrapper<TimeOrdered<int, 4>>);
static_assert(GradedWrapper<SharedPermission<VerificationTag>>);

// The is_graded_wrapper_v variable template auto-specializes from
// the concept — same coverage, value-form for use in metaprograms.
static_assert(is_graded_wrapper_v<Linear<int>>);
static_assert(is_graded_wrapper_v<SharedPermission<VerificationTag>>);

// Negative coverage (sanity) — bare types are NOT graded wrappers.
static_assert(!is_graded_wrapper_v<int>);
static_assert(!is_graded_wrapper_v<void*>);
static_assert(!is_graded_wrapper_v<std::string_view>);

// ── COVERAGE MATRIX — forwarder fidelity (GRADED-CONCEPT-C5) ───────
//
// Concept admits "lattice_name() returns string_view" but doesn't
// enforce "returns the SAME string as graded_type::lattice_name()".
// A wrapper with a custom forwarder returning a wrong string passes
// the concept silently.  This template-folded check closes the hole
// — if any forwarder ever stops forwarding, the per-wrapper
// instantiation fires at compile time.
//
// Implemented as a consteval bool function rather than a macro
// because template-arg commas would split macro arguments and
// `(W)::` parses as old-style cast under -Werror=old-style-cast.

template <typename W>
[[nodiscard]] consteval bool forwarders_actually_forward() noexcept {
    return W::value_type_name() == W::graded_type::value_type_name()
        && W::lattice_name()    == W::graded_type::lattice_name();
}

static_assert(forwarders_actually_forward<Linear<int>>());
static_assert(forwarders_actually_forward<Refined<positive_local, int>>());
static_assert(forwarders_actually_forward<SealedRefined<positive_local, int>>());
static_assert(forwarders_actually_forward<Tagged<int, VerificationTag>>());
static_assert(forwarders_actually_forward<Secret<int>>());
static_assert(forwarders_actually_forward<NumericalTier<Tolerance::BITEXACT, int>>());
static_assert(forwarders_actually_forward<NumericalTier<Tolerance::ULP_FP16, double>>());
static_assert(forwarders_actually_forward<Consistency<Consistency_v::STRONG, int>>());
static_assert(forwarders_actually_forward<Consistency<Consistency_v::EVENTUAL, double>>());
static_assert(forwarders_actually_forward<OpaqueLifetime<Lifetime_v::PER_FLEET, int>>());
static_assert(forwarders_actually_forward<OpaqueLifetime<Lifetime_v::PER_REQUEST, double>>());
static_assert(forwarders_actually_forward<Monotonic<std::uint64_t>>());
static_assert(forwarders_actually_forward<AppendOnly<int>>());
static_assert(forwarders_actually_forward<Stale<int>>());
static_assert(forwarders_actually_forward<TimeOrdered<int, 4>>());
static_assert(forwarders_actually_forward<SharedPermission<VerificationTag>>());

// ── COVERAGE MATRIX — lattice_name forwarder uniformity ────────────
//
// Hand-written consteval string literals in algebra/lattices/*.h
// are TU-stable; use == for tight coverage.  The exception is
// BoolLattice + TrustLattice, which derive their name from
// display_string_of(^^Pred) / (^^Source) — those are TU-fragile.

static_assert(Linear<int>::lattice_name()       == "QttSemiring::At<1>");
static_assert(Refined<positive_local, int>::lattice_name()
                                                 .ends_with("PositiveCheck"));
// SealedRefined shares Refined's BoolLattice<P> substrate exactly;
// the lattice_name is identical.  External code distinguishes the
// two wrappers by class identity, not by lattice name.
static_assert(SealedRefined<positive_local, int>::lattice_name()
                                                 .ends_with("PositiveCheck"));
static_assert(Tagged<int, VerificationTag>::lattice_name()
                                                 .ends_with("VerificationTag"));
static_assert(Secret<int>::lattice_name()       == "ConfLattice::At<Secret>");
static_assert(NumericalTier<Tolerance::BITEXACT, int>::lattice_name()
                                                 == "ToleranceLattice::At<BITEXACT>");
static_assert(NumericalTier<Tolerance::ULP_FP16, double>::lattice_name()
                                                 == "ToleranceLattice::At<ULP_FP16>");
static_assert(NumericalTier<Tolerance::RELAXED,  long long>::lattice_name()
                                                 == "ToleranceLattice::At<RELAXED>");
static_assert(Consistency<Consistency_v::STRONG, int>::lattice_name()
                                                 == "ConsistencyLattice::At<STRONG>");
static_assert(Consistency<Consistency_v::CAUSAL_PREFIX, double>::lattice_name()
                                                 == "ConsistencyLattice::At<CAUSAL_PREFIX>");
static_assert(Consistency<Consistency_v::EVENTUAL, long long>::lattice_name()
                                                 == "ConsistencyLattice::At<EVENTUAL>");
static_assert(OpaqueLifetime<Lifetime_v::PER_FLEET,   int>::lattice_name()
                                                 == "LifetimeLattice::At<PER_FLEET>");
static_assert(OpaqueLifetime<Lifetime_v::PER_PROGRAM, double>::lattice_name()
                                                 == "LifetimeLattice::At<PER_PROGRAM>");
static_assert(OpaqueLifetime<Lifetime_v::PER_REQUEST, long long>::lattice_name()
                                                 == "LifetimeLattice::At<PER_REQUEST>");
static_assert(Monotonic<std::uint64_t>::lattice_name() == "MonotoneLattice");
static_assert(AppendOnly<int>::lattice_name()   == "SeqPrefixLattice");
static_assert(Stale<int>::lattice_name()        == "StalenessSemiring");
static_assert(TimeOrdered<int, 4>::lattice_name() == "HappensBeforeLattice");
static_assert(SharedPermission<VerificationTag>::lattice_name() == "FractionalLattice");

// ── COVERAGE MATRIX — cross-wrapper composition ────────────────────
//
// Every migrated wrapper must be usable as the inner T of every
// other migrated wrapper that admits a movable T.  This proves the
// migration didn't introduce silent constraint regressions across
// the 8 wrappers.
//
// Skipping pairs that are semantically nonsensical (e.g. Refined of
// a moved-only Linear has no defined predicate; Secret of Tagged
// would need a comonad-vs-relative-monad blend).  Focus on the
// cross-cuts that DO appear in production code:
//   - Tagged<Linear<T>, Tag>       — provenance over linear resource
//   - Stale<int>                   — already exercised in Stale's test
//   - Refined wrapping arithmetic types (existing pattern)

using TaggedLinear = Tagged<Linear<int>, VerificationTag>;
static_assert(sizeof(TaggedLinear) == sizeof(int));

// MIGRATE-12-extend + GRADED-CONCEPT-L1: extended cross-composition.
// These compositions appear in production-code patterns; the
// harness asserts they instantiate cleanly + preserve sizeof where
// the regime promises it.

// Tagged<Linear<T>, Source> — provenance over linear resource.
// Already exercised above; kept as the canonical example.

// Refined<P, Linear<T>>... wait, Refined needs a predicate over T,
// and Linear<T> isn't directly comparable.  The predicate works on
// the WRAPPED value, so this composition would require the predicate
// be invocable on Linear<T>.  Most natural-language predicates aren't.
// SKIP this composition — not a meaningful pattern.

// Secret<Refined<P, T>> — classified-then-predicate-checked.
// Production: Cipher accepts a Secret<Refined<aligned, void*>>
// from the Vessel boundary; the Tagged-Refined sequencing layers
// classification over validation.
using SecretRefined = Secret<Refined<positive_local, int>>;
static_assert(sizeof(SecretRefined) == sizeof(Refined<positive_local, int>));
static_assert(sizeof(SecretRefined) == sizeof(int));   // double EBO collapse

// Tagged<Stale<T>, Source> — staleness-with-provenance.  Production:
// async-DiLoCo gradient events carry both a vector clock (TimeOrdered)
// AND a source tag (Tagged) AND a staleness counter (Stale).  This
// composition exercises the regime-1 (Tagged) over regime-4 (Stale)
// nesting — Tagged's substrate type IS Stale<T>, not int.
using TaggedStale = Tagged<Stale<int>, VerificationTag>;
// sizeof grows by Stale's grade carrier (uint64_t staleness counter).
static_assert(sizeof(TaggedStale) >= sizeof(Stale<int>));

// Linear<SealedRefined<P, T>> — owned proof-token.  Production:
// session-boundary handles that carry a sealed-refined value with
// linear ownership semantics.  Both wrappers are regime-1 and
// EBO-collapse — the composed sizeof is just sizeof(T).
using LinearSealed = Linear<SealedRefined<positive_local, int>>;
static_assert(sizeof(LinearSealed) == sizeof(int));

// SealedRefined<P, Linear<T>> would require a predicate invocable
// on Linear<T> — same SKIP rationale as Refined<P, Linear<T>>.

// Tagged<NumericalTier<T_at, T>, Source> — provenance over recipe-tier
// pin.  Production: a Forge Phase E recipe-select output carries the
// NumericalTier (recipe-tier promise) AND the Tagged source provenance
// (which fleet member emitted the kernel).  Both wrappers are
// regime-1 over At<>-style empty grades — the composed sizeof must
// collapse to sizeof(T).
using TaggedNumericalTier = Tagged<NumericalTier<Tolerance::BITEXACT, int>, VerificationTag>;
static_assert(sizeof(TaggedNumericalTier) == sizeof(int),
    "Tagged<NumericalTier<...>, Source> must EBO-collapse to sizeof(T) "
    "— two regime-1 wrappers stacked, both with empty grade, must "
    "preserve the underlying T's storage exactly.");

// NumericalTier<T_at, Linear<T>> — recipe-tier pin over linear
// ownership.  Production: a kernel emit returns Linear<DeviceBuffer>
// pinned at BITEXACT — the buffer is consumed exactly once AND
// satisfies the bit-exact contract.  The composed wrapper:
//   * preserves Linear's move-only discipline (no copy ctor on
//     NumericalTier<T_at, Linear<T>> because Linear deletes copy)
//   * EBO-collapses to sizeof(Linear<T>) == sizeof(T)
using NumericalTierOverLinear = NumericalTier<Tolerance::BITEXACT, Linear<int>>;
static_assert(sizeof(NumericalTierOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<NumericalTierOverLinear>,
    "NumericalTier<T_at, Linear<T>> must preserve Linear's "
    "move-only discipline.  If this fires, Linear's copy-deletion "
    "is no longer transitively visible through the NumericalTier "
    "wrapper — investigate the defaulted-copy regression.");
static_assert(std::is_move_constructible_v<NumericalTierOverLinear>);

// NumericalTier<T_at, Refined<P, T>> — recipe-tier pin over predicate
// refinement.  Production: a precision-budget calibrator carries
// Refined<positive, double> pinned at ULP_FP32 — the value is
// strictly positive AND meets the FP32 precision contract.  Both
// regime-1 → EBO-collapse to sizeof(double).
using NumericalTierOverRefined =
    NumericalTier<Tolerance::ULP_FP32, Refined<positive_local, int>>;
static_assert(sizeof(NumericalTierOverRefined) == sizeof(int));

// Tagged<Consistency<Level, T>, Source> — provenance over consistency
// pin.  Production: a Canopy gossip event carries the parameter shard's
// Consistency<STRONG> AND a Tagged<...,FromPeerNodeId> source — both
// regime-1 wrappers over At<>-style empty grades; sizeof must collapse.
using TaggedConsistency =
    Tagged<Consistency<Consistency_v::STRONG, int>, VerificationTag>;
static_assert(sizeof(TaggedConsistency) == sizeof(int));

// Consistency<Level, NumericalTier<T_at, T>> — consistency over
// recipe-tier (DOUBLE EBO collapse witness).  Production: a Forge
// Phase K BatchPolicy<TP-axis, STRONG> AnyAxis carrying a
// NumericalTier<BITEXACT, ResultTensor> — both wrappers regime-1,
// both grades empty, sizeof reduces all the way down to sizeof(T).
using ConsistencyOverNumerical =
    Consistency<Consistency_v::STRONG,
                NumericalTier<Tolerance::BITEXACT, int>>;
static_assert(sizeof(ConsistencyOverNumerical) == sizeof(int),
    "Consistency<...,NumericalTier<...,T>> must DOUBLE EBO-collapse "
    "to sizeof(T) — two regime-1 wrappers stacked, both with empty "
    "grade.");

// Consistency<Level, Linear<T>> — consistency-pinned linear ownership.
// Production: a Raft-replicated state shard owned linearly.  Asserts
// transitive copy-deletion preservation.
using ConsistencyOverLinear = Consistency<Consistency_v::STRONG, Linear<int>>;
static_assert(sizeof(ConsistencyOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<ConsistencyOverLinear>,
    "Consistency<Level, Linear<T>> must preserve Linear's move-only "
    "discipline.  If this fires, Linear's copy-deletion is no longer "
    "transitively visible through the Consistency wrapper.");
static_assert(std::is_move_constructible_v<ConsistencyOverLinear>);

// OpaqueLifetime<Scope, T> ⊕ {Tagged, NumericalTier, Linear} cells —
// parallel to the Consistency cross-composition coverage.
//
// Tagged<OpaqueLifetime<...>, Source> — provenance over scope pin.
// Production: a Cipher cold-tier write carries OpaqueLifetime<PER_FLEET>
// AND a Tagged<...,FromCipherWriter> source.
using TaggedOpaqueLifetime =
    Tagged<OpaqueLifetime<Lifetime_v::PER_FLEET, int>, VerificationTag>;
static_assert(sizeof(TaggedOpaqueLifetime) == sizeof(int));

// OpaqueLifetime<Scope, NumericalTier<Tier, T>> — scope-pinned recipe-
// tier value.  Production: a fleet-replicated parameter shard at
// BITEXACT tier — both regime-1 wrappers, both grades empty,
// DOUBLE EBO collapse.
using OpaqueLifetimeOverNumerical =
    OpaqueLifetime<Lifetime_v::PER_FLEET,
                   NumericalTier<Tolerance::BITEXACT, int>>;
static_assert(sizeof(OpaqueLifetimeOverNumerical) == sizeof(int));

// OpaqueLifetime<Scope, Linear<T>> — scope-pinned linear ownership.
// Production: a request-scoped linear handle (e.g. a per-request
// streaming cursor that consumes exactly once during the request).
using OpaqueLifetimeOverLinear =
    OpaqueLifetime<Lifetime_v::PER_REQUEST, Linear<int>>;
static_assert(sizeof(OpaqueLifetimeOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<OpaqueLifetimeOverLinear>);
static_assert(std::is_move_constructible_v<OpaqueLifetimeOverLinear>);

// TRIPLE-NESTED witness — the universal-vocabulary claim made
// concrete.  Production: a fleet-replicated, strongly-consistent,
// bit-exact-tier parameter shard.  Three regime-1 wrappers stacked
// over different lattices, all with empty grades; sizeof MUST
// reduce all the way to sizeof(T).  This cell is the load-bearing
// witness that the wrapper composition is actually compositional —
// not just at two layers but at any depth.
using TripleNested =
    OpaqueLifetime<Lifetime_v::PER_FLEET,
                   Consistency<Consistency_v::STRONG,
                               NumericalTier<Tolerance::BITEXACT, int>>>;
static_assert(sizeof(TripleNested) == sizeof(int),
    "TRIPLE-nested OpaqueLifetime<Consistency<NumericalTier<T>>> must "
    "EBO-collapse all the way to sizeof(T) — three regime-1 wrappers "
    "stacked over distinct lattices, all with empty grades.  If this "
    "fires, one of the three wrapper layers stopped using the EBO-"
    "friendly Graded substrate.");
static_assert(GradedWrapper<TripleNested>,
    "TRIPLE-nested wrapper must satisfy GradedWrapper at the outermost "
    "layer — proves the concept is compositional across distinct "
    "lattice types.");

// ── COVERAGE MATRIX — runtime API parity (smoke checks) ────────────
//
// Confirm peek / consume / construction round-trip for the four
// stateless wrappers.  Per-header self-tests already cover this in
// depth; this is a final confirmation that the consolidated TU sees
// the same behavior under the project's release-grade flags.

void runtime_smoke_linear() {
    Linear<int> x{42};
    if (x.peek() != 42) std::abort();
    int y = std::move(x).consume();
    if (y != 42) std::abort();
}

void runtime_smoke_refined() {
    Refined<positive_local, int> r{7};
    if (r.value() != 7) std::abort();
    int v = std::move(r).into();
    if (v != 7) std::abort();
}

void runtime_smoke_sealed_refined() {
    SealedRefined<positive_local, int> s{42};
    if (s.value() != 42) std::abort();
    // No into() — sealed.  The discipline IS the difference.

    // Round-trip: convert from a Refined into a SealedRefined.
    Refined<positive_local, int> r{7};
    SealedRefined<positive_local, int> sealed_from_r{std::move(r)};
    if (sealed_from_r.value() != 7) std::abort();
}

void runtime_smoke_tagged() {
    Tagged<int, VerificationTag> t{99};
    if (t.value() != 99) std::abort();
    int v = std::move(t).into();
    if (v != 99) std::abort();
}

void runtime_smoke_secret() {
    Secret<int> s{0xCAFE};
    int v = std::move(s).declassify<struct VerificationPolicy>();
    if (v != 0xCAFE) std::abort();
}

void runtime_smoke_numerical_tier() {
    // Construct at BITEXACT, relax to FP16, then to RELAXED.  The
    // round-trip exercises the relax<LooserTier> requires-clause at
    // runtime + the const& and && overloads.
    NumericalTier<Tolerance::BITEXACT, int> bx{42};
    if (bx.peek() != 42) std::abort();

    auto fp16 = bx.relax<Tolerance::ULP_FP16>();           // const& form
    if (fp16.peek() != 42) std::abort();
    if (fp16.tier != Tolerance::ULP_FP16) std::abort();

    auto relaxed = std::move(fp16).relax<Tolerance::RELAXED>();   // && form
    if (relaxed.peek() != 42) std::abort();

    // satisfies<...> reads at runtime.
    static_assert(NumericalTier<Tolerance::BITEXACT, int>
                      ::satisfies<Tolerance::ULP_FP16>);
    static_assert(!NumericalTier<Tolerance::ULP_FP16, int>
                       ::satisfies<Tolerance::BITEXACT>);

    // peek_mut + swap.
    NumericalTier<Tolerance::BITEXACT, int> a{10};
    NumericalTier<Tolerance::BITEXACT, int> b{20};
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();
    a.swap(b);
    if (a.peek() != 20 || b.peek() != 100) std::abort();

    // operator== — same-tier comparison delegates to peek().
    NumericalTier<Tolerance::BITEXACT, int> eq_a{42};
    NumericalTier<Tolerance::BITEXACT, int> eq_b{42};
    NumericalTier<Tolerance::BITEXACT, int> eq_c{43};
    if (!(eq_a == eq_b)) std::abort();
    if (  eq_a == eq_c)  std::abort();

    // Cross-composition runtime — Tagged over NumericalTier.
    Tagged<NumericalTier<Tolerance::BITEXACT, int>, VerificationTag>
        tagged_bx{NumericalTier<Tolerance::BITEXACT, int>{77}};
    auto unwrapped_bx = std::move(tagged_bx).into();
    if (unwrapped_bx.peek() != 77) std::abort();
}

void runtime_smoke_consistency() {
    // Round-trip: STRONG → CAUSAL_PREFIX → EVENTUAL via relax.
    Consistency<Consistency_v::STRONG, int> strong{42};
    if (strong.peek() != 42) std::abort();

    auto causal = strong.relax<Consistency_v::CAUSAL_PREFIX>();
    if (causal.peek() != 42) std::abort();
    if (causal.level != Consistency_v::CAUSAL_PREFIX) std::abort();

    auto eventual = std::move(causal).relax<Consistency_v::EVENTUAL>();
    if (eventual.peek() != 42) std::abort();

    // satisfies<...> reads at runtime.
    static_assert(Consistency<Consistency_v::STRONG, int>
                      ::satisfies<Consistency_v::CAUSAL_PREFIX>);
    static_assert(!Consistency<Consistency_v::EVENTUAL, int>
                       ::satisfies<Consistency_v::STRONG>);

    // peek_mut + swap.
    Consistency<Consistency_v::STRONG, int> a{10};
    Consistency<Consistency_v::STRONG, int> b{20};
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();
    a.swap(b);
    if (a.peek() != 20 || b.peek() != 100) std::abort();

    // operator== — same-level comparison delegates to peek().
    Consistency<Consistency_v::STRONG, int> eq_a{42};
    Consistency<Consistency_v::STRONG, int> eq_b{42};
    Consistency<Consistency_v::STRONG, int> eq_c{43};
    if (!(eq_a == eq_b)) std::abort();
    if (  eq_a == eq_c)  std::abort();

    // DOUBLE-nested cross-composition: Consistency over NumericalTier.
    ConsistencyOverNumerical nested{NumericalTier<Tolerance::BITEXACT, int>{55}};
    auto inner_after_consume = std::move(nested).consume();
    if (inner_after_consume.peek() != 55) std::abort();
}

void runtime_smoke_opaque_lifetime() {
    // Round-trip: PER_FLEET → PER_PROGRAM → PER_REQUEST via relax.
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> fleet{42};
    if (fleet.peek() != 42) std::abort();

    auto program = fleet.relax<Lifetime_v::PER_PROGRAM>();
    if (program.peek() != 42) std::abort();
    if (program.scope != Lifetime_v::PER_PROGRAM) std::abort();

    auto request = std::move(program).relax<Lifetime_v::PER_REQUEST>();
    if (request.peek() != 42) std::abort();

    // satisfies<...> reads at runtime.
    static_assert(OpaqueLifetime<Lifetime_v::PER_FLEET, int>
                      ::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert(!OpaqueLifetime<Lifetime_v::PER_REQUEST, int>
                       ::satisfies<Lifetime_v::PER_FLEET>);

    // peek_mut + swap.
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> a{10};
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> b{20};
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();
    a.swap(b);
    if (a.peek() != 20 || b.peek() != 100) std::abort();

    // operator== — same-scope.
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> eq_a{42};
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> eq_b{42};
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> eq_c{43};
    if (!(eq_a == eq_b)) std::abort();
    if (  eq_a == eq_c)  std::abort();

    // TRIPLE-nested cross-composition runtime witness.
    TripleNested triple{
        Consistency<Consistency_v::STRONG,
                    NumericalTier<Tolerance::BITEXACT, int>>{
            NumericalTier<Tolerance::BITEXACT, int>{77}
        }
    };
    auto consistency_layer = std::move(triple).consume();      // peel OpaqueLifetime
    auto numerical_layer   = std::move(consistency_layer).consume();  // peel Consistency
    if (numerical_layer.peek() != 77) std::abort();
}

void runtime_smoke_monotonic() {
    Monotonic<std::uint64_t> m{10};
    if (m.get() != 10) std::abort();
    m.advance(20);
    if (m.get() != 20) std::abort();
    if (!m.try_advance(20)) std::abort();   // leq holds for equal
    if ( m.try_advance(15)) std::abort();   // backward fails
}

void runtime_smoke_append_only() {
    AppendOnly<int> a{};
    a.append(1);
    a.append(2);
    a.append(3);
    if (a.size() != 3) std::abort();
    if (a[0] != 1 || a[1] != 2 || a[2] != 3) std::abort();
}

// SharedPermission's runtime smoke goes via the Pool (the carrier).
// Constructs a Pool from a freshly-minted exclusive Permission, lends
// a Guard, extracts the SharedPermission proof token, lets the Guard
// drop, then upgrades back to exclusive.  Exercises the full façade
// migration loop in one self-contained block.
void runtime_smoke_shared_permission() {
    auto exclusive = permission_root_mint<VerificationTag>();
    SharedPermissionPool<VerificationTag> pool{std::move(exclusive)};

    auto guard1 = pool.lend();
    if (!guard1) std::abort();
    [[maybe_unused]] SharedPermission<VerificationTag> tok = guard1->token();
    if (pool.outstanding() != 1) std::abort();

    // Drop the guard — refcount returns to 0.
    guard1.reset();
    if (pool.outstanding() != 0) std::abort();

    // Upgrade succeeds because no shares are out.
    auto upgraded = pool.try_upgrade();
    if (!upgraded) std::abort();
    pool.deposit_exclusive(std::move(*upgraded));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_migration_verification:\n");

    runtime_smoke_linear();
    std::fprintf(stderr, "  linear:      OK\n");
    runtime_smoke_refined();
    std::fprintf(stderr, "  refined:           OK\n");
    runtime_smoke_sealed_refined();
    std::fprintf(stderr, "  sealed_refined:    OK\n");
    runtime_smoke_tagged();
    std::fprintf(stderr, "  tagged:      OK\n");
    runtime_smoke_secret();
    std::fprintf(stderr, "  secret:      OK\n");
    runtime_smoke_numerical_tier();
    std::fprintf(stderr, "  numerical_tier:    OK\n");
    runtime_smoke_consistency();
    std::fprintf(stderr, "  consistency:       OK\n");
    runtime_smoke_opaque_lifetime();
    std::fprintf(stderr, "  opaque_lifetime:   OK\n");
    runtime_smoke_monotonic();
    std::fprintf(stderr, "  monotonic:   OK\n");
    runtime_smoke_append_only();
    std::fprintf(stderr, "  append_only:       OK\n");
    runtime_smoke_shared_permission();
    std::fprintf(stderr, "  shared_permission: OK\n");

    std::fprintf(stderr, "\nALL PASSED — 13 migrated wrappers verified uniformly "
                         "(12 Graded-backed + 1 façade)\n");
    return EXIT_SUCCESS;
}
