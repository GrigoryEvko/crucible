// A keyed body starts a joined thread whose body captures the key of the
// parent by reference.  The root door refuses a body that reaches a key of a
// brand of its keys.
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Array.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>
namespace {
struct HolderBrand final {};
}  // namespace
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto outer = [&ctx](::foundation::core::LoanKey<HolderBrand>& parent) noexcept {
        auto inner = [&parent](::foundation::core::LoanKey<HolderBrand>& key) noexcept {
            static_cast<void>(parent);
            static_cast<void>(key);
        };
        static_cast<void>(::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, inner).is_ok());
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, outer).is_ok() ? 0 : 1;
}
