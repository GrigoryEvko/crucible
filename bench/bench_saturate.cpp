// Microbench for crucible::sat::add_sat / sub_sat / mul_sat.
//
// Establishes that the P0543 polyfill compiles to the same
// conditional-move shape as hand-written clamps — one __builtin_*_
// overflow (CMP+CMOV on x86-64) plus a branchless select on the
// rare overflow path.  Target: ≤1 ns/op for the common case.

#include <crucible/Saturate.h>
#include <cstdint>
#include <cstdio>

#include "bench_harness.h"

using crucible::sat::add_sat;
using crucible::sat::sub_sat;
using crucible::sat::mul_sat;

int main() {
    std::printf("bench_saturate:\n");

    // Common case: random-ish operands that rarely overflow.
    // Rotating indices keep the compiler from folding everything.
    volatile uint32_t u_a = 0x1234'5678u;
    volatile uint32_t u_b = 0x0000'0100u;
    volatile int32_t  s_a = 0x1234'5678;
    volatile int32_t  s_b = 0x0000'0100;
    volatile uint64_t U_a = 0x1234'5678'9abc'def0ULL;
    volatile uint64_t U_b = 0x0000'0000'0000'0100ULL;
    volatile int64_t  S_a = 0x1234'5678'9abc'def0LL;
    volatile int64_t  S_b = 0x0000'0000'0000'0100LL;

    BENCH_CHECK("add_sat<u32> common", 10'000'000, 3.0, {
        auto r = add_sat(u_a, u_b);
        bench::DoNotOptimize(r);
    });
    BENCH_CHECK("sub_sat<u32> common", 10'000'000, 3.0, {
        auto r = sub_sat(u_a, u_b);
        bench::DoNotOptimize(r);
    });
    BENCH_CHECK("mul_sat<u32> common", 10'000'000, 3.0, {
        auto r = mul_sat(u_a, u_b);
        bench::DoNotOptimize(r);
    });

    BENCH_CHECK("add_sat<i32> common", 10'000'000, 3.0, {
        auto r = add_sat(s_a, s_b);
        bench::DoNotOptimize(r);
    });
    BENCH_CHECK("sub_sat<i32> common", 10'000'000, 3.0, {
        auto r = sub_sat(s_a, s_b);
        bench::DoNotOptimize(r);
    });
    BENCH_CHECK("mul_sat<i32> common", 10'000'000, 3.0, {
        auto r = mul_sat(s_a, s_b);
        bench::DoNotOptimize(r);
    });

    BENCH_CHECK("add_sat<u64> common", 10'000'000, 3.0, {
        auto r = add_sat(U_a, U_b);
        bench::DoNotOptimize(r);
    });
    BENCH_CHECK("mul_sat<u64> common", 10'000'000, 3.0, {
        auto r = mul_sat(U_a, U_b);
        bench::DoNotOptimize(r);
    });

    BENCH_CHECK("mul_sat<i64> common", 10'000'000, 3.0, {
        auto r = mul_sat(S_a, S_b);
        bench::DoNotOptimize(r);
    });

    // Overflow path — both saturate every call.  Slightly slower
    // than the common case because the [[unlikely]] branch fires,
    // but the cmov-based clamp stays branchless.
    volatile uint64_t big_a = ~uint64_t{0};
    volatile uint64_t big_b = ~uint64_t{0};
    volatile int32_t  imin  = INT32_MIN;
    volatile int32_t  neg1  = -1;

    BENCH_CHECK("add_sat<u64> overflow", 10'000'000, 3.5, {
        auto r = add_sat(big_a, big_b);
        bench::DoNotOptimize(r);
    });
    BENCH_CHECK("mul_sat<i32> INT_MIN*-1", 10'000'000, 3.5, {
        auto r = mul_sat(imin, neg1);
        bench::DoNotOptimize(r);
    });

    // Arena-relevant shape: saturating size math (n × sizeof(T)).
    // Mirrors Arena::alloc_array — catch overflow before malloc.
    volatile size_t n       = 10'000;
    volatile size_t elt_sz  = 168;
    BENCH_CHECK("mul_sat<size_t> array", 10'000'000, 3.0, {
        auto r = mul_sat(n, elt_sz);
        bench::DoNotOptimize(r);
    });

    std::printf("bench_saturate: ok\n");
    return 0;
}
