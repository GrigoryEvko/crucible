// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-189 ThreadName, mismatch class #1 of 2:
// THREAD NAME EXCEEDS TASK_COMM_LEN.
//
// The Linux kernel caps a thread name at TASK_COMM_LEN = 16 bytes (15
// visible chars + NUL) and SILENTLY truncates anything longer.  A
// ThreadNameLiteral<N> with N > 16 must be a COMPILE ERROR so the
// truncation can never reach the kernel — feeding an over-long name to
// mint_thread_name fails at the literal's static_assert, before any syscall.
//
// Distinct from neg_thread_name_wrong_ctx.cpp (a ctx-gate rejection); here
// the failure is the value-level TASK_COMM_LEN bound at NTTP formation.
//
// Expected diagnostic: TASK_COMM_LEN / static assertion / exceeds / too large.

#include <crucible/safety/ThreadName.h>

#include <crucible/effects/Capabilities.h>

int main() {
    auto init = ::crucible::effects::testing::init();

    // 23 visible chars + NUL = 24 > 16 → ThreadNameLiteral static_assert fires.
    auto witness = ::crucible::safety::mint_thread_name<
        "this-thread-name-is-far-too-long">(init);

    return static_cast<int>(witness.visible_length());
}
