// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A body that declares exec_ctx_type is opting into the ExecCtx safety
// axis.  Malformed context metadata must be rejected, not silently
// ignored by the typed autosplit planner.

#include <crucible/concurrent/AutoSplit.h>

namespace {

struct BadCtxBody {
    using exec_ctx_type = int;

    void operator()(crucible::concurrent::AutoSplitShard) const noexcept {}
};

constexpr auto bad_hint =
    crucible::concurrent::infer_workload_hint<BadCtxBody>();

}  // namespace

int main() { return 0; }
