// Negative-compile test.  This file must fail to compile.
//
// The count of a QueuedRegion tells when its region left the uncompiled queue,
// and the sweep releases the region then.  Only a push gives the count, so no
// other code builds a QueuedRegion with a count of its own.

#include <crucible/BackgroundThread.h>

#include <crucible/MerkleDag.h>

#include <cstdint>

namespace {

using Queued = crucible::BackgroundThread::QueuedRegion;

// The node is a parameter, so the fixture makes no node.
[[maybe_unused]] Queued claim_a_push(crucible::DagRef<crucible::RegionNode> node) { return Queued{node, uint64_t{0}}; }

}  // namespace

int main() { return 0; }
