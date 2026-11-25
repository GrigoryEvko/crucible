// append_with of a SegmentList takes the allocation capability first, as append
// does.  A different capability tag fails the gate.

#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::core::SegmentList<int> numbers;
    numbers.append_with(::foundation::effects::testing::test().io, [](std::size_t) noexcept { return 1; });
    return static_cast<int>(numbers.size());
}
