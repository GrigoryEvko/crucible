// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Probe outcomes are published by background workers only.  The
// foreground hot-path context has Row<> and must not satisfy
// CtxFitsSyntheticProbeRecord.

#include <crucible/observe/SyntheticProbe.h>

namespace observe = crucible::observe;

template <::foundation::effects::IsExecCtx Ctx>
    requires observe::CtxFitsSyntheticProbeRecord<Ctx>
constexpr int record_gate() noexcept {
    return 1;
}

static_assert(record_gate<::fixy::HotFgCtx>() == 1, "a foreground context must not record synthetic probes");

int main() { return 0; }
