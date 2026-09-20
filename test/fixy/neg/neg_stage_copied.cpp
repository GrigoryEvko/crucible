// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A stage holds its handles, and a handle holds a linear token; a copy
// would be a second token.  The copy constructor is deleted with that
// reason, and the compiler repeats it.

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

    auto twin = stage;
    (void)twin;
    return 0;
}
