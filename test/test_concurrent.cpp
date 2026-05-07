// Sentinel TU for include/crucible/effects/Concurrent.h.
//
// Per feedback_header_only_static_assert_blind_spot.md: header-only
// static_asserts inside Concurrent.h are only evaluated under the
// project's full warning + standard flags when SOMEONE includes the
// header from a TU that lands in the build graph.  This sentinel
// makes the inclusion explicit so the concurrent_row_self_test block
// is exercised by every default build.
//
// The runtime portion exercises the empty `ConcurrentRow` carrier
// type via runtime instantiation — there's no per-instance state to
// poke at since rows are pure type-level constructs, but constructing
// instances confirms the empty-base-optimization claim and the
// concept-gate accepts representative tag packs.
//
// GAPS-190.

#include <crucible/effects/Concurrent.h>
#include <crucible/effects/Resources.h>

#include "test_assert.h"

#include <cstdio>
#include <type_traits>

namespace eff = crucible::effects;

// ── Empty-base-optimization smoke test ──────────────────────────────
//
// ConcurrentRow holds only static-constexpr template parameters.  At
// runtime the row carries no fields, so sizeof must be 1 (empty
// struct floor) and the type must be trivially constructible.

static void test_concurrent_row_layout() {
    eff::ConcurrentRow<> empty{};
    eff::ConcurrentRow<eff::resource::SmBudget<32>> single{};
    eff::ConcurrentRow<eff::resource::SmBudget<32>,
                       eff::resource::NicQp<4>> pair{};

    static_assert(sizeof(empty)  == 1, "Empty row must be 1 byte (empty struct floor).");
    static_assert(sizeof(single) == 1);
    static_assert(sizeof(pair)   == 1);

    static_assert(std::is_empty_v<decltype(empty)>);
    static_assert(std::is_empty_v<decltype(single)>);
    static_assert(std::is_empty_v<decltype(pair)>);

    static_assert(std::is_trivially_default_constructible_v<decltype(empty)>);
    static_assert(std::is_trivially_copyable_v<decltype(empty)>);

    // Volatile barrier prevents the optimizer from constant-folding
    // the size readout via the static_asserts above and silently
    // masking a runtime layout divergence.
    volatile std::size_t s = sizeof(pair);
    assert(s == 1);

    std::printf("  test_concurrent_row_layout:           PASSED\n");
}

// ── Type-level sum smoke test ───────────────────────────────────────
//
// Most of the algebra is consteval-only and thus already pinned via
// in-header static_asserts.  This runtime portion validates that the
// resulting types can be reified at runtime (instantiated, sized,
// passed by value) — not just that the alias machinery resolves.

static void test_concurrent_row_sum_runtime() {
    using R1 = eff::ConcurrentRow<eff::resource::SmBudget<32>,
                                  eff::resource::NicQp<4>>;
    using R2 = eff::ConcurrentRow<eff::resource::SmBudget<64>,
                                  eff::resource::NicQp<2>>;
    using Sum = eff::concurrent_row_sum_t<R1, R2>;

    Sum s{};
    static_assert(sizeof(s) == 1);

    // Recovery of the per-kind sums via concurrent_row_value_v matches
    // the materialized canonical row.
    static_assert(eff::concurrent_row_value_v<eff::ResourceKind::Sm, Sum>
                  == 96);
    static_assert(eff::concurrent_row_value_v<eff::ResourceKind::NicQp, Sum>
                  == 6);

    // Volatile barrier: confirm the type-level sum's runtime
    // instantiation lives in this TU's section, not just in the
    // consteval evaluation context.
    volatile auto sm_total = eff::concurrent_row_value_v<
        eff::ResourceKind::Sm, Sum>;
    assert(sm_total == 96);

    std::printf("  test_concurrent_row_sum_runtime:      PASSED\n");
}

// ── Variadic N-way smoke test ───────────────────────────────────────

static void test_concurrent_row_n_way() {
    using R1 = eff::ConcurrentRow<eff::resource::SmBudget<10>>;
    using R2 = eff::ConcurrentRow<eff::resource::SmBudget<20>>;
    using R3 = eff::ConcurrentRow<eff::resource::SmBudget<30>>;
    using R4 = eff::ConcurrentRow<eff::resource::SmBudget<40>>;
    using Total = eff::concurrent_row_n_t<R1, R2, R3, R4>;

    static_assert(eff::concurrent_row_value_v<eff::ResourceKind::Sm, Total>
                  == 100);

    // 4-way scheduling that fits AND doesn't overflow on any kind.
    static_assert(eff::ConcurrentlySchedulable<R1, R2>);
    static_assert(eff::ConcurrentlySchedulable<R3, R4>);

    Total t{};
    static_assert(sizeof(t) == 1);
    volatile auto v = eff::concurrent_row_value_v<
        eff::ResourceKind::Sm, Total>;
    assert(v == 100);

    std::printf("  test_concurrent_row_n_way:            PASSED\n");
}

int main() {
    std::printf("test_concurrent: 3 groups\n");
    test_concurrent_row_layout();
    test_concurrent_row_sum_runtime();
    test_concurrent_row_n_way();
    std::printf("test_concurrent: 3 groups, all passed\n");
    return 0;
}
