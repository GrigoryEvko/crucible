// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The MetaLog holds the ring of its records in a FixedHeapArray.  An
// assignment to the ring would free the storage that the producer writes and
// the consumer reads, and each later append and read would use freed memory.
// The ring is private, and a FixedHeapArray has no assignment, so no code
// outside the log writes it.

#include <crucible/MetaLog.h>
#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    ::crucible::MetaLog log{::foundation::effects::testing::test().alloc};
    log.records_ = ::fixy::mint_fixed_heap_array<::crucible::TensorMeta, ::crucible::MetaLog::CAPACITY,
                                                 ::crucible::warden::kHugePageBytes>(
        ::foundation::effects::testing::test().alloc, ::crucible::TensorMeta{});
    return 0;
}
