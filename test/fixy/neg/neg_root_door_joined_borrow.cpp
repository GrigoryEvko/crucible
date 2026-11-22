// A frame body of a holder starts a joined thread whose body borrows the
// holder and names a key of its brand.  The door would mint a second key of
// the brand, and the joined body could open a second frame of the holder.  The
// root door refuses a body that reaches an owner of a brand of its keys
// through a borrow.
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
    auto outer = [&holder, &ctx](::foundation::core::LoanKey<HolderBrand>& key) noexcept {
        holder.array.with_mut(key, [&holder, &ctx](auto run) noexcept {
            auto joined = [&holder](::foundation::core::LoanKey<HolderBrand>& inner_key) noexcept {
                holder.array.with_mut(inner_key, [](auto inner) noexcept { static_cast<void>(inner.size()); });
            };
            static_cast<void>(::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, joined).is_ok());
            static_cast<void>(run.size());
        });
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, outer).is_ok() ? 0 : 1;
}
