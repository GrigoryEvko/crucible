// An FP scope writes back the word of the register one time, on the thread
// that opened it.  A copy would write it back two times, and a move could
// take the end of the scope to another thread.  So the scope has no copy and
// no move.

#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

namespace {

[[maybe_unused]] void attempt() {
    ::foundation::os::FpScope<::foundation::core::kFpModeFlushToZero> const scope =
        ::foundation::os::mint_fp_scope<::foundation::core::kFpModeFlushToZero>();
    [[maybe_unused]] ::foundation::os::FpScope<::foundation::core::kFpModeFlushToZero> const copy = scope;
}

}  // namespace

int main() { return 0; }
