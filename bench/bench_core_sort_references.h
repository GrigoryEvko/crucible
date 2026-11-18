#pragma once

// The doors of bench_core_sort to the pinned references of the state of the
// art.  Each one sorts the count elements from first in place.  A unit of
// its own compiles each reference, so the flags of a unit, such as the
// x86-64 level of x86-simd-sort, stay with that reference.  The bench
// compiles a door only when its definition is set: bench/CMakeLists.txt sets
// CRUCIBLE_BENCH_FLUXSORT and CRUCIBLE_BENCH_X86_SIMD_SORT when the tools
// directory holds the pinned source (python3 utils/bin/setup install TOOL).

#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the doors of the pinned references of the sorts of the base")

namespace bench_sort_references {

#if defined(CRUCIBLE_BENCH_FLUXSORT)
// fluxsort of the bits of floats by the total order of IEEE 754: the key of
// encode_total_order_key of foundation/core/Sort.h, as a signed word.  Stable.
void sort_fluxsort_total_order(std::int32_t* first, std::size_t count) noexcept;

// fluxsort of unsigned words by their value.  Stable.
void sort_fluxsort_words(std::uint32_t* first, std::size_t count) noexcept;
void sort_fluxsort_wide_words(std::uint64_t* first, std::size_t count) noexcept;
#endif

#if defined(CRUCIBLE_BENCH_X86_SIMD_SORT)
// x86-simd-sort in ascending order, at the x86-64 level v3: the AVX2 arm.
// Not stable.  A float takes the order of <, and the pool holds no NaN.
void sort_simd_avx2(float* first, std::size_t count) noexcept;
void sort_simd_avx2(std::uint32_t* first, std::size_t count) noexcept;
void sort_simd_avx2(std::uint64_t* first, std::size_t count) noexcept;

// keyvalue_qsort of x86-simd-sort at the x86-64 level v3: the count float
// keys from keys in ascending order by <, and each value of values with its
// key.  The roof of a sort of records by a key.  Not stable.
void sort_simd_avx2_keyvalue(float* keys, std::uint32_t* values, std::size_t count) noexcept;

// x86-simd-sort in ascending order, at the x86-64 level of the host: the
// AVX-512 arm on a host with AVX512DQ and AVX512VL, else the AVX2 arm.  On a
// Zen 4 core the AVX-512 arm stores each partition with vpcompressd to
// memory, which the core runs in microcode.  Not stable.
void sort_simd_host(float* first, std::size_t count) noexcept;
void sort_simd_host(std::uint32_t* first, std::size_t count) noexcept;
void sort_simd_host(std::uint64_t* first, std::size_t count) noexcept;
#endif

}  // namespace bench_sort_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
