// The plant of the admit rows.  check_plugin.py compiles this quarantined file
// in error mode with no plant and with PLANT_STD_NAME.  The test table admits
// no std::swap, so the plant gives one finding, and the file with no plant
// gives none.

#include <type_traits>
#include <utility>

#if defined(PLANT_STD_NAME)
void plant_swap(int& left, int& right) { std::swap(left, right); }
#endif

int plant_plain(int value) {
    int moved = std::move(value);
    return std::is_same_v<decltype(moved), int> ? moved : 0;
}
