// The unit of bench_core_fma at x86-64: fp::fma runs its exact form.  The
// build compiles this file with -march=x86-64.

#include "fma_bench_body.h"
#include "fma_bench_units.h"

namespace crucible::bench_fma {

double chain_of_fma_at_v1(::foundation::core::View<double const> triples) noexcept {
    return chain_of_fma_here(triples);
}

double sum_of_fma_at_v1(::foundation::core::View<double const> triples) noexcept { return sum_of_fma_here(triples); }

}  // namespace crucible::bench_fma
