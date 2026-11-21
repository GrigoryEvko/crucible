// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A graph of two stages with one edge.  The two handles of the edge name one
// channel and carry one payload, and none of them states a capacity of the
// channel.  No capacity of the channel is known, so the edge check refuses
// the edge, and the port check never runs.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

namespace {

namespace cc = fixy::concurrent;

template <typename T>
struct FakeChannel {};

// The two handles state no channel_capacity.
template <typename T>
struct UnstatedConsumer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] ::foundation::core::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct UnstatedProducer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void body(UnstatedConsumer<int>&&, UnstatedProducer<int>&&) noexcept {}

using Stage = cc::Stage<&body, fixy::HotFgCtx>;
using UnstatedGraph = cc::StageGraph<cc::StagePack<Stage, Stage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

// The size is the one use of the dag, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<UnstatedGraph>);

}  // namespace

int main() { return 0; }
