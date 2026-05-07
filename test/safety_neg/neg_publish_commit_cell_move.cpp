// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PublishCommitCell owns a std::atomic<uint64_t> that crosses a
// thread boundary (bg writer / fg reader).  Moving the cell would
// invalidate the foreground's pointer/reference to it; copying would
// duplicate the channel identity (two atomic counters that the
// foreground might race between).
//
// The wrapper deletes both move and copy with an explicit reason
// string.  This fixture verifies a copy attempt is rejected at
// compile time.

#include <crucible/safety/PublishCommit.h>

namespace saf = crucible::safety;

namespace {
struct PipelineTag {};
struct PublishStage {};
} // namespace

int main() {
    saf::PublishCommitCell<PipelineTag, PublishStage> a;
    // Copy is deleted with reason string.  Expected diagnostic:
    // "use of deleted function" + the reason text.
    saf::PublishCommitCell<PipelineTag, PublishStage> b{a};
    (void)b;
    return 0;
}
