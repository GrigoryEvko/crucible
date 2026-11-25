// append_with builds the element from the result of make with no copy and no
// move, so make gives the element type and not a type that converts to it.

#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::core::SegmentList<long> numbers;
    numbers.append_with(::foundation::effects::testing::test().alloc, [](std::size_t) noexcept { return 1; });
    return static_cast<int>(numbers.size());
}
