// Negative-compile test.  This file must fail to compile.
//
// The MetaLog holds the ring of its records in a FixedHeapArray.  An
// assignment to the ring would free the storage that the producer writes and
// the consumer reads, and each later append and read would use freed memory.
// The ring is private, so no code outside the log names it, and no code
// outside the log assigns it.  A FixedHeapArray also has no assignment
// (neg_core_fixed_heap_array_assigned).
//
// Expected diagnostic: the ring of the log is private.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

int main() {
    ::crucible::MetaLog log{::foundation::effects::testing::test().alloc};
    [[maybe_unused]] auto const& ring = log.records_;
    return 0;
}
