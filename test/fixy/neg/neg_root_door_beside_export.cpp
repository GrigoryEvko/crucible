// The body of run_beside moves its key into a field that it holds by value.
// run_beside is a root door too, and it refuses the body.
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/List.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>
#include <utility>
namespace {
struct HolderBrand final {};
struct Thief {
    ::foundation::core::FixedList<::foundation::core::LoanKey<HolderBrand>, 1> stolen{};
    void operator()(::foundation::core::LoanKey<HolderBrand>& key) noexcept {
        static_cast<void>(stolen.append(std::move(key)));
    }
};
}  // namespace
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    return ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, Thief{}, [] noexcept {}).is_ok() ? 0 : 1;
}
