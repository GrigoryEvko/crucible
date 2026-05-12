// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 for GAPS-141. Probe outcomes are published by
// background workers only; foreground hot-path context has Row<> and
// must not satisfy CtxFitsSyntheticProbeRecord.

#include <crucible/observe/SyntheticProbe.h>

namespace eff = crucible::effects;
namespace observe = crucible::observe;

template <eff::IsExecCtx Ctx>
    requires observe::CtxFitsSyntheticProbeRecord<Ctx>
constexpr int record_gate() noexcept { return 1; }

static_assert(record_gate<eff::HotFgCtx>() == 1,
    "GAPS-141: foreground context must not record synthetic probes.");

int main() { return 0; }
