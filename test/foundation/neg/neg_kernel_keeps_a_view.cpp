// A kernel captures by reference an Option of a View, which the caller reads
// after run_kernel.  The kernel can keep a borrow of a buffer of the call there.
// run_kernel refuses a kernel that can store a borrow outside its call
// (IsKernelOf of foundation/effects/Ctx.h).

#include <foundation/core/Choice.h>
#include <foundation/core/FpMode.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>

namespace {
struct KernelBrand final {};

using KernelContext = ::foundation::effects::KernelCtx<KernelBrand, ::foundation::core::kFpModeIeee>;
using KernelCall = ::foundation::effects::KernelCall<KernelBrand, ::foundation::core::kFpModeIeee>;

[[maybe_unused]] void run(KernelContext const& ctx) noexcept {
    ::foundation::core::Option<::foundation::core::View<float const>> kept{::foundation::core::none};
    ::foundation::effects::run_kernel(ctx, [&kept](KernelCall const& call) noexcept {
        static_cast<void>(call);
        static_cast<void>(kept.is_some());
    });
}
}  // namespace

int main() { return 0; }
