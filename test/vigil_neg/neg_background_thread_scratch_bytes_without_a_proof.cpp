// Negative-compile test.  This file must fail to compile.
//
// The build stage and the publish stage write the two scratch arenas with no
// order.  So a read of their bytes takes the proof that no pipeline runs
// (prove_quiescence), and a read with no proof does not compile.

#include <crucible/BackgroundThread.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] size_t read_bytes(crucible::BackgroundThread const& background) {
    return background.scratch_retained_bytes();
}

}  // namespace

int main() { return 0; }
