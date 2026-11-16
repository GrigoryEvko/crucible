// An FP scope writes the FP control register of its thread, and its end
// writes the word of before back.  Its constructor is private, and its one
// friend is mint_fp_scope, so no code opens a scope with no mint.

#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

namespace {

[[maybe_unused]] void attempt() {
    [[maybe_unused]] ::foundation::os::FpScope<::foundation::core::kFpModeFlushToZero> const scope{};
}

}  // namespace

int main() { return 0; }
