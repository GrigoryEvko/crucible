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
#include <crucible/safety/HotPath.h>

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
// Set-semantic dedup — fixy-H-19 runtime peer.  Duplicate-atom drift
// cannot fragment the federation cache: `Row<X, X> ≡ Row<X>` and
// `Row<Bg, IO, Bg, IO> ≡ Row<IO, Bg>` by hash.  The header pins this
// at compile time; the runtime peer defeats any consteval-only fast-
// path miscompile per the runtime-smoke-test discipline.
static void test_runtime_set_semantic_dedup() {
    using ce::Effect;
    using ce::Row;

    // Singleton dedup — every atom × twice collapses to once.
    volatile std::uint64_t single_alloc =
        cd::row_hash_of_v<Row<Effect::Alloc>>.raw();
    volatile std::uint64_t double_alloc =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::Alloc>>.raw();
    assert(single_alloc == double_alloc);

    volatile std::uint64_t single_io = cd::row_hash_of_v<Row<Effect::IO>>.raw();
    volatile std::uint64_t double_io =
        cd::row_hash_of_v<Row<Effect::IO, Effect::IO>>.raw();
    assert(single_io == double_io);

    volatile std::uint64_t triple_io =
        cd::row_hash_of_v<Row<Effect::IO, Effect::IO, Effect::IO>>.raw();
    assert(single_io == triple_io);

    // Pair + leading/trailing duplicate collapses to the pair.
    volatile std::uint64_t alloc_io_pair =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t alloc_io_left_dup =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t alloc_io_right_dup =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::IO>>.raw();
    assert(alloc_io_pair == alloc_io_left_dup);
    assert(alloc_io_pair == alloc_io_right_dup);

    // Interleaved duplicates collapse to canonical pair regardless
    // of declaration order before dedup.
    volatile std::uint64_t bg_io_pair =
        cd::row_hash_of_v<Row<Effect::IO, Effect::Bg>>.raw();
    volatile std::uint64_t bg_io_interleaved =
        cd::row_hash_of_v<Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::IO>>.raw();
    volatile std::uint64_t io_bg_interleaved =
        cd::row_hash_of_v<Row<Effect::IO, Effect::Bg, Effect::Bg, Effect::IO>>.raw();
    assert(bg_io_pair == bg_io_interleaved);
    assert(bg_io_pair == io_bg_interleaved);

    // 4-pack with 2 unique atoms — pins that cardinality_seed is fed
    // unique_count (2), not raw pack size (4).  Otherwise the seed
    // would differ from the canonical pair's seed.
    volatile std::uint64_t alloc_io_2x2 =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::Alloc,
                              Effect::IO,    Effect::IO>>.raw();
    assert(alloc_io_2x2 == alloc_io_pair);

    // Set-equivalent dedup must NOT collide with a strictly larger
    // set — `Row<IO, IO>` must still differ from `Row<IO, Bg>`.
    volatile std::uint64_t io_bg_canonical =
        cd::row_hash_of_v<Row<Effect::IO, Effect::Bg>>.raw();
    assert(double_io != io_bg_canonical);

    std::printf("  test_set_semantic_dedup:        PASSED\n");
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

    // ── FIXY-FOUND-053 — expanded Computation pins ────────────────
    //
    // Per-Effect singletons (Bg already pinned above) + nested
    // expansions covering EmptyRow-outer, asymmetric-reverse,
    // same-row, cross-row, and triple-nested compositions.  Mirrors
    // the static_assert pins in safety/diag/RowHashFold.h.

    sink = cd::row_hash_of_v<Computation<Row<Effect::Alloc>, int>>.raw();
    assert(sink == 0x058CA6EFB434D439ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::IO>, int>>.raw();
    assert(sink == 0xCCFE717213BBA49CULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Block>, int>>.raw();
    assert(sink == 0x6D28A236D0E146C7ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Init>, int>>.raw();
    assert(sink == 0x64EF4D0126C4A4E3ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Test>, int>>.raw();
    assert(sink == 0xF4060D16B464EFDEULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow,
        Computation<Row<Effect::Alloc>, int>>>.raw();
    assert(sink == 0x0BECBF75AD6D7A0CULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow,
        Computation<Row<Effect::Block>, int>>>.raw();
    assert(sink == 0x32894FE89819DEA1ULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow,
        Computation<Row<Effect::Bg>, int>>>.raw();
    assert(sink == 0xEDF6E609659BD93CULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow,
        Computation<Row<Effect::Init>, int>>>.raw();
    assert(sink == 0x93C6E9DAD4DDF07AULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow,
        Computation<Row<Effect::Test>, int>>>.raw();
    assert(sink == 0x792A21E2C4F20C13ULL);

    // Asymmetric reverse (combine_ids non-commutative witness).
    sink = cd::row_hash_of_v<Computation<Row<Effect::Bg>,
        Computation<EmptyRow, int>>>.raw();
    assert(sink == 0x40D0E7791202A526ULL);

    // Same-row nested — distinct from single-Bg.
    sink = cd::row_hash_of_v<Computation<Row<Effect::Bg>,
        Computation<Row<Effect::Bg>, int>>>.raw();
    assert(sink == 0xAFCB34F7B12A2F95ULL);

    // Cross-row nested.
    sink = cd::row_hash_of_v<Computation<Row<Effect::Alloc>,
        Computation<Row<Effect::IO>, int>>>.raw();
    assert(sink == 0xB25AFEA0CE322A7EULL);

    // Triple-nested chained fold.
    sink = cd::row_hash_of_v<Computation<Row<Effect::Alloc>,
        Computation<Row<Effect::IO>,
            Computation<Row<Effect::Block>, int>>>>.raw();
    assert(sink == 0xAC3F22322B23C1FEULL);

    std::printf("  test_federation_hash_pins:       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// fixy-A3-016 sentinel — pins BOTH order properties RowHashFold.h's
// doc-comment now distinguishes:
//
//   (i)  Computation-internal order (R-first, T-second):
//        `Computation<R, int>` differs from a hypothetical
//        `Computation<int, R>` because combine_ids is non-commutative.
//        We witness this through a row swap (which is the only legal
//        way to swap the two slots — bare `int` isn't a valid row
//        type, but two distinct Rows can be tried in either slot).
//
//   (ii) Wrapper-stack × Computation interleave order:
//        `HotPath<Hot, Computation<R, int>>` differs from
//        `Computation<R, HotPath<Hot, int>>` — wrapping the
//        Computation is NOT the same as wrapping the payload inside
//        the Computation.  This is the property a reader who
//        misreads the doc-comment would expect to be EQUAL; the
//        sentinel pins it INEQUAL, foreclosing that
//        misinterpretation.
//
// Both properties together imply the doc clarification is load-
// bearing: a refactor that flattens the distinction between
// "wrapper outside Computation" and "Computation outside wrapper"
// would break federation cache slot assignment for every wrapped
// row-typed carrier.
static void test_runtime_wrapper_computation_interleave_order() {
    using ce::Effect;
    using ce::EmptyRow;
    using ce::Row;
    using ce::Computation;
    using crucible::safety::HotPath;
    using crucible::safety::HotPathTier_v;

    // (i) Within Computation, swapping the two type slots produces a
    // different hash because combine_ids is non-commutative.  Use two
    // structurally-distinct row types to legally fill both slots —
    // bare integral types in slot 1 would not be Row-shaped.
    using R_A = Row<Effect::Alloc>;
    using R_B = Row<Effect::IO>;

    volatile std::uint64_t sink_AB =
        cd::row_hash_of_v<Computation<R_A, R_B>>.raw();
    volatile std::uint64_t sink_BA =
        cd::row_hash_of_v<Computation<R_B, R_A>>.raw();

    // Distinct rows in distinct slots ⇒ distinct combine_ids output.
    // (Both Rows non-zero, combine_ids non-commutative.)
    assert(sink_AB != sink_BA);

    // (ii) Wrapper outside vs. inside Computation produces different
    // hashes — this is the load-bearing pin against the doc-comment
    // misreading.  Both expressions touch the same atom set; only the
    // nesting differs.
    volatile std::uint64_t sink_wrap_outside =
        cd::row_hash_of_v<HotPath<HotPathTier_v::Hot,
                                  Computation<Row<Effect::Bg>, int>>>.raw();
    volatile std::uint64_t sink_wrap_inside =
        cd::row_hash_of_v<Computation<Row<Effect::Bg>,
                                      HotPath<HotPathTier_v::Hot, int>>>.raw();
    assert(sink_wrap_outside != sink_wrap_inside);

    // Both must also differ from the bare-Computation baseline (the
    // wrapper contribution flows in regardless of nesting position).
    volatile std::uint64_t sink_bare_comp =
        cd::row_hash_of_v<Computation<Row<Effect::Bg>, int>>.raw();
    assert(sink_wrap_outside != sink_bare_comp);
    assert(sink_wrap_inside  != sink_bare_comp);

    std::printf("  test_wrapper_computation_interleave_order: PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
int main() {
    test_runtime_permutation_invariance();
    test_runtime_cardinality_discrimination();
    test_runtime_set_semantic_dedup();
    test_runtime_bare_types_zero();
    test_runtime_empty_row_distinct_from_bare();
    test_runtime_determinism();
    test_runtime_computation_specialization();
    test_runtime_federation_hash_pins();
    test_runtime_wrapper_computation_interleave_order();
    std::printf("test_row_hash_fold: 9 groups, all passed\n");
    return 0;
}
