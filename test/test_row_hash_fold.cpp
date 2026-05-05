// FOUND-I02: RowHash recursive fmix64 fold over wrapper stack.
//
// Sentinel TU for `safety/diag/RowHashFold.h` — closes the
// header-only static_assert blind spot (feedback memory) by ensuring
// every embedded compile-time check runs under the project's full
// warning matrix at least once.  Runtime peer checks live here rather
// than in the production header.
//
// The volatile-sink discipline below defeats constant-folding so the
// optimizer cannot collapse these to compile-time-only checks: every
// raw u64 result must materialize through a volatile store.

#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/Types.h>
#include <crucible/effects/Computation.h>

#include "test_assert.h"

#include <cstdio>
#include <cstdint>

namespace ce = crucible::effects;
namespace cd = crucible::safety::diag;
using crucible::RowHash;

// ─────────────────────────────────────────────────────────────────────
// Permutation invariance — every pack permutation yields the same
// row hash.  The header asserts this at compile time; the runtime
// peer ensures the fold isn't a consteval-only fast-path masking a
// runtime miscompile.
static void test_runtime_permutation_invariance() {
    using ce::Effect;
    using ce::Row;

    // 2-way: every pair permutation hashes identically.
    volatile std::uint64_t sink_ai =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t sink_ia =
        cd::row_hash_of_v<Row<Effect::IO, Effect::Alloc>>.raw();
    assert(sink_ai == sink_ia);

    volatile std::uint64_t sink_bg =
        cd::row_hash_of_v<Row<Effect::Block, Effect::Bg>>.raw();
    volatile std::uint64_t sink_gb =
        cd::row_hash_of_v<Row<Effect::Bg, Effect::Block>>.raw();
    assert(sink_bg == sink_gb);

    // 3-way: triplet permutations hash identically.
    volatile std::uint64_t sink_aib =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>.raw();
    volatile std::uint64_t sink_bia =
        cd::row_hash_of_v<Row<Effect::Block, Effect::IO, Effect::Alloc>>.raw();
    volatile std::uint64_t sink_iba =
        cd::row_hash_of_v<Row<Effect::IO, Effect::Block, Effect::Alloc>>.raw();
    assert(sink_aib == sink_bia);
    assert(sink_aib == sink_iba);
    assert(sink_bia == sink_iba);

    std::printf("  test_permutation_invariance:    PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Cardinality discrimination — adding a single effect must always
// change the row hash.  The cache cannot collapse `Row<Alloc>` and
// `Row<Alloc, IO>` into the same slot; doing so silently breaks the
// federation contract because Row<Alloc> ⊊ Row<Alloc, IO>.
static void test_runtime_cardinality_discrimination() {
    using ce::Effect;
    using ce::Row;

    volatile std::uint64_t h0 = cd::row_hash_of_v<Row<>>.raw();
    volatile std::uint64_t h1 = cd::row_hash_of_v<Row<Effect::Alloc>>.raw();
    volatile std::uint64_t h2 =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t h3 =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>.raw();
    volatile std::uint64_t h6 = cd::row_hash_of_v<
        Row<Effect::Alloc, Effect::IO, Effect::Block,
            Effect::Bg,    Effect::Init, Effect::Test>>.raw();

    assert(h0 != h1);
    assert(h1 != h2);
    assert(h2 != h3);
    assert(h3 != h6);
    assert(h0 != h6);

    // None of these are zero (bare-type sentinel) or UINT64_MAX
    // (KernelCache EMPTY-slot marker).  Real row hashes occupy the
    // interior of the 64-bit space.
    assert(h0 != 0);
    assert(h1 != 0);
    assert(h6 != 0);
    assert(h0 != static_cast<std::uint64_t>(-1));
    assert(h6 != static_cast<std::uint64_t>(-1));

    std::printf("  test_cardinality:               PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Bare types contribute zero — confirms the row_hash_contribution
// primary template fires for non-row-bearing T, and that the result
// flows through `row_hash_of_v` as `RowHash{0}`.
static void test_runtime_bare_types_zero() {
    volatile std::uint64_t h_int    = cd::row_hash_of_v<int>.raw();
    volatile std::uint64_t h_float  = cd::row_hash_of_v<float>.raw();
    volatile std::uint64_t h_double = cd::row_hash_of_v<double>.raw();
    volatile std::uint64_t h_void   = cd::row_hash_of_v<void>.raw();

    assert(h_int    == 0);
    assert(h_float  == 0);
    assert(h_double == 0);
    assert(h_void   == 0);

    // Bare-type RowHash is the default sentinel value (zero) but NOT
    // the EMPTY-slot sentinel (UINT64_MAX).  These are semantically
    // distinct cache states.
    auto rh_int = cd::row_hash_of_v<int>;
    assert(!rh_int.is_sentinel());
    assert(rh_int == RowHash{});

    std::printf("  test_bare_types_zero:           PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// EmptyRow vs bare-type discrimination — semantically critical.
// `Computation<EmptyRow, T>` is a row-typed Met(X) carrier; bare T
// is just a payload.  The cache must distinguish them.
static void test_runtime_empty_row_distinct_from_bare() {
    volatile std::uint64_t h_empty_row = cd::row_hash_of_v<ce::EmptyRow>.raw();
    volatile std::uint64_t h_bare      = cd::row_hash_of_v<int>.raw();

    assert(h_empty_row != h_bare);
    assert(h_empty_row != 0);
    assert(h_bare      == 0);

    // EmptyRow hash is the published constant; pin it so any change
    // to the seed strategy is caught at runtime as well as compile
    // time.
    assert(h_empty_row == cd::detail::EMPTY_ROW_HASH);

    std::printf("  test_empty_row_distinct:        PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Determinism — repeated calls must produce identical hashes
// (exercises the `inline constexpr` storage of the variable
// template).  Different invocation contexts also yield the same
// hash; the volatile sink defeats hoisting.
static void test_runtime_determinism() {
    using ce::Effect;
    using ce::Row;

    auto get_hash = []() noexcept -> std::uint64_t {
        return cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    };

    volatile std::uint64_t a = get_hash();
    volatile std::uint64_t b = get_hash();
    volatile std::uint64_t c = get_hash();
    assert(a == b);
    assert(b == c);
    assert(a == c);

    std::printf("  test_determinism:               PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Computation<R, T> runtime peer — closes the audit blind spot
// (FOUND-I02-AUDIT, 2026-04-30).  Exercises the four invariants the
// header's static_asserts pin:
//   (a) distinct from bare T
//   (b) distinct from bare row
//   (c) payload-blind for bare T
//   (d) row-discriminating
//   (e) permutation invariance lifts through carrier
//   (f) cardinality discrimination lifts through carrier
//   (g) nested Computation contributes inner row
static void test_runtime_computation_specialization() {
    using ce::Effect;
    using ce::EmptyRow;
    using ce::Row;
    using ce::Computation;

    // (a) Computation distinct from bare T.
    volatile std::uint64_t sink_comp_int =
        cd::row_hash_of_v<Computation<EmptyRow, int>>.raw();
    volatile std::uint64_t sink_int = cd::row_hash_of_v<int>.raw();
    assert(sink_comp_int != sink_int);
    assert(sink_comp_int != 0);

    // (b) Computation<EmptyRow, int> distinct from bare EmptyRow row.
    volatile std::uint64_t sink_empty_row = cd::row_hash_of_v<EmptyRow>.raw();
    assert(sink_comp_int != sink_empty_row);

    // (c) Payload-blind: Computation<EmptyRow, int> ==
    // Computation<EmptyRow, double>.
    volatile std::uint64_t sink_comp_double =
        cd::row_hash_of_v<Computation<EmptyRow, double>>.raw();
    assert(sink_comp_int == sink_comp_double);

    volatile std::uint64_t sink_comp_alloc_int =
        cd::row_hash_of_v<Computation<Row<Effect::Alloc>, int>>.raw();
    volatile std::uint64_t sink_comp_alloc_char =
        cd::row_hash_of_v<Computation<Row<Effect::Alloc>, char>>.raw();
    assert(sink_comp_alloc_int == sink_comp_alloc_char);

    // (d) Row-discriminating: Alloc-row carrier != IO-row carrier.
    volatile std::uint64_t sink_comp_io_int =
        cd::row_hash_of_v<Computation<Row<Effect::IO>, int>>.raw();
    assert(sink_comp_alloc_int != sink_comp_io_int);
    assert(sink_comp_alloc_int != sink_comp_int);  // vs EmptyRow carrier

    // (e) Permutation invariance lifts through Computation.
    volatile std::uint64_t sink_comp_alloc_io =
        cd::row_hash_of_v<Computation<Row<Effect::Alloc, Effect::IO>, int>>.raw();
    volatile std::uint64_t sink_comp_io_alloc =
        cd::row_hash_of_v<Computation<Row<Effect::IO, Effect::Alloc>, int>>.raw();
    assert(sink_comp_alloc_io == sink_comp_io_alloc);

    // (f) Cardinality discrimination lifts through Computation.
    assert(sink_comp_alloc_int != sink_comp_alloc_io);

    // (g) Nested Computation: inner row participates in outer hash.
    volatile std::uint64_t sink_nested =
        cd::row_hash_of_v<
            Computation<EmptyRow, Computation<Row<Effect::IO>, int>>>.raw();
    assert(sink_nested != sink_comp_int);          // != flat EmptyRow carrier
    assert(sink_nested != sink_comp_io_int);       // != flat IO-row carrier

    // No accidental sentinel collision.
    assert(sink_comp_int != static_cast<std::uint64_t>(-1));
    assert(sink_comp_alloc_int != static_cast<std::uint64_t>(-1));
    assert(sink_nested != static_cast<std::uint64_t>(-1));

    std::printf("  test_computation_specialization: PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Federation hash-stability runtime peer (FOUND-I04).  The header's
// static_asserts pin the canonical row hashes at compile time; this
// test exercises the same pinning discipline through runtime sinks
// per feedback_algebra_runtime_smoke_test_discipline.  The hex
// literals MUST match the static_assert pins in
// `safety/diag/RowHashFold.h` exactly — drift between the two would
// indicate a consteval-only specialization that differs at runtime,
// the exact bug class the runtime smoke test discipline catches.
static void test_runtime_federation_hash_pins() {
    using ce::Effect;
    using ce::EmptyRow;
    using ce::Row;

    volatile std::uint64_t sink;

    sink = cd::row_hash_of_v<EmptyRow>.raw();
    assert(sink == 0xEFD01F60BA992926ULL);

    sink = cd::row_hash_of_v<Row<Effect::Alloc>>.raw();
    assert(sink == 0x436DAF9EDCB565C3ULL);

    sink = cd::row_hash_of_v<Row<Effect::IO>>.raw();
    assert(sink == 0x6FBFD0F707B63BECULL);

    sink = cd::row_hash_of_v<Row<Effect::Block>>.raw();
    assert(sink == 0x3117F06B828C9247ULL);

    sink = cd::row_hash_of_v<Row<Effect::Bg>>.raw();
    assert(sink == 0x008A519814C8FC81ULL);

    sink = cd::row_hash_of_v<Row<Effect::Init>>.raw();
    assert(sink == 0x9E23FC5AC81DA675ULL);

    sink = cd::row_hash_of_v<Row<Effect::Test>>.raw();
    assert(sink == 0x26A9EB08E748D58FULL);

    sink = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    assert(sink == 0x6CC046F52E6D7663ULL);

    sink = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO,
                Effect::Block, Effect::Bg, Effect::Init,
                Effect::Test>>.raw();
    assert(sink == 0x1C9D0E4F548FAAD6ULL);

    // Computation<R, T> pins — detect drift in combine_ids + the
    // Computation specialization (FOUND-I02-AUDIT).
    using ce::Computation;

    sink = cd::row_hash_of_v<Computation<EmptyRow, int>>.raw();
    assert(sink == 0x49A55BE1CFC23FB0ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Bg>, int>>.raw();
    assert(sink == 0x3ACE35615F0F9243ULL);

    sink = cd::row_hash_of_v<Computation<
        Row<Effect::Alloc, Effect::IO>, int>>.raw();
    assert(sink == 0x83D432DE6CDEACA7ULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow,
        Computation<Row<Effect::IO>, int>>>.raw();
    assert(sink == 0x94EC56B861A6B8FDULL);

    std::printf("  test_federation_hash_pins:       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
int main() {
    test_runtime_permutation_invariance();
    test_runtime_cardinality_discrimination();
    test_runtime_bare_types_zero();
    test_runtime_empty_row_distinct_from_bare();
    test_runtime_determinism();
    test_runtime_computation_specialization();
    test_runtime_federation_hash_pins();
    std::printf("test_row_hash_fold: 7 groups, all passed\n");
    return 0;
}
