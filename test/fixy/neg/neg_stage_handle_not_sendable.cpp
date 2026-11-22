// Negative compile test.  This file must fail to compile.
//
// A stage can run on a thread of its own, so each handle of its body moves to
// another thread (StageCrossesThreads of fixy/concurrent/Stage.h).  A consumer
// handle that refers to a plain int does not, because the thread of the
// caller can still write that int.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>

namespace {

template <typename T>
struct ReferringConsumer {
    int& shared;
    [[nodiscard]] ::foundation::core::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct PlainProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(ReferringConsumer<int>&&, PlainProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    int count = 0;
    auto stage = fixy::concurrent::mint_stage<&pass_through>(ctx, ReferringConsumer<int>{count}, PlainProducer<int>{});
    (void)stage;
    return 0;
}
