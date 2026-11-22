// Negative compile test.  This file must fail to compile.
//
// A graph of two stages with one edge.  The two handles of the edge name one
// channel and carry one payload.  The output handle states a capacity of
// one value, and the input handle states none.  No capacity comes from one
// handle alone, so the edge check refuses the edge with the fault that
// names the input handle, and the port check never runs.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <cstddef>

namespace {

namespace cc = fixy::concurrent;

template <typename T>
struct FakeChannel {};

// The input handle states no channel_capacity.
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

inline void body(UnstatedConsumer<int>&&, StatedProducer<int>&&) noexcept {}

using Stage = cc::Stage<&body, fixy::HotFgCtx>;
using InputUnstatedGraph = cc::StageGraph<cc::StagePack<Stage, Stage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

// The size is the one use of the dag, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<InputUnstatedGraph>);

}  // namespace

int main() { return 0; }
