// drain gives each element away and ends the list, so a named list does not
// drain: the caller moves it.

#include <foundation/core/SegmentList.h>

int main() {
    ::foundation::core::SegmentList<int> list;
    int total = 0;
    list.drain([&total](int value) noexcept { total += value; });
    return total;
}
