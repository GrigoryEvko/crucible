// A body of run_joined owns a holder of a plain brand by value and opens a
// frame of it with its key.  The frame body borrows the holder and calls the
// dispatch door, whose body opens a second frame of the holder with a key of
// the same brand.  The dispatch door mints only keys of dispatch brands, so the
// plant fails at run_dispatch.
#include <fixy/Ctx.h>
#include <fixy/concurrent/Dispatch.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Array.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
namespace {
struct HolderBrand final {};
struct Holder {
    ::foundation::core::FixedArray<int, 4, HolderBrand> array{};
};
using DispatchCtx =
    ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::BrandedFg<Holder>, ::foundation::effects::Row<>>;
}  // namespace
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    Holder const holder{};
    auto outer = [moved = holder](::foundation::core::LoanKey<HolderBrand>& key) mutable noexcept {
        moved.array.with_mut(key, [&moved](auto run) noexcept {
            DispatchCtx const dispatch_ctx = ::foundation::effects::testing::foreground<Holder>();
            ::fixy::dispatch::run_dispatch(
                dispatch_ctx, [&moved](DispatchCtx const&, ::foundation::core::LoanKey<HolderBrand>& second) noexcept {
                    moved.array.with_mut(second, [](auto inner) noexcept { static_cast<void>(inner.size()); });
                });
            static_cast<void>(run.size());
        });
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(outer)).is_ok() ? 0 : 1;
}
