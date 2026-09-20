// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A three-stage graph with one edge: the third stage has no edge in
// either direction, so nothing in the graph produces for it and it feeds
// nothing.  The connectivity walk refuses it.

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
using UnreachableGraph = cc::StageGraph<cc::StagePack<Stage, Stage, Stage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

using Bad = cc::PipelineDag<UnreachableGraph>;

}  // namespace

int main() { return 0; }
