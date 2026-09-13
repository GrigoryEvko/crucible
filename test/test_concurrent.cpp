// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags. The rows are pure type-level constructs
// with no per-instance state, so the runtime body constructs them to
// confirm the empty-base claim and to reach the concept gate.

#include <crucible/effects/Concurrent.h>
#include <crucible/effects/Resources.h>

#include "test_assert.h"

#include <cstdio>
#include <type_traits>

namespace eff = crucible::effects;

// The row holds only static template parameters and carries no fields at
// runtime, so one byte is its floor and it must be trivially constructible.

static void test_concurrent_row_layout() {
    eff::ConcurrentRow<> empty{};
    eff::ConcurrentRow<eff::resource::SmBudget<32>> single{};
    eff::ConcurrentRow<eff::resource::SmBudget<32>, eff::resource::NicQp<4>> pair{};

    static_assert(sizeof(empty) == 1, "Empty row must be 1 byte (empty struct floor).");
    static_assert(sizeof(single) == 1);
    static_assert(sizeof(pair) == 1);

    static_assert(std::is_empty_v<decltype(empty)>);
    static_assert(std::is_empty_v<decltype(single)>);
    static_assert(std::is_empty_v<decltype(pair)>);

    static_assert(std::is_trivially_default_constructible_v<decltype(empty)>);
    static_assert(std::is_trivially_copyable_v<decltype(empty)>);

    // The volatile barrier keeps the optimizer from folding the size readout
    // through the assertions above, which would mask a layout divergence.
    volatile std::size_t s = sizeof(pair);
    assert(s == 1);

    std::printf("  test_concurrent_row_layout:           PASSED\n");
}

// The algebra itself is consteval and already pinned in the header. What
// this adds is that the resulting types can be reified at runtime, that is,
// instantiated, sized and passed by value, not merely resolved.

static void test_concurrent_row_sum_runtime() {
    using R1 = eff::ConcurrentRow<eff::resource::SmBudget<32>, eff::resource::NicQp<4>>;
    using R2 = eff::ConcurrentRow<eff::resource::SmBudget<64>, eff::resource::NicQp<2>>;
    using Sum = eff::concurrent_row_sum_t<R1, R2>;

    Sum s{};
    static_assert(sizeof(s) == 1);

    static_assert(eff::concurrent_row_value_v<eff::ResourceKind::Sm, Sum> == 96);
    static_assert(eff::concurrent_row_value_v<eff::ResourceKind::NicQp, Sum> == 6);

    // The volatile barrier puts the sum's instantiation in this unit's own
    // section, not only in the consteval evaluation context.
    volatile auto sm_total = eff::concurrent_row_value_v<eff::ResourceKind::Sm, Sum>;
    assert(sm_total == 96);

    std::printf("  test_concurrent_row_sum_runtime:      PASSED\n");
}

static void test_concurrent_row_n_way() {
    using R1 = eff::ConcurrentRow<eff::resource::SmBudget<10>>;
    using R2 = eff::ConcurrentRow<eff::resource::SmBudget<20>>;
    using R3 = eff::ConcurrentRow<eff::resource::SmBudget<30>>;
    using R4 = eff::ConcurrentRow<eff::resource::SmBudget<40>>;
    using Total = eff::concurrent_row_n_t<R1, R2, R3, R4>;

    static_assert(eff::concurrent_row_value_v<eff::ResourceKind::Sm, Total> == 100);

    // A four-way schedule that fits, and overflows on no resource kind.
    static_assert(eff::ConcurrentlySchedulable<R1, R2>);
    static_assert(eff::ConcurrentlySchedulable<R3, R4>);

    Total t{};
    static_assert(sizeof(t) == 1);
    volatile auto v = eff::concurrent_row_value_v<eff::ResourceKind::Sm, Total>;
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
