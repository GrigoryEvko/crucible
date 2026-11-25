// An append to a SegmentList takes the allocation capability first, so each
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::SegmentList<int> numbers;
    numbers.append(::foundation::effects::testing::test().io, 1);
    return static_cast<int>(numbers.size());
}
