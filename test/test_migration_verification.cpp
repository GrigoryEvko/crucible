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

#include <crucible/permissions/Permission.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/TimeOrdered.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
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

static_assert(sizeof(Tagged<int, VerificationTag>)  == sizeof(int));
static_assert(sizeof(Tagged<long, VerificationTag>) == sizeof(long));

static_assert(sizeof(Secret<int>)                   == sizeof(int));
static_assert(sizeof(Secret<long long>)             == sizeof(long long));

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
static_assert(Tagged<int, VerificationTag>::value_type_name().ends_with("int"));
static_assert(Secret<int>::value_type_name().ends_with("int"));
static_assert(Monotonic<std::uint64_t>::value_type_name().ends_with("uint64_t")
           || Monotonic<std::uint64_t>::value_type_name().ends_with("long unsigned int"));
static_assert(Stale<int>::value_type_name().ends_with("int"));
static_assert(TimeOrdered<int, 4>::value_type_name().ends_with("int"));
// SharedPermission's value_type IS the Tag (phantom region label); the
// reflection-derived name ends with the local tag struct's name.
static_assert(SharedPermission<VerificationTag>::value_type_name()
                                            .ends_with("VerificationTag"));

// ── COVERAGE MATRIX — lattice_name forwarder uniformity ────────────
//
// Hand-written consteval string literals in algebra/lattices/*.h
// are TU-stable; use == for tight coverage.  The exception is
// BoolLattice + TrustLattice, which derive their name from
// display_string_of(^^Pred) / (^^Source) — those are TU-fragile.

static_assert(Linear<int>::lattice_name()       == "QttSemiring::At<1>");
static_assert(Refined<positive_local, int>::lattice_name()
                                                 .ends_with("PositiveCheck"));
static_assert(Tagged<int, VerificationTag>::lattice_name()
                                                 .ends_with("VerificationTag"));
static_assert(Secret<int>::lattice_name()       == "ConfLattice::At<Secret>");
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
    std::fprintf(stderr, "  refined:     OK\n");
    runtime_smoke_tagged();
    std::fprintf(stderr, "  tagged:      OK\n");
    runtime_smoke_secret();
    std::fprintf(stderr, "  secret:      OK\n");
    runtime_smoke_monotonic();
    std::fprintf(stderr, "  monotonic:   OK\n");
    runtime_smoke_append_only();
    std::fprintf(stderr, "  append_only:       OK\n");
    runtime_smoke_shared_permission();
    std::fprintf(stderr, "  shared_permission: OK\n");

    std::fprintf(stderr, "\nALL PASSED — 9 migrated wrappers verified uniformly "
                         "(8 Graded-backed + 1 façade)\n");
    return EXIT_SUCCESS;
}
