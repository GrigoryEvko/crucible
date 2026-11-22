// A keyed body moves its key into a field that it holds by value, and the
// caller could read the field after the join and open a frame with a second
// key of the brand.  The root door refuses a body that reaches a key of a
// brand of its keys, also by value.
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
    Thief thief{};
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(thief)).is_ok() ? 0 : 1;
}
