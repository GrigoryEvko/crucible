// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-086: PipelineDag requires a StageGraph whose edges are in
// topological order.  An edge from a later node back to an earlier node
// is a cycle witness and must be rejected by StageGraphWellFormed.

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
using CyclicGraph = conc::StageGraph<
    conc::StagePack<Stage, Stage>,
    conc::EdgePack<conc::StageEdge<1, 0>>>;

using Bad = conc::PipelineDag<CyclicGraph>;

int main() { return 0; }
