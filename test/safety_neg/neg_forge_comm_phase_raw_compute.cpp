// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/_wip/Phases/Comm.h>

namespace phase = crucible::forge::_wip::phases::comm;
namespace ir = crucible::forge::ir001;

using ComputeNode = ir::Ir001Node<
    ir::Ir001OpKind::Gemm,
    ir::TensorPort,
    crucible::effects::ConcurrentRow<crucible::effects::SmBudget<1>>>;
using SendNode = ir::Ir001Node<
    ir::Ir001OpKind::SendAsync,
    ir::PointToPointAttrs,
    crucible::effects::ConcurrentRow<crucible::effects::NvlinkBandwidth<1>>>;

int main() {
    auto constraints = crucible::forge::recipes::query_constraints(
        crucible::NumericalRecipe{});
    ComputeNode raw{};
    SendNode send{};
    auto declared_send = ir::admit_ir001_node(send);
    auto fused = phase::admit_comm_fusion<
        phase::CommFusionPattern::SendFromEpilogue,
        crucible::cog::CogKind::Gpu>(raw, declared_send, constraints);
    (void)fused;
}
