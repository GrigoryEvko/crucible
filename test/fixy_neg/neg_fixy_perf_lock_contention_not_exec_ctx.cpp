// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// crucible::fixy::perf::mint_lock_contention is the re-export of the
// perf mint.  Its gate first asks for an execution context.
// NotAnExecCtx has no row, so the gate rejects it.  The second argument
// is a valid startup load context, so the gate is the one reason that
// the compiler rejects the call.

#include <crucible/fixy/Perf.h>

namespace test_fixy_perf_lock_contention_not_exec_ctx {

struct NotAnExecCtx {};

}  // namespace test_fixy_perf_lock_contention_not_exec_ctx

int main() {
    auto hub = crucible::fixy::perf::mint_lock_contention(test_fixy_perf_lock_contention_not_exec_ctx::NotAnExecCtx{},
                                                          ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
