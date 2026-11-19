// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a read through the target of an arm with no test of the link.
//
// The target of a const arm is an Option of a Ref of a const TraceNode.  An
// arm of a loaded branch has no target until a caller resolves it.  A link
// has no operator->, so a walk reads the target through a match, a loop over
// the link, or resolved_target(), which ends the process with a reason when
// the arm has no target.

#include <crucible/MerkleDag.h>

int main() {
    const crucible::BranchNode::Arm arm{};
    return static_cast<int>(arm.target()->merkle_hash.raw());
}
