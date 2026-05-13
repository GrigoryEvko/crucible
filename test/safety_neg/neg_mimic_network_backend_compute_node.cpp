// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/mimic/_wip/network/Backend.h>

namespace ir = crucible::forge::ir001;
namespace mb = crucible::mimic::_wip::network;

using ComputeNode = ir::Ir001Node<ir::Ir001OpKind::Gemm, ir::TensorPort>;

static_assert(mb::NetworkKernelNode<ComputeNode>);
