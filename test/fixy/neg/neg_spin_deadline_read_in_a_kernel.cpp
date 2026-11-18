// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A kernel can get a spin deadline by reference from its caller, but it
// cannot read it.  has_passed takes the bare tag of Nondet, and the context
// of a kernel has the empty row, so it gives no tag.  So the counter does not
// reach a result of the kernel through the deadline.
//
// Expected diagnostic: no candidate of extract_bare_from_ctx for the context
// of the kernel.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/SpinBudget.h>
#include <foundation/core/FpMode.h>
#include <foundation/effects/Capability.h>

namespace {

struct KernelBrand {};

using KernelCall = ::fixy::KernelCall<KernelBrand, ::foundation::core::kFpModeIeee>;

[[maybe_unused]] bool kernel(KernelCall const& call, ::fixy::time::SpinDeadline const& deadline) noexcept {
    return deadline.has_passed(
        ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(call.ctx()));
}

}  // namespace

int main() { return 0; }
