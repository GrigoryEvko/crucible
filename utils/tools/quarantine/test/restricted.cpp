// The plants of the restrictions of the admit rows.  check_plugin.py compiles
// this quarantined file in error mode with restricted.txt, one time with no
// plant and one time with each PLANT_ macro.  Each plant gives a finding of
// its restriction, and the file with no plant gives none.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

int plant_moved(int value) { return std::move(value); }

template <class T>
T plant_moved_in_template(T value) {
    return std::move(value);
}

template <class T>
bool plant_concept_holds() {
    return std::integral<T>;
}

int plant_count(std::initializer_list<int> values) { return static_cast<int>(values.size()); }

std::byte plant_byte{};

#if defined(PLANT_MOVE_ALGORITHM)
void plant_move_range(int* first, int* last, int* out) { std::move(first, last, out); }
#endif

#if defined(PLANT_RANGES_SWAP)
void plant_swap(int& left, int& right) { std::ranges::swap(left, right); }
#endif

#if defined(PLANT_LIST_OBJECT)
std::initializer_list<int> plant_list = {1, 2};
#endif

#if defined(PLANT_TO_INTEGER)
int plant_integer() { return std::to_integer<int>(plant_byte); }
#endif

#if defined(PLANT_BYTE_OPERATOR)
std::byte plant_shifted() { return plant_byte << 1; }
#endif

#if defined(PLANT_ALIGNMENT)
std::align_val_t plant_alignment{};
#endif
