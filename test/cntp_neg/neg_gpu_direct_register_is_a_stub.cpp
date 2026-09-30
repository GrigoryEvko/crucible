// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// No driver registers a GPU memory region, so register_gpu_memory is a
// stub.  Its deprecation makes each call site see that at compile time.
// The pragma turns the warning into an error whatever the build flags are.

#pragma GCC diagnostic error "-Wdeprecated-declarations"

#include <crucible/cntp/_wip/GpuDirect.h>

#include <expected>

namespace gd = crucible::cntp::_wip::gpu_direct;

namespace {

[[maybe_unused]] std::expected<gd::OwnedGpuDirectMr, gd::GpuDirectError>
register_once(gd::DeclaredGpuDirectMrPlan plan) noexcept {
    return gd::register_gpu_memory(plan);
}

}  // namespace

int main() { return 0; }
