// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copy-constructing a `PublishCommitCell<Tag, WriteAuth>`.
// The copy ctor is `= delete("PublishCommitCell owns the channel
// identity; not copyable")` at PublishCommit.h:124.
//
// Discipline rationale (PublishCommit.h:10-44, 92-127):
//   PublishCommitCell<Tag, WriteAuth> is the structural fix for the
//   GAPS-FLUSH-RACE bug class (test_vigil_dispatch / test_end_to_end /
//   test_region_cache / test_vigil_deadline_watchdog).  The cell IS
//   the channel identity — the std::atomic<uint64_t> at a fixed
//   cross-thread address pairs the bg pipeline's stage-4 (publish)
//   release store with the fg's flush()-side acquire load.
//
//   Duplicating the cell via copy would give two distinct atomic
//   instances both claiming to be the "committed count" channel.
//   Stage-4 would bump one; fg's flush would poll the other.  The
//   acquire/release pairing the wrapper guarantees would silently
//   evaporate — fg sees mode_=COMPILED with no synchronization on
//   pending_region_ / mode_ / active_region_, exactly the
//   sub-microsecond observable-but-unsynchronized window the wrapper
//   was created to close.
//
//   The deleted-copy is the structural guarantee that the cell's
//   atomic identity is unique per pipeline.  Pipelines that need
//   multiple commit channels declare multiple cells with distinct
//   Tag/WriteAuth pairs — each ITS OWN unique atomic.
//
// HS14 — distinct-class fixture pair for PublishCommitCell:
//   * Class T-copy (THIS file): deleted copy ctor with channel-
//     identity reason — pins the "one cell == one atomic == one
//     channel identity" structural invariant.
//   * Class T-friend-gated-bump (sibling neg_publishcommit_non_
//     friend_bump): bump_by()/bump() private + friend-gated to
//     WriteAuth — pins the "only stage-4 may bump" structural
//     access discipline that closes GAPS-FLUSH-RACE.
//
//   Both Class T but on STRUCTURALLY DISTINCT enforcement points:
//   one is `= delete("reason")` on a special member, the other is
//   private + friend list.  Together they pin BOTH soundness
//   layers of the wrapper.
//
// FIXY-U-157 — first PublishCommitCell HS14 fixture (wrapper had
// ZERO neg-coverage before this ship; closes its slice of #146
// A8-P2).

#include <crucible/safety/PublishCommit.h>

namespace {

struct PipelineTag {};
struct StageFourPublisher {};

using Cell = ::crucible::safety::PublishCommitCell<PipelineTag, StageFourPublisher>;

}  // namespace

// Anchor: in-place default-construction compiles cleanly — the only
// path to OBTAIN a PublishCommitCell.
[[maybe_unused]] static Cell anchor_default_cell{};

// VIOLATION: PublishCommitCell<Tag, WriteAuth>(const PublishCommitCell&)
// is `= delete("PublishCommitCell owns the channel identity; not
// copyable")`.  Direct copy construction from a const lvalue triggers
// the deleted-function diagnostic with the channel-identity reason
// string verbatim.
[[maybe_unused]] static Cell offending_copy_construct(const Cell& source) {
    return Cell{source};
    // ERROR: use of deleted function 'PublishCommitCell(const PublishCommitCell&)'
    // diagnostic message: "PublishCommitCell owns the channel identity; not copyable"
}

int main() { return 0; }
