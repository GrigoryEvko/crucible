// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A three-stage graph with one edge, from the first stage to the second.
// The output of the second stage has no edge, and the third stage has no
// edge at all.  The second stage is not the last stage, so the port check
// refuses its output first.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>

namespace {

namespace cc = fixy::concurrent;

// The one edge joins two handles on one channel, so the edge is valid and
// only the port check can refuse.
template <typename T>
struct FakeChannel {};

template <typename T>
struct FakeConsumer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void body(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using Stage = cc::Stage<&body, fixy::HotFgCtx>;
using UnreachableGraph = cc::StageGraph<cc::StagePack<Stage, Stage, Stage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

// The size is the one use of the dag, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<UnreachableGraph>);

}  // namespace

int main() { return 0; }
