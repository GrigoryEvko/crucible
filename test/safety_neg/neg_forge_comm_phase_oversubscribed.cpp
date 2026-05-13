// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/Phases/Comm.h>

namespace phase = crucible::forge::phases::comm;
namespace ir = crucible::forge::ir001;

using OversubscribedComputeNode = ir::Ir001Node<
    ir::Ir001OpKind::Gemm,
    ir::TensorPort,
    crucible::effects::ConcurrentRow<crucible::effects::SmBudget<999>>>;
using SendNode = ir::Ir001Node<
    ir::Ir001OpKind::SendAsync,
    ir::PointToPointAttrs,
    crucible::effects::ConcurrentRow<crucible::effects::NvlinkBandwidth<1>>>;

static_assert(phase::CommFusionEligible<
              OversubscribedComputeNode,
              SendNode,
              phase::CommFusionPattern::SendFromEpilogue,
              crucible::cog::CogKind::Gpu>);
