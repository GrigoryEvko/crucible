// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/_wip/Phases/Comm.h>

namespace phase = crucible::forge::_wip::phases::comm;
namespace ir = crucible::forge::ir001;

using RecvNode = ir::Ir001Node<
    ir::Ir001OpKind::RecvAsync,
    ir::PointToPointAttrs,
    crucible::effects::ConcurrentRow<crucible::effects::NvlinkBandwidth<1>>>;
using SendNode = ir::Ir001Node<
    ir::Ir001OpKind::SendAsync,
    ir::PointToPointAttrs,
    crucible::effects::ConcurrentRow<crucible::effects::NvlinkBandwidth<1>>>;

static_assert(phase::CommFusionEligible<
              RecvNode,
              SendNode,
              phase::CommFusionPattern::ReduceOnRecv,
              crucible::cog::CogKind::Gpu>);
