// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PublishOnce<T> hands off a T*, so T names the pointee.  Passing a
// pointer type builds an atomic<CompiledKernel**> and publishes the
// address of a pointer variable rather than the handle, so every
// observer reads a live pointer through one that is usually a dead
// local.
//
// The guard this exercises replaced one reading
// `is_pointer_v<T*> || is_same_v<T, T>`, whose second disjunct holds
// for every T.  That version admitted this file.

#include <crucible/handles/PublishOnce.h>

struct CompiledKernel;

int main() {
    crucible::safety::PublishOnce<CompiledKernel*> slot;
    (void)slot;
    return 0;
}
