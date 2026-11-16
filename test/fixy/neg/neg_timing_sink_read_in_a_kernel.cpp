// A kernel writes the observation sink with no context, but it reads no
// count.  The read takes the bare tag of Nondet, and the context of a kernel
// has the empty row, so it gives no tag.  So no count reaches a result of
// the kernel.
//
// Expected diagnostic: no candidate of extract_bare_from_ctx for the context
// of the kernel.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/TimingSink.h>
#include <foundation/core/FpMode.h>
#include <foundation/effects/Capability.h>

namespace {

struct KernelBrand {};

using KernelCall = ::fixy::KernelCall<KernelBrand, ::foundation::core::kFpModeIeee>;
using Sink = ::fixy::observe::TimingSink<2>;

[[maybe_unused]] void kernel(KernelCall const& call, Sink& sink) noexcept {
    sink.mark(::fixy::index<0>());
    [[maybe_unused]] auto count =
        sink.read(::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(call.ctx()),
                  ::fixy::index<0>());
}

}  // namespace

int main() { return 0; }
