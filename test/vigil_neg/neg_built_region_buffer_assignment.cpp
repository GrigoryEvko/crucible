// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A BuiltRegion holds the ops of a region, the runs that the ops point into,
// and the arena that owns the runs.  An assignment to the arena would free
// the runs that the ops still name.  So the arena and the runs are private,
// and a region moves only as a whole (crucible/BackgroundThread.h).
//
// Expected diagnostic: the arena of the region is private.

#include <crucible/BackgroundThread.h>

int main() {
    crucible::BackgroundThread::BuiltRegion region{};
    region.arena_ = ::fixy::HeapArray<::fixy::Box<::fixy::BumpArena>>{};
    return 0;
}
