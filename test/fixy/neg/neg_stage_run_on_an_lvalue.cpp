// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Running a stage moves its handles into the body, so run is
// rvalue-qualified and an lvalue stage cannot be run in place.

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

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx;
    auto stage = fixy::concurrent::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    stage.run();
    return 0;
}
