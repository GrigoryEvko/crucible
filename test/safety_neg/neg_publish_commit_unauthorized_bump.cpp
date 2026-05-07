// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Verifies the friend-gated write surface of
// safety::PublishCommitCell<Tag, WriteAuth>.  Code that is NOT the
// listed WriteAuth attempting to call bump_by must be rejected with a
// "private member" diagnostic — the type-system fence against the
// GAPS-FLUSH-RACE bug class.
//
// The cell is parametrized on (Tag, WriteAuth).  Only WriteAuth (and
// its members) are friends; bump_by/bump are private.  An unrelated
// caller (here, OtherStage) cannot invoke them.

#include <crucible/safety/PublishCommit.h>

namespace saf = crucible::safety;

namespace {
struct PipelineTag {};
struct PublishStage {};
struct OtherStage {};
} // namespace

int main() {
    // Cell befriends PublishStage as the SOLE legal bumper.
    saf::PublishCommitCell<PipelineTag, PublishStage> cell;

    // OtherStage is NOT a friend.  Calling bump_by from main()
    // (which is not within PublishStage's scope) must fail.
    //
    // Expected diagnostic substring: "is private within this context"
    // or "private member" — the precise substring is regex-matched by
    // the CMake fixture.
    auto prev = cell.bump_by(1);
    (void)prev;
    return 0;
}
