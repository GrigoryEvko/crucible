// Negative-compile test.  This file must fail to compile.
//
// A writer of a slot that no claim gave would be a second writer of the slot
// (crucible/KernelCache.h).  Only the slot builds its writer, after the claim.
//
// Expected diagnostic: the constructor of the writer is private.

#include <crucible/KernelCache.h>

int main() {
    crucible::KernelCache::KernelCacheSlot slot{};
    crucible::KernelCache::KernelCacheSlot::WriterHandle const writer{slot};
    static_cast<void>(writer);
    return 0;
}
