// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-086: PipelineDag rejects StageGraph nodes that have no incident
// edges when the graph has more than one node.  Such a stage has no
// graph producer and feeds no graph consumer, so it is unreachable
// glue rather than a connected pipeline node.

#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>

namespace conc = crucible::concurrent;
namespace eff = crucible::effects;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void body(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using Stage = conc::Stage<&body, eff::HotFgCtx>;
using UnreachableGraph = conc::StageGraph<
    conc::StagePack<Stage, Stage, Stage>,
    conc::EdgePack<conc::StageEdge<0, 1>>>;

using Bad = conc::PipelineDag<UnreachableGraph>;

int main() { return 0; }
