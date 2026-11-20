// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A BuiltRegion holds the ops of a region and the buffers that the ops point
// into.  An assignment to one buffer would free the storage that the ops
// still name.  So the buffers are private, and a region moves only as a
// whole (crucible/BackgroundThread.h).
//
// Expected diagnostic: the buffer of the slots is private.

#include <crucible/BackgroundThread.h>

int main() {
    crucible::BackgroundThread::BuiltRegion region{};
    region.slots_ = ::fixy::HeapArray<crucible::TensorSlot>{};
    return 0;
}
