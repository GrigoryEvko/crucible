// Microbench for the saturating forms of the Scalar family and of fixy::sat.
//
// saturating_add, saturating_sub and saturating_mul come from
// foundation/core/Arithmetic.h through fixy/Core.h. Each one gives the
// exact result when no overflow occurs, and the nearest limit of the type
// on overflow. The *_sat_checked forms of fixy/Saturated.h give the result
// in a Saturated<T>, and the *_sat_into forms step a Saturated counter.

#include <cstdint>

#include <fixy/Core.h>
#include <fixy/Saturate.h>

#include "bench_harness.h"

using fixy::add_sat_checked;
using fixy::mul_sat_checked;
using fixy::saturating_add;
using fixy::saturating_mul;
using fixy::saturating_sub;
using fixy::sub_sat_checked;
using fixy::sat::add_sat_into;
using fixy::sat::mul_sat_into;
using fixy::sat::sub_sat_into;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    // All volatile so the compiler can't constant-fold. Declared in outer
    // scope so every IIFE-lambda body captures the same memory location.
    volatile uint32_t u_a = 0x12345678u;
    volatile uint32_t u_b = 0x00000100u;
    volatile int32_t s_a = 0x12345678;
    volatile int32_t s_b = 0x00000100;
    volatile uint64_t U_a = 0x123456789abcdef0ULL;
    volatile uint64_t U_b = 0x0000000000000100ULL;
    volatile int64_t S_a = 0x123456789abcdef0LL;
    volatile int64_t S_b = 0x0000000000000100LL;

    // Overflow-path operands. Each call saturates, so each call takes the
    // clamp path of the helper.
    volatile uint64_t big_a = ~uint64_t{0};
    volatile uint64_t big_b = ~uint64_t{0};
    volatile int32_t imin = INT32_MIN;
    volatile int32_t neg1 = -1;

    // The shape of an array size: n × sizeof(T) with an overflow check.
    volatile size_t n = 10000;
    volatile size_t elt_sz = 168;

    auto counter_add = fixy::Saturated<uint32_t>::exact(0x12340000u);
    auto counter_sub = fixy::Saturated<uint32_t>::exact(0x12340000u);
    auto counter_mul = fixy::Saturated<uint32_t>::exact(0x00001000u);

    bench::print("=== saturate ===\n\n");

    ::fixy::FixedArray<bench::Report, 18> reports{
        bench::run("add_sat<u32>   common",
                   [&] {
                       auto r = saturating_add(u_a, u_b);
                       bench::do_not_optimize(r);
                   }),
        bench::run("sub_sat<u32>   common",
                   [&] {
                       auto r = saturating_sub(u_a, u_b);
                       bench::do_not_optimize(r);
                   }),
        bench::run("mul_sat<u32>   common",
                   [&] {
                       auto r = saturating_mul(u_a, u_b);
                       bench::do_not_optimize(r);
                   }),

        bench::run("add_sat<i32>   common",
                   [&] {
                       auto r = saturating_add(s_a, s_b);
                       bench::do_not_optimize(r);
                   }),
        bench::run("sub_sat<i32>   common",
                   [&] {
                       auto r = saturating_sub(s_a, s_b);
                       bench::do_not_optimize(r);
                   }),
        bench::run("mul_sat<i32>   common",
                   [&] {
                       auto r = saturating_mul(s_a, s_b);
                       bench::do_not_optimize(r);
                   }),

        bench::run("add_sat<u64>   common",
                   [&] {
                       auto r = saturating_add(U_a, U_b);
                       bench::do_not_optimize(r);
                   }),
        bench::run("mul_sat<u64>   common",
                   [&] {
                       auto r = saturating_mul(U_a, U_b);
                       bench::do_not_optimize(r);
                   }),
        bench::run("mul_sat<i64>   common",
                   [&] {
                       auto r = saturating_mul(S_a, S_b);
                       bench::do_not_optimize(r);
                   }),

        bench::run("add_sat<u64>   overflow",
                   [&] {
                       auto r = saturating_add(big_a, big_b);
                       bench::do_not_optimize(r);
                   }),
        bench::run("mul_sat<i32>   INT_MIN×-1",
                   [&] {
                       auto r = saturating_mul(imin, neg1);
                       bench::do_not_optimize(r);
                   }),
        bench::run("mul_sat<size_t> array-sz",
                   [&] {
                       auto r = saturating_mul(n, elt_sz);
                       bench::do_not_optimize(r);
                   }),

        bench::run("add_sat_checked<u32> counter",
                   [&] {
                       auto r = add_sat_checked(counter_add.value(), uint32_t{u_b});
                       bench::do_not_optimize(r);
                   }),
        bench::run("sub_sat_checked<u32> counter",
                   [&] {
                       auto r = sub_sat_checked(counter_sub.value(), uint32_t{u_b});
                       bench::do_not_optimize(r);
                   }),
        bench::run("mul_sat_checked<u32> counter",
                   [&] {
                       auto r = mul_sat_checked(counter_mul.value(), uint32_t{3});
                       bench::do_not_optimize(r);
                   }),

        bench::run("add_sat_into<u32> counter",
                   [&] {
                       add_sat_into(counter_add, uint32_t{1});
                       bench::do_not_optimize(counter_add);
                   }),
        bench::run("sub_sat_into<u32> counter",
                   [&] {
                       sub_sat_into(counter_sub, uint32_t{1});
                       bench::do_not_optimize(counter_sub);
                   }),
        bench::run("mul_sat_into<u32> counter",
                   [&] {
                       mul_sat_into(counter_mul, uint32_t{1});
                       bench::do_not_optimize(counter_mul);
                   }),
    };

    bench::emit_reports_text(reports.view());

    // Overflow vs. common path for u64 add — should be distinguishable
    // (the rare branch fires in every sample), but typically within 10%.
    bench::print("\n=== compare ===\n");
    bench::compare(reports.at(::fixy::index<6>()), reports.at(::fixy::index<9>())).print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
