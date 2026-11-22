// Negative-compile test. This file must fail to compile.

#include <crucible/mimic/_wip/network/Backend.h>

namespace ir = crucible::forge::ir001;
namespace mb = crucible::mimic::_wip::network;

using ComputeNode = ir::Ir001Node<ir::Ir001OpKind::Gemm, ir::TensorPort>;

static_assert(mb::NetworkKernelNode<ComputeNode>);
