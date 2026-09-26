// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Calibration is startup or background work, not a foreground hot-path
// operation, so the context gate refuses the foreground context. The
// foreground context is built only from the producer claim, so the
// fixture names it in an unevaluated operand.

#include <crucible/cog/Calibrate.h>

#include <fixy/Ctx.h>

#include <utility>

namespace cog = crucible::cog;

using Refused = decltype(cog::calibrate_cog<cog::CogKind::Gpu>(std::declval<::fixy::HotFgCtx const&>(), cog::CogIdentity{}));

int main() { return 0; }
