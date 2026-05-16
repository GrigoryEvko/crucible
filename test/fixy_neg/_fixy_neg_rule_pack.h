// ── Shared scaffold for fixy_neg/neg_fixy_rule_*.cpp fixtures ──────
//
// Each rule fixture builds an all-engaged grant pack, optionally
// substitutes one or two axis grants to push an axis off its strict
// default, opens crucible::safety::fn::collision and specialises one
// or more `marks_*` traits on the resolved safety_fn_t, then
// instantiates `fixy::fn<Type, Grants...>` to drive the resolved
// `safety::fn::Fn<...>` class-body validate() static_asserts.  The
// targeted rule's diagnostic (e.g. "I002:") appears in the compile
// error and is matched by the neg_compile_driver.
//
// Common helpers shared across all rule fixtures live here.

#pragma once

#include <crucible/fixy/Fn.h>

namespace fixy_neg_rule_detail {

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace sfn  = crucible::safety::fn;
namespace eff  = crucible::effects;

using D = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

}  // namespace fixy_neg_rule_detail
