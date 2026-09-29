// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/_wip/Phases/Comm.h>

namespace phase = crucible::forge::_wip::phases::comm;
namespace ir = crucible::forge::ir001;

using OversubscribedComputeNode =
    ir::Ir001Node<ir::Ir001OpKind::Gemm, ir::TensorPort,
                  ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<999>>>;
using SendNode = ir::Ir001Node<ir::Ir001OpKind::SendAsync, ir::PointToPointAttrs,
                               ::foundation::effects::ConcurrentRow<::foundation::effects::NvlinkBandwidth<1>>>;

static_assert(phase::CommFusionEligible<OversubscribedComputeNode, SendNode, phase::CommFusionPattern::SendFromEpilogue,
                                        crucible::cog::CogKind::Gpu>);
