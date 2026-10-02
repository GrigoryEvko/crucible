// The plants of the restrictions of the admit rows.  check_plugin.py compiles
// this quarantined file in error mode with restricted.txt, one time with no
// plant and one time with each PLANT_ macro.  Each plant gives a finding of
// its restriction, and the file with no plant gives none.

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

enum class PlantColor : unsigned char { red };
struct PlantPair {
    unsigned int low;
    unsigned int high;
};

int plant_moved(int value) { return std::move(value); }

unsigned long plant_bits(double value) { return std::bit_cast<unsigned long>(value); }

PlantPair plant_pair(unsigned long bits) { return std::bit_cast<PlantPair>(bits); }

// The explicit argument gives the result, and the argument of the call is
// dependent.
template <class T>
unsigned long plant_bits_of(T value) {
    return std::bit_cast<unsigned long>(value);
}

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

#if defined(PLANT_BIT_CAST_BOOL)
bool plant_flag(unsigned char byte) { return std::bit_cast<bool>(byte); }
#endif

#if defined(PLANT_BIT_CAST_ENUM)
PlantColor plant_color(unsigned char byte) { return std::bit_cast<PlantColor>(byte); }
#endif

#if defined(PLANT_BIT_CAST_MEMBER)
struct PlantFlags {
    unsigned char count;
    bool flag;
};
PlantFlags plant_flags(unsigned short bits) { return std::bit_cast<PlantFlags>(bits); }
#endif

#if defined(PLANT_BIT_CAST_BASE)
struct PlantColorBase {
    PlantColor color;
};
struct PlantColored : PlantColorBase {
    unsigned char shade;
};
PlantColored plant_colored(unsigned short bits) { return std::bit_cast<PlantColored>(bits); }
#endif

#if defined(PLANT_BIT_CAST_ARRAY)
struct PlantColors {
    PlantColor colors[2];
};
PlantColors plant_colors(unsigned short bits) { return std::bit_cast<PlantColors>(bits); }
#endif

#if defined(PLANT_BIT_CAST_DEPENDENT)
template <class T>
T plant_from_bits(unsigned long bits) {
    return std::bit_cast<T>(bits);
}
#endif
