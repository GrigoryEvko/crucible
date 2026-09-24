// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_stage with a bare int where an execution context belongs.  The
// body is a stage; the context is the only thing wrong.

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
    int not_a_ctx = 0;
    FakeConsumer<int> in;
    FakeProducer<int> out;

    auto bad = fixy::concurrent::mint_stage<&pass_through>(not_a_ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
