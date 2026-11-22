// Negative compile test.  This file must fail to compile.
//
// A kernel context names the floating-point scope of the thread that minted
// it.  run_joined refuses a body that captures a context (foundation/
// Closure.h, rule 3), and the body takes the context of the door in its
// place.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

namespace {

struct KernelBrand {};

}  // namespace

int main() {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    os::FpScope<core::kFpModeFlushToZero> const scope = os::mint_fp_scope<core::kFpModeFlushToZero>();
    ::fixy::Kernel<KernelBrand, core::kFpModeFlushToZero> const ctx = ::fixy::mint_kernel_context<KernelBrand>(scope);
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [&ctx]() noexcept { static_cast<void>(ctx); };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
