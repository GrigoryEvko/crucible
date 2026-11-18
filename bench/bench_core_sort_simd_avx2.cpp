// The doors of bench_core_sort to the pinned x86-simd-sort of numpy
// (utils/pins.txt, the BSD 3-Clause license), at the x86-64 level v3.
// bench/CMakeLists.txt compiles this unit with -march=x86-64-v3, so the
// static header picks the AVX2 arm on each host.  The AVX2 arm holds no
// vpcompressd to memory, which a Zen 4 core runs in microcode.

#include "bench_core_sort_references.h"

#include "x86simdsort-static-incl.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the pinned x86-simd-sort is the speed roof of the sorts of the base")

namespace bench_sort_references {

void sort_simd_avx2(float* first, std::size_t count) noexcept { x86simdsortStatic::qsort(first, count, false, false); }

void sort_simd_avx2(std::uint32_t* first, std::size_t count) noexcept {
    x86simdsortStatic::qsort(first, count, false, false);
}

void sort_simd_avx2(std::uint64_t* first, std::size_t count) noexcept {
    x86simdsortStatic::qsort(first, count, false, false);
}

void sort_simd_avx2_keyvalue(float* keys, std::uint32_t* values, std::size_t count) noexcept {
    x86simdsortStatic::keyvalue_qsort(keys, values, count, false, false);
}

}  // namespace bench_sort_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
