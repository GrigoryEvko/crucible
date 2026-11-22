// A keyed body holds one holder by value and borrows a second holder of the
// same brand.  The walk reads each type in each region, so the borrowed holder
// is refused although the same type is held by value.
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
    Holder const first{};
    Holder second{};
    auto both = [moved = first, &second](::foundation::core::LoanKey<HolderBrand>& key) mutable noexcept {
        moved.array.with_mut(key, [](auto run) noexcept { static_cast<void>(run.size()); });
        static_cast<void>(second);
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(both)).is_ok() ? 0 : 1;
}
