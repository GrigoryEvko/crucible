// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A stage graph whose edges must run in topological order, given an edge
// from a later stage back to an earlier one.  That edge is a cycle
// witness, and the well-formedness gate refuses the graph before a
// PipelineDag over it can be named.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>

namespace {

namespace cc = fixy::concurrent;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void body(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using Stage = cc::Stage<&body, fixy::HotFgCtx>;
using CyclicGraph = cc::StageGraph<cc::StagePack<Stage, Stage>, cc::EdgePack<cc::StageEdge<1, 0>>>;

using Bad = cc::PipelineDag<CyclicGraph>;

}  // namespace

int main() { return 0; }
