#pragma once

// The pointwise walk and the element family in namespace fixy.
//
// The walk lives in foundation/core/Pointwise.h and the element ops in
// foundation/core/Elementwise.h, in namespace foundation::core.  This header
// puts their public names into namespace fixy, so code outside the base
// spells fixy::write_pointwise and fixy::elementwise::Sum.  Each line is a
// using-declaration of the one entity, as in fixy/Core.h.  fixy/Core.h does
// not include this header, so a unit that runs no pointwise walk does not
// parse the walk.

#include <foundation/core/Elementwise.h>
#include <foundation/core/Pointwise.h>

namespace fixy {

using ::foundation::core::CanWritePointwise;
using ::foundation::core::LaneOpOf;
using ::foundation::core::NanRule;
using ::foundation::core::PointwiseInput;
using ::foundation::core::PointwiseOp;
using ::foundation::core::write_pointwise;

// The ops of the element family keep their namespace elementwise.
namespace elementwise = ::foundation::core::elementwise;

}  // namespace fixy
