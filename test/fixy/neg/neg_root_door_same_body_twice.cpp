// One keyed body that owns a holder by value, given two times as an lvalue.
// Two threads would each mint a key and open two frames of one holder at the
// same time.  The door takes each body by move, so an lvalue body meets the
// deleted overload.
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
    Holder const holder{};
    auto by_value = [moved = holder](::foundation::core::LoanKey<HolderBrand>& key) mutable noexcept {
        moved.array.with_mut(key, [](auto run) noexcept {
            auto element = run.get_mut(0).expect("four elements");
            *element += 1;
        });
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, by_value, by_value).is_ok() ? 0 : 1;
}
