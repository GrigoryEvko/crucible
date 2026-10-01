// SPDX-License-Identifier: Apache-2.0
//
// A precondition that fires during constant evaluation as well as at
// runtime, whatever the shape of the enclosing function.
//
// The native contract clause is not enough on GCC 16.1.1.  A clause in
// the parser-special position ahead of the body is silently skipped
// during constant evaluation for most parameter shapes, and a variant
// of the compiler that fixes that breaks always-true postconditions on
// constexpr functions with foldable bodies.  Writing the check in the
// function body instead sidesteps the constexpr-cache machinery that
// causes both.
//
// GCC 16 also does not keep the contract clause of a template in a
// header unit or in a precompiled header.  So the tree has no language
// contract clause.  The quarantine plugin of utils/tools/quarantine/
// rejects each `pre` and each `post` clause in each build, and its error
// names these macros.
//
// The consteval arm calls `__builtin_trap()`, which is not a constant
// expression.  Reaching it poisons the enclosing constant evaluation,
// and that is what a static_assert reports as a failure.  At runtime
// the `if consteval` arm is dead and is gone before codegen.
//
// The runtime arm obeys the contract evaluation semantic of the
// translation unit, as a language contract clause does.  The build sets
// that semantic in CMakeLists.txt, SECTION 6 and SECTION 6b.  Debug
// enforces it, and Release observes it through a handler that aborts.
// So a precondition does its check in a Release library, as it does in
// Debug.  NDEBUG has no effect on these macros.
//
// A translation unit on the `ignore` semantic also gets the define
// CRUCIBLE_CONTRACT_SEMANTIC_IGNORE, from the same list of options,
// CRUCIBLE_CONTRACT_IGNORE_OPTIONS.  GCC gives no macro for the
// semantic.  Without the define, a header cannot know the semantic.
// There the runtime arm has no check, and an `[[assume]]` gives the
// condition to the optimizer.  Only the build system sets the define.
// The configure step rejects a target or a source file that has the
// define without the flag, or the flag without the define.
//
// A function with the attribute [[gnu::const]] or [[gnu::pure]] can use
// these macros only where the optimizer is permitted to remove the
// check.  The runtime arm calls the violation handler, and that call has
// side effects.
//
// Each translation unit that instantiates a template with one of these
// macros references the violation handler, as a language contract clause
// does.  The message form also references contract_failed_msg.  So a
// library or a program that instantiates such a template links
// `foundation`, which defines the two functions.
//
// The violation handler and the reporting function of the message form
// are in src/foundation/ContractHandler.cpp.  Every binary that links
// `foundation` contains them.  The macro names keep their CRUCIBLE_
// prefix for the reason foundation/Platform.h gives: macros have no
// namespace, and the layer rule is stated over namespace roots.

#pragma once

#include <foundation/Platform.h>

namespace foundation::detail {

// Writes the predicate text, the source location and the message of the
// caller to file descriptor 2 with async-signal-safe calls only.  Then it
// stops at a breakpoint if a debugger is attached, and aborts.  A second
// violation on the thread while it reports aborts at once.
[[noreturn, gnu::cold]]
void contract_failed_msg(char const* expr, char const* file, int line, char const* fn, char const* msg) noexcept;

}  // namespace foundation::detail

#if defined(CRUCIBLE_CONTRACT_SEMANTIC_IGNORE)

// The ignore arm keeps the consteval check.  A negative-compile fixture
// then has the same result in each translation unit.
#define CRUCIBLE_PRE(cond)              \
    do {                                \
        if consteval {                  \
            if (!(cond)) [[unlikely]] { \
                __builtin_trap();       \
            }                           \
        }                               \
        CRUCIBLE_CONTRACT_FENCE_();     \
        [[assume(cond)]];               \
    } while (0)

#else

// No `[[assume]]` follows the check.  Under the observe semantic a
// handler can return, and an assumption of a false condition is
// undefined behaviour.  An optimizer can also use an assumption to
// delete the check before it.
//
// The handler reads the predicate text from the language clause.  GCC
// 16.2.1 gives the wrong text when the first or the last token of the
// predicate comes from a macro expansion.  The file, the line and the
// function in the report stay correct.
#define CRUCIBLE_PRE(cond)              \
    do {                                \
        if consteval {                  \
            if (!(cond)) [[unlikely]] { \
                __builtin_trap();       \
            }                           \
        } else {                        \
            contract_assert(cond);      \
        }                               \
    } while (0)

#endif

// The violation path here is a bare trap with no message, so debugging
// falls back to the core dump.  That is the whole point of the variant.
// Reach for it only where the cost of formatting and flushing the
// diagnostic has been measured and found to matter.
//
// The contract semantic selects if the fast form does a check, as for
// the plain form.  The trap calls no function, and it does not cause a
// link gap.
#if defined(CRUCIBLE_CONTRACT_SEMANTIC_IGNORE)

#define CRUCIBLE_PRE_FAST(cond) CRUCIBLE_PRE(cond)

#else

#define CRUCIBLE_PRE_FAST(cond)     \
    do {                            \
        if (!(cond)) [[unlikely]] { \
            __builtin_trap();       \
        }                           \
    } while (0)

#endif

// The message reaches the runtime report only.  A trap during constant
// evaluation carries no extra text either way.  The message must be a
// string literal.  The `""` before it causes a compile error for a
// message of a different kind.
//
// A language contract clause has no message.  Because of this, the
// checking arm of this form calls a reporting function of its own.  That
// function aborts, as the handler of this project does.  A program that
// replaces the handler does not change that function.
#if defined(CRUCIBLE_CONTRACT_SEMANTIC_IGNORE)

#define CRUCIBLE_PRE_MSG(cond, msg) \
    do {                            \
        (void)("" msg);             \
        CRUCIBLE_PRE(cond);         \
    } while (0)

#else

#define CRUCIBLE_PRE_MSG(cond, msg)                                                                  \
    do {                                                                                             \
        if (!(cond)) [[unlikely]] {                                                                  \
            if consteval {                                                                           \
                __builtin_trap();                                                                    \
            } else {                                                                                 \
                ::foundation::detail::contract_failed_msg(#cond, __builtin_FILE(), __builtin_LINE(), \
                                                          __PRETTY_FUNCTION__, "" msg);              \
            }                                                                                        \
        }                                                                                            \
    } while (0)

#endif

// An optimizer that reasons from undefined behaviour is entitled to
// delete code standing BEFORE an `[[assume(cond)]]`, on the grounds
// that the code could not have run if the condition were false there.
// The checkpoint is a sequence point the optimizer may not carry that
// reasoning backward across.  It costs no machine instructions.  Only
// the ignore arm has an assumption, and only that arm uses the
// checkpoint.
//
// The hardening is opt-in because the compiler in use does not exploit
// the hole today.  Enable it for translation units where the
// theoretical hole would carry real cost, such as key derivation and
// other secret handling.
#if defined(CRUCIBLE_CONTRACT_OBSERVABLE)
#define CRUCIBLE_CONTRACT_FENCE_() __builtin_observable_checkpoint()
#else
#define CRUCIBLE_CONTRACT_FENCE_() ((void)0)
#endif
