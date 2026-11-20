// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The MetaLog keeps a View of the records of its own buffer.  An assignment
// to the buffer would free the storage that the View names, and each later
// append and read through the View would use freed memory.  The buffer and
// the View are private, so no code outside the log writes them.

#include <crucible/MetaLog.h>
#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    ::crucible::MetaLog log{::foundation::effects::testing::test().alloc};
    log.entries_buffer_ = ::fixy::mint_heap_array<::crucible::TensorMeta, ::crucible::warden::kHugePageBytes>(
        ::foundation::effects::testing::test().alloc, 1, ::crucible::TensorMeta{});
    return 0;
}
