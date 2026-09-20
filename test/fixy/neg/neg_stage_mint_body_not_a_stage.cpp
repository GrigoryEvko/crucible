// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_stage over a body that is not a pipeline stage: one consumer
// handle and no producer handle.  The factory's own signature names the
// body's second parameter, so there is nothing to call.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>

#include <optional>
#include <utility>

namespace {

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void consumer_only(FakeConsumer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx;
    FakeConsumer<int> in;
    FakeProducer<int> out;

    auto bad = fixy::concurrent::mint_stage<&consumer_only>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
