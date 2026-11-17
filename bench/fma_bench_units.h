#pragma once

// The units of bench_core_fma.  The build compiles fma_bench_v1.cpp for
// x86-64 and fma_bench_v2.cpp for x86-64-v2, where fp::fma runs its exact
// form in integers, an out-of-line call for each fma.  The main unit of the
// bench compiles for the host, where fp::fma is one instruction.  fp::fma and
// its exact form are in the inline namespace of the target, so each unit
// calls the body of its own level.
//
// Each function takes a run of values and reads it in triples (left, right,
// addend).  The chain feeds each result into the addend of the next fma, so
// the latency of one fma bounds it.  The sum adds the results of fmas that do
// not depend on each other, so the throughput bounds it.

#include <foundation/core/Region.h>

namespace crucible::bench_fma {

// The chain: acc = fma(left, right, acc) for each triple, from the first
// addend.
[[nodiscard]] double chain_of_fma_at_v1(::foundation::core::View<double const> triples) noexcept;
[[nodiscard]] double chain_of_fma_at_v2(::foundation::core::View<double const> triples) noexcept;
[[nodiscard]] double chain_of_fma_at_host(::foundation::core::View<double const> triples) noexcept;

// The sum of fma(left, right, addend) of each triple.
[[nodiscard]] double sum_of_fma_at_v1(::foundation::core::View<double const> triples) noexcept;
[[nodiscard]] double sum_of_fma_at_v2(::foundation::core::View<double const> triples) noexcept;
[[nodiscard]] double sum_of_fma_at_host(::foundation::core::View<double const> triples) noexcept;

}  // namespace crucible::bench_fma
