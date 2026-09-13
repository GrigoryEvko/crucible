// SPDX-License-Identifier: Apache-2.0
//
// A postcondition written as a language `post` clause does not always fire at
// consteval on the compilers this project builds with: one build skips the
// clause for most return shapes, the other folds an always-true result out of a
// constant-foldable body.  A macro placed in the function body just before the
// return is subject to neither, which is why these exist.
//
// The caller names the return variable because plain C++ has no equivalent of
// the implicit binding a language post clause introduces.
//
// These add no checking that a precondition macro does not already provide.
// The separate spelling is for the reader: a precondition near the top of a
// body, a postcondition near its return.  A predicate whose consequent
// dereferences a pointer the antecedent proves non-null must use short-circuit
// `||`, because a function-call spelling of implication evaluates both operands.

#pragma once

#include <crucible/safety/Pre.h>

#define CRUCIBLE_POST(retvar, cond) \
    do {                            \
        (void)(retvar);             \
        CRUCIBLE_PRE(cond);         \
    } while (0)

// Traps on violation instead of routing through the diagnostic shim.  The trade
// is the message for the stderr flush it costs.  A core dump still carries the
// stack.
#define CRUCIBLE_POST_FAST(retvar, cond) \
    do {                                 \
        (void)(retvar);                  \
        CRUCIBLE_PRE_FAST(cond);         \
    } while (0)

// Adds a note line to the runtime diagnostic.  At consteval the message is
// unused and the behaviour matches the unannotated form.
#define CRUCIBLE_POST_MSG(retvar, cond, msg) \
    do {                                     \
        (void)(retvar);                      \
        CRUCIBLE_PRE_MSG(cond, msg);         \
    } while (0)
