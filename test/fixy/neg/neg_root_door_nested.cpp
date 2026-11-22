// A joined body with no key borrows a holder and starts a second joined
// thread whose body borrows the holder and names a key of its brand.  The
// inner root door refuses its body, also under a door with no key.
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Array.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>
namespace {
struct HolderBrand final {};
struct Holder {
    ::foundation::core::FixedArray<int, 4, HolderBrand> array{};
};
}  // namespace
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    Holder holder{};
    auto keyless = [&ctx, &holder]() noexcept {
        auto inner = [&holder](::foundation::core::LoanKey<HolderBrand>& key) noexcept {
            holder.array.with_mut(key, [](auto run) noexcept { static_cast<void>(run.size()); });
        };
        static_cast<void>(::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(inner)).is_ok());
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(keyless)).is_ok() ? 0 : 1;
}
