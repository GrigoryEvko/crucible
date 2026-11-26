// Negative-compile test.  This file must fail to compile.
//
// A BuiltRegion holds the ops of a region and the runs that the ops point
// into, in the parcel of the arena that owns the runs.  An assignment to the
// parcel would free the runs that the ops still name.  So the parcel is
// private, and a region moves only as a whole (crucible/BackgroundThread.h).
//
// Expected diagnostic: the parcel of the region is private.

#include <crucible/BackgroundThread.h>

int main() {
    crucible::BuiltRegion region{};
    region.parcel_ = ::fixy::Option<crucible::BuiltRegion::Parcel>{};
    return 0;
}
