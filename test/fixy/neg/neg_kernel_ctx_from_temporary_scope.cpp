// A temporary FP scope ends at the end of the full expression, and the
// register then no longer holds the mode of the type.  So the mint of a
// kernel context refuses a scope that is an rvalue: its overload for one is
// deleted.

#include <fixy/Ctx.h>
#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

namespace {

struct KernelBrand {};

[[maybe_unused]] void attempt() {
    [[maybe_unused]] auto ctx = ::fixy::mint_kernel_context<KernelBrand>(
        ::foundation::os::mint_fp_scope<::foundation::core::kFpModeFlushToZero>());
}

}  // namespace

int main() { return 0; }
