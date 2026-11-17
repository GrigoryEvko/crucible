// The doors of bench_core_sort to the pinned x86-simd-sort of numpy
// (utils/pins.txt, the BSD 3-Clause license), at the x86-64 level of the
// host.  The static header picks the arm from the flags of the unit: the
// AVX-512 arm on a host with AVX512DQ and AVX512VL, else the AVX2 arm.
// bench_core_sort_simd_avx2.cpp gives the AVX2 arm on each host.

#include "bench_core_sort_references.h"

#include "x86simdsort-static-incl.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the pinned x86-simd-sort is the speed roof of the sorts of the base")

namespace bench_sort_references {

void sort_simd_host(float* first, std::size_t count) noexcept { x86simdsortStatic::qsort(first, count, false, false); }

void sort_simd_host(std::uint32_t* first, std::size_t count) noexcept {
    x86simdsortStatic::qsort(first, count, false, false);
}

void sort_simd_host(std::uint64_t* first, std::size_t count) noexcept {
    x86simdsortStatic::qsort(first, count, false, false);
}

}  // namespace bench_sort_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
