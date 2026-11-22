// Negative compile test.  This file must fail to compile.
//
// A linear pipeline of two stages.  The output handle of the first stage
// states a capacity of one value, and the input handle of the second stage
// states none.  The chain gate refuses the link through the term
// StatesHandleCapacity of HandlesAgreeOnCapacity, and the refusal names the
// input handle that states no capacity.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <cstddef>

namespace {

namespace cc = fixy::concurrent;

template <typename T>
struct FakeChannel {};

template <typename T>
struct StatedConsumer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t channel_capacity = 1;
    [[nodiscard]] ::foundation::core::Option<T> try_pop() noexcept { return {}; }
};

// The input handle of the second stage states no channel_capacity.
template <typename T>
struct UnstatedConsumer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] ::foundation::core::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct StatedProducer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t channel_capacity = 1;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void first_body(StatedConsumer<int>&&, StatedProducer<int>&&) noexcept {}
inline void second_body(UnstatedConsumer<int>&&, StatedProducer<int>&&) noexcept {}

using First = cc::Stage<&first_body, fixy::HotFgCtx>;
using Second = cc::Stage<&second_body, fixy::HotFgCtx>;

// The size is the one use of the pipeline, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_pipeline_size = sizeof(cc::Pipeline<First, Second>);

}  // namespace

int main() { return 0; }
