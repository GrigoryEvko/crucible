// A body names two keys of one brand.  One body gets one key of each brand.
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>
namespace {
struct HolderBrand final {};
}  // namespace
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto twice = [](::foundation::core::LoanKey<HolderBrand>&, ::foundation::core::LoanKey<HolderBrand>&) noexcept {};
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(twice)).is_ok() ? 0 : 1;
}
