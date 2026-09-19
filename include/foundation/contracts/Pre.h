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
// The consteval arm calls `__builtin_trap()`, which is not a constant
// expression.  Reaching it poisons the enclosing constant evaluation,
// and that is what a static_assert reports as a failure.  At runtime
// the `if consteval` arm is dead and is gone before codegen, so a
// release build pays nothing for the compile-time enforcement.
//
// A function marked [[gnu::const]] or [[gnu::pure]] may only use these
// macros where enforcement in release builds alone is acceptable.  The
// release arm is pure, but the debug arm calls out to a reporting
// function that has side effects.
//
// Do not use these macros in a free function template whose parameter
// types are deduced at the call site.  Every consumer translation unit
// that instantiates the template then references the reporting symbol,
// and a static library that does not link the handler fails at link
// time.  A member of a class template is safe, because it instantiates
// once per fixed class parameter.
//
// The reporting functions are defined in src/foundation/ContractHandler.cpp,
// which every binary that links `foundation` carries.  The macro names keep
// their CRUCIBLE_ prefix for the reason foundation/Platform.h gives: macros
// have no namespace, and the layer rule is stated over namespace roots.
//
// Old spelling: include/crucible/safety/Pre.h.

#pragma once

#include <foundation/Platform.h>

namespace foundation::detail {

// Prints the predicate text and the source location to stderr, adds a
// stack trace where the standard library supplies one, offers an
// attached debugger a breakpoint, then aborts.
[[noreturn, gnu::cold]]
void contract_failed(char const* expr, char const* file, int line, char const* fn) noexcept;

// Adds a note line carrying the caller's message between the source
// location and the stack trace.
[[noreturn, gnu::cold]]
void contract_failed_msg(char const* expr, char const* file, int line, char const* fn, char const* msg) noexcept;

}  // namespace foundation::detail

#ifdef NDEBUG

// The release arm keeps the consteval check rather than collapsing to
// the hint alone, so a negative-compile fixture behaves the same in
// release as in debug.
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

#elif defined(CRUCIBLE_CONTRACT_RUNTIME_OFF)

// This arm drops the runtime path without defining NDEBUG.  A static
// library whose archive does not carry the reporting function still
// references it through any inline header use of these macros, and a
// left-to-right archive scan can discard the defining object before it
// sees that reference.  Defining this macro on the inner target removes
// the reference and closes the link gap, at the cost of runtime
// enforcement inside that one library.
//
// Only the build system defines this, per target.  Defining it in a
// header would silently disarm every consumer.
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

#define CRUCIBLE_PRE(cond)                                                                                             \
    do {                                                                                                               \
        if (!(cond)) [[unlikely]] {                                                                                    \
            if consteval {                                                                                             \
                __builtin_trap();                                                                                      \
            } else {                                                                                                   \
                ::foundation::detail::contract_failed(#cond, __builtin_FILE(), __builtin_LINE(), __PRETTY_FUNCTION__); \
            }                                                                                                          \
        }                                                                                                              \
        CRUCIBLE_CONTRACT_FENCE_();                                                                                    \
        [[assume(cond)]];                                                                                              \
    } while (0)

#endif

// The violation path here is a bare trap with no message, so debugging
// falls back to the core dump.  That is the whole point of the variant.
// Reach for it only where the cost of formatting and flushing the
// diagnostic has been measured and found to matter.
#ifdef NDEBUG

#define CRUCIBLE_PRE_FAST(cond)         \
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

#define CRUCIBLE_PRE_FAST(cond)     \
    do {                            \
        if (!(cond)) [[unlikely]] { \
            __builtin_trap();       \
        }                           \
        CRUCIBLE_CONTRACT_FENCE_(); \
        [[assume(cond)]];           \
    } while (0)

#endif

// The message reaches the runtime report only.  A trap during constant
// evaluation carries no extra text either way.
#if defined(NDEBUG) || defined(CRUCIBLE_CONTRACT_RUNTIME_OFF)

#define CRUCIBLE_PRE_MSG(cond, msg)     \
    do {                                \
        if consteval {                  \
            if (!(cond)) [[unlikely]] { \
                __builtin_trap();       \
            }                           \
        }                               \
        (void)(msg);                    \
        CRUCIBLE_CONTRACT_FENCE_();     \
        [[assume(cond)]];               \
    } while (0)

#else

#define CRUCIBLE_PRE_MSG(cond, msg)                                                                  \
    do {                                                                                             \
        if (!(cond)) [[unlikely]] {                                                                  \
            if consteval {                                                                           \
                __builtin_trap();                                                                    \
            } else {                                                                                 \
                ::foundation::detail::contract_failed_msg(#cond, __builtin_FILE(), __builtin_LINE(), \
                                                          __PRETTY_FUNCTION__, (msg));               \
            }                                                                                        \
        }                                                                                            \
        CRUCIBLE_CONTRACT_FENCE_();                                                                  \
        [[assume(cond)]];                                                                            \
    } while (0)

#endif

// An optimizer that reasons from undefined behaviour is entitled to
// delete code standing BEFORE an `[[assume(cond)]]`, on the grounds
// that the code could not have run if the condition were false there.
// The checkpoint is a sequence point the optimizer may not carry that
// reasoning backward across.  It costs no machine instructions.
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
