// Microbench for crucible::sat::add_sat / sub_sat / mul_sat.
//
// Establishes that the P0543 polyfill compiles to the same conditional-
// move shape as hand-written clamps — one __builtin_*_overflow
// (CMP+CMOV on x86-64) plus a branchless select on the rare overflow
// path. Target: ≤1 ns/op for the common case.

#include <cstdint>
#include <cstdio>

#include <crucible/Saturate.h>

#include "bench_harness.h"

using crucible::sat::add_sat;
using crucible::sat::mul_sat;
using crucible::sat::sub_sat;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // All volatile so the compiler can't constant-fold. Declared in outer
    // scope so every IIFE-lambda body captures the same memory location.
    volatile uint32_t u_a = 0x1234'5678u;
    volatile uint32_t u_b = 0x0000'0100u;
    volatile int32_t  s_a = 0x1234'5678;
    volatile int32_t  s_b = 0x0000'0100;
    volatile uint64_t U_a = 0x1234'5678'9abc'def0ULL;
    volatile uint64_t U_b = 0x0000'0000'0000'0100ULL;
    volatile int64_t  S_a = 0x1234'5678'9abc'def0LL;
    volatile int64_t  S_b = 0x0000'0000'0000'0100LL;

    // Overflow-path operands — saturate every call. Slightly slower than
    // the common case because the [[unlikely]] branch fires, but the
    // cmov-based clamp stays branchless.
    volatile uint64_t big_a = ~uint64_t{0};
    volatile uint64_t big_b = ~uint64_t{0};
    volatile int32_t  imin  = INT32_MIN;
    volatile int32_t  neg1  = -1;

    // Arena-relevant shape: n × sizeof(T) with overflow-check. Mirrors
    // Arena::alloc_array — catch overflow before malloc.
    volatile size_t n      = 10'000;
    volatile size_t elt_sz = 168;

    std::printf("=== saturate ===\n\n");

    bench::Report reports[] = {
        bench::run("add_sat<u32>   common",   [&]{ auto r = add_sat(u_a, u_b);     bench::do_not_optimize(r); }),
        bench::run("sub_sat<u32>   common",   [&]{ auto r = sub_sat(u_a, u_b);     bench::do_not_optimize(r); }),
        bench::run("mul_sat<u32>   common",   [&]{ auto r = mul_sat(u_a, u_b);     bench::do_not_optimize(r); }),

        bench::run("add_sat<i32>   common",   [&]{ auto r = add_sat(s_a, s_b);     bench::do_not_optimize(r); }),
        bench::run("sub_sat<i32>   common",   [&]{ auto r = sub_sat(s_a, s_b);     bench::do_not_optimize(r); }),
        bench::run("mul_sat<i32>   common",   [&]{ auto r = mul_sat(s_a, s_b);     bench::do_not_optimize(r); }),

        bench::run("add_sat<u64>   common",   [&]{ auto r = add_sat(U_a, U_b);     bench::do_not_optimize(r); }),
        bench::run("mul_sat<u64>   common",   [&]{ auto r = mul_sat(U_a, U_b);     bench::do_not_optimize(r); }),
        bench::run("mul_sat<i64>   common",   [&]{ auto r = mul_sat(S_a, S_b);     bench::do_not_optimize(r); }),

        bench::run("add_sat<u64>   overflow", [&]{ auto r = add_sat(big_a, big_b); bench::do_not_optimize(r); }),
        bench::run("mul_sat<i32>   INT_MIN×-1", [&]{ auto r = mul_sat(imin, neg1); bench::do_not_optimize(r); }),
        bench::run("mul_sat<size_t> array-sz", [&]{ auto r = mul_sat(n, elt_sz);   bench::do_not_optimize(r); }),
    };

    bench::emit_reports_text(reports);

    // Overflow vs. common path for u64 add — should be distinguishable
    // (the rare branch fires in every sample), but typically within 10%.
    std::printf("\n=== compare ===\n");
    bench::compare(reports[6], reports[9]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
