// A walk of the segments of a temporary SegmentList would read the directory of
// a list that ends with the full expression, so segments() of an rvalue is
// refused.

#include <foundation/core/SegmentList.h>

#include <cstddef>

int main() {
    auto const runs = ::foundation::core::SegmentList<int>{}.segments();
    return static_cast<int>(runs.size());
}
