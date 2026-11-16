// The token of a kernel call names one call of run_kernel.  A copy could
// live past the call, when the scope of the context has ended, so the token
// has no copy.
//
// Expected diagnostic: the deleted copy of the token, with its reason.

#include <fixy/Ctx.h>
#include <foundation/core/FpMode.h>

namespace {

struct KernelBrand {};

using KernelCall = ::fixy::KernelCall<KernelBrand, ::foundation::core::kFpModeIeee>;

[[maybe_unused]] void kernel(KernelCall const& call) noexcept { [[maybe_unused]] KernelCall const kept = call; }

}  // namespace

int main() { return 0; }
