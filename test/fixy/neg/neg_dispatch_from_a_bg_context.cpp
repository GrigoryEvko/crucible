// A thread with a background context opens a dispatch.  Only a context with a
// branded foreground source, which a producer claim gives, opens one.
#include <fixy/Ctx.h>
#include <fixy/concurrent/Dispatch.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>
namespace {
struct[[= ::foundation::core::dispatch_brand{}]] LogBrand final {};
}  // namespace
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    ::fixy::dispatch::run_dispatch(ctx,
                                   [](::fixy::BgLoadCtx const&, ::foundation::core::LoanKey<LogBrand>&) noexcept {});
    return 0;
}
