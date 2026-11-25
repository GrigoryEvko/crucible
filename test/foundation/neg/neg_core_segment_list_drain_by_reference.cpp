// The body of a drain takes each element by reference.  drain ends the element
// after the call, so the body would leave a linear element with no consumer.
// A body takes each element by value.

#include <foundation/core/SegmentList.h>

#include <utility>

int main() {
    ::foundation::core::SegmentList<int> list;
    int total = 0;
    std::move(list).drain([&total](int&& value) noexcept { total += value; });
    return total;
}
