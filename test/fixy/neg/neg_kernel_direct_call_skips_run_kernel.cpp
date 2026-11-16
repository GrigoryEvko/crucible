// A kernel takes the token of one call, and only run_kernel makes a token,
// after the check of the thread, the scope and the register.  A direct call
// of a kernel with its context gives no token, so it does not compile, and
// no call of a kernel skips the check.
//
// Expected diagnostic: no conversion from the context to the token.

#include <fixy/Ctx.h>
#include <foundation/core/FpMode.h>

namespace {

struct KernelBrand {};

using KernelCtx = ::fixy::Kernel<KernelBrand, ::foundation::core::kFpModeIeee>;
using KernelCall = ::fixy::KernelCall<KernelBrand, ::foundation::core::kFpModeIeee>;

void kernel(KernelCall const&) noexcept {}

[[maybe_unused]] void attempt(KernelCtx const& ctx) { kernel(ctx); }

}  // namespace

int main() { return 0; }
