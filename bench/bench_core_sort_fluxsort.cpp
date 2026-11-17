// The doors of bench_core_sort to the pinned fluxsort of scandum
// (utils/pins.txt, the Unlicense).
//
// fluxsort compiles its comparison into each function when the macro cmp is
// set before its header.  Here cmp gives a word of each type its order: the
// signed words of fluxsort32 are the bits of floats, which take the total
// order of IEEE 754, and the unsigned words of fluxsort_uint32 and
// fluxsort_uint64 take their value.  The functions of fluxsort have external
// linkage, and only this unit defines them.
//
// fluxsort holds a variable length array.  A door calls fluxsort through a
// volatile pointer, so the compiler does not inline its body into the door,
// and the body stays in the system header of the pinned source.  The call
// costs one indirect branch for each sort.

#include "bench_core_sort_references.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the pinned fluxsort is the stable reference of the sorts of the base")

namespace {

// The order of the bits of floats: the key of encode_total_order_key of
// foundation/core/Sort.h, as a signed word.
[[nodiscard]] inline bool is_after(int left, int right) noexcept {
    int const left_key = left ^ static_cast<int>(static_cast<unsigned>(left >> 31) >> 1);
    int const right_key = right ^ static_cast<int>(static_cast<unsigned>(right >> 31) >> 1);
    return left_key > right_key;
}

[[nodiscard]] inline bool is_after(unsigned left, unsigned right) noexcept { return left > right; }
[[nodiscard]] inline bool is_after(unsigned long long left, unsigned long long right) noexcept { return left > right; }
[[nodiscard]] inline bool is_after(long long left, long long right) noexcept { return left > right; }

// fluxsort also builds its sorts of 8, 16 and 128 bits with cmp.  The bench
// calls none of them.  A word of 8 or 16 bits promotes to int.
[[nodiscard]] inline bool is_after(long double left, long double right) noexcept { return left > right; }

}  // namespace

#define cmp(a, b) is_after(*(a), *(b))
#include "fluxsort.h"
#undef cmp

namespace {

// The entry points of fluxsort, behind volatile pointers.
using FluxsortEntry = void (*)(void*, std::size_t, CMPFUNC*);
FluxsortEntry volatile const total_order_entry = &fluxsort32;
FluxsortEntry volatile const words_entry = &fluxsort_uint32;
FluxsortEntry volatile const wide_words_entry = &fluxsort_uint64;

}  // namespace

namespace bench_sort_references {

void sort_fluxsort_total_order(std::int32_t* first, std::size_t count) noexcept {
    total_order_entry(first, count, nullptr);
}

void sort_fluxsort_words(std::uint32_t* first, std::size_t count) noexcept { words_entry(first, count, nullptr); }

void sort_fluxsort_wide_words(std::uint64_t* first, std::size_t count) noexcept {
    wide_words_entry(first, count, nullptr);
}

}  // namespace bench_sort_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
