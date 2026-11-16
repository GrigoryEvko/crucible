// A kernel context has the empty row, so a kernel allocates nothing.  The
// allocation doors take the bare tag of Alloc, and only a context whose row
// holds Alloc mints it.  A kernel takes its buffers from its caller.

#include <fixy/Ctx.h>
#include <foundation/core/FpMode.h>
#include <foundation/effects/Capability.h>

namespace {

struct KernelBrand {};

using KernelCtx = ::fixy::Kernel<KernelBrand, ::foundation::core::kFpModeIeee>;

[[maybe_unused]] void kernel(KernelCtx const& ctx) {
    [[maybe_unused]] auto alloc = ::foundation::effects::extract_bare(
        ::foundation::effects::mint_from_ctx<::foundation::effects::Effect::Alloc>(ctx));
}

}  // namespace

int main() { return 0; }
