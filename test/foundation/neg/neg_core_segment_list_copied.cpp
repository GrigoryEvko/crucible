// A copy of a SegmentList would allocate with no allocation capability at its
// call, so a list does not copy.

#include <foundation/core/SegmentList.h>

int main() {
    ::foundation::core::SegmentList<int> const numbers;
    auto const copy = numbers;
    return static_cast<int>(copy.size());
}
