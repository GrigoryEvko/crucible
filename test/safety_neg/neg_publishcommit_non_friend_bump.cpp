// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `PublishCommitCell<Tag, WriteAuth>::bump()` (or
// `bump_by(delta)`) from code that is NOT WriteAuth (i.e., not a
// friend of the cell template instantiation).  The write surface is
// `private:` (PublishCommit.h:110-116) with `friend WriteAuth;`
// (PublishCommit.h:97) — the ONLY legal callers are members of
// WriteAuth's body.
//
// Discipline rationale (PublishCommit.h:23-44, 92-117):
//   This IS the GAPS-FLUSH-RACE structural fix.  The bg pipeline has
//   4 stages: drain → detect → build → publish.  Stage 3 (build)
//   historically bumped total_processed inline for commit-only
//   markers, BEFORE stage 4 (publish) had run on_region_ready.
//   Foreground flush() spins on total_processed; flush returning
//   therefore carried no synchronization on pending_region_ / mode_ /
//   active_region_ — they were stage-4 writes that hadn't happened
//   yet.  Sub-microsecond observable-but-unsynchronized window;
//   under load the four named test_vigil_* / test_end_to_end /
//   test_region_cache tests hit it every run.
//
//   `PublishCommitCell<Tag, WriteAuth>` makes the rule structural:
//   bump_by()/bump() are PRIVATE, with `friend WriteAuth;` as the
//   ONLY exception.  WriteAuth is the C++ type that physically
//   performs the publish action (stage-4's functor / member-holder).
//   Any code outside WriteAuth's body that tries to bump triggers a
//   private-access diagnostic at the call site — the bug class is
//   structurally eliminated.
//
//   No witness object, no token to thread through call sites.  The
//   friend list IS the witness.  Adding a second authorized writer
//   requires adding a second friend declaration (visible in review).
//
// HS14 — distinct-class fixture pair for PublishCommitCell:
//   * Class T-friend-gated-bump (THIS file): private + friend-gated
//     write surface — pins the "only stage-4 may bump" access-
//     control discipline that closes GAPS-FLUSH-RACE.
//   * Class T-copy (sibling neg_publishcommit_copy_rejected):
//     deleted copy ctor with channel-identity reason — pins the
//     "one cell == one atomic == one channel identity" structural
//     invariant.
//
//   Both Class T but on STRUCTURALLY DISTINCT enforcement points:
//   private+friend vs `= delete("reason")` on a special member.
//   Together they pin BOTH soundness layers of the wrapper.
//
// FIXY-U-157 — second PublishCommitCell HS14 fixture.

#include <crucible/safety/PublishCommit.h>

namespace {

struct PipelineTag {};
struct StageFourPublisher {};   // The ONLY authorized writer.

using Cell = ::crucible::safety::PublishCommitCell<PipelineTag, StageFourPublisher>;

// Anchor: reads are public, callable from anywhere.
[[maybe_unused]] static uint64_t anchor_load(const Cell& cell) noexcept {
    return cell.load_acquire();   // legal: load_acquire is public
}

// A non-WriteAuth type pretending to bump.  Stage-3 (build) is the
// historical bug site — code that ISN'T the publisher trying to
// advance the counter.
struct StageThreeBuilder {
    void offending_bump_call(Cell& cell) noexcept {
        cell.bump();
        // ERROR: 'uint64_t crucible::safety::PublishCommitCell<...>::bump()'
        // is private within this context.  Only StageFourPublisher
        // (the declared friend) may call bump / bump_by.
    }
};

}  // namespace

int main() { return 0; }
