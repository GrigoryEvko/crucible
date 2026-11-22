// The body of run_beside moves its key into a field that it holds by value.
// run_beside is a root door too, and it refuses the body.
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Choice.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>
#include <utility>
namespace {
struct HolderBrand final {};
struct Thief {
    ::foundation::core::Option<::foundation::core::LoanKey<HolderBrand>> stolen{};
    void operator()(::foundation::core::LoanKey<HolderBrand>& key) noexcept { stolen.fill(std::move(key)); }
};
}  // namespace
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto continuation = [] noexcept {};
    static_cast<void>(::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, Thief{}, continuation)
                          .expect("the door starts the body"));
    return 0;
}
