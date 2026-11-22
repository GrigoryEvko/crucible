// One mutable body with no key, given two times as an lvalue.  Two threads
// would write the capture of one object at the same time.  The door takes
// each body by move, so an lvalue body meets the deleted overload.
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>
int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto counting = [calls = 0]() mutable noexcept {
        calls += 1;
        return calls;
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, counting, counting).is_ok() ? 0 : 1;
}
