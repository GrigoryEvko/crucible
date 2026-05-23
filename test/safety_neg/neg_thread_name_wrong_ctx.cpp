// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-189 ThreadName, mismatch class #2 of 2:
// NAMING ATTEMPTED FROM A NON-INIT-PHASE CONTEXT.
//
// pthread_setname_np is a syscall (writes /proc/self/task/<tid>/comm); it
// belongs in the init phase, not in bg-drain or on the hot path.  The mint
// admits only a context whose effect-row engages Effect::Init
// (CtxIsInitPhase) — a Bg context carries Bg/Alloc but not Init, so feeding
// it to mint_thread_name MUST be a compile error.
//
// Distinct from neg_thread_name_too_long.cpp (a value-level TASK_COMM_LEN
// bound); here the failure is the §XXI ctx-gate constraint at the call.
//
// Expected diagnostic: constraints not satisfied / no matching function /
// CtxIsInitPhase / mint_thread_name.

#include <crucible/safety/ThreadName.h>

#include <crucible/effects/Capabilities.h>

int main() {
    auto bg = ::crucible::effects::testing::bg();

    // Should FAIL: Bg is not an init-phase context; CtxIsInitPhase<Bg> is
    // false, so mint_thread_name has no viable specialization.
    auto witness = ::crucible::safety::mint_thread_name<"crucible-bg">(bg);

    return static_cast<int>(witness.visible_length());
}
