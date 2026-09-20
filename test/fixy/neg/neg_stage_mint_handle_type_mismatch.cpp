// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The factory's parameters are the body's own handle types, spelled from
// the body's signature rather than deduced from the arguments, so a
// consumer of the wrong element type does not bind.  Both handles pass
// the shape predicates; only the exact type is wrong, which is the gate
// the concept alone would not catch.

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

inline void int_to_int(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx;
    FakeConsumer<float> wrong_element_type;
    FakeProducer<int> out;

    auto bad = fixy::concurrent::mint_stage<&int_to_int>(ctx, std::move(wrong_element_type), std::move(out));
    (void)bad;
    return 0;
}
