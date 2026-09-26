// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// CogMimic binds a Cog that Mimic can compile for: a compute, network or
// memory substrate.  A power rail, a sensor or a container has no
// operations to emit code for, so CtxFitsCogMimic refuses such a kind at
// the substrate-family conjunct even under a context that may mint.
//
// The companion fixture neg_cog_mimic_ctx_row_missing.cpp refuses at the
// context-row conjunct, a distinct mismatch class.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

template <cog::CogKind K, ::foundation::effects::IsExecCtx Ctx>
    requires mimic::CtxFitsCogMimic<Ctx, K>
constexpr int allocate_cog_mimic_slot() noexcept {
    return 1;
}

static_assert(allocate_cog_mimic_slot<cog::CogKind::PsuRail, ::fixy::ColdInitCtx>() == 1,
              "CtxFitsCogMimic must refuse a kind that is not a Mimic substrate.");

int main() { return 0; }
