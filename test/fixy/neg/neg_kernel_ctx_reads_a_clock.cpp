// A kernel context has the empty row and no Nondet, so a kernel reads no
// clock.  The mint of a clock reader refuses the context of the kernel.  A
// clock value in a kernel would make the bits of its result depend on the
// time.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>
#include <foundation/core/FpMode.h>

namespace {

struct KernelBrand {};

using KernelCtx = ::fixy::Kernel<KernelBrand, ::foundation::core::kFpModeIeee>;

[[maybe_unused]] void kernel(KernelCtx const& ctx) {
    [[maybe_unused]] auto reader = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
}

}  // namespace

int main() { return 0; }
