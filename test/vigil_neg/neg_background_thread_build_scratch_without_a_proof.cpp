// Negative-compile test.  This file must fail to compile.
//
// The build stage writes its scratch arena with no order.  So a read of the
// bytes of the arena takes the proof that no pipeline runs (prove_quiescence),
// and a read with no proof does not compile.

#include <crucible/BackgroundThread.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] size_t read_bytes(crucible::BackgroundThread& background) {
    return background.build_scratch_retained_bytes().raw();
}

}  // namespace

int main() { return 0; }
