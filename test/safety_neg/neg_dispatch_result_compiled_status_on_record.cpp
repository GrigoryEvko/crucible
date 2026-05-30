// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for the fix-10 consteval-bypass
// migration of DispatchResult::compiled_status().
//
// BACKGROUND
// ──────────
// CrucibleContext.h's DispatchResult::compiled_status() guards its
// COMPILED-arm payload accessor with a precondition on the `this->action`
// discriminant:
//
//     pre (action == Action::COMPILED)   // OLD — vanilla P2900
//
// On the un-patched distro GCC 16.1.1, a P2900 `pre()` clause whose
// predicate touches a class member through `this->` is silently bypassed
// at consteval for foldable-bodied functions (the documented consteval-
// bypass family).  compiled_status() is exactly such a function:
// foldable body (`return status;`), member-predicate `pre()`.  On the
// un-patched toolchain the guard becomes a consteval no-op, so a constexpr
// caller reaching for the COMPILED payload on a RECORD result compiles
// CLEAN — silently reading a meaningless `status` field.
//
// fix-10 migrated the clause to the in-body CRUCIBLE_PRE macro, which
// lives in the function BODY (not the parser-special pre-clause position)
// and therefore fires across BOTH the patched and un-patched builds.
//
// WHAT THIS FIXTURE PROVES
// ────────────────────────
// Constructing a default DispatchResult (action == RECORD) and calling
// compiled_status() in a constant-expression context must FAIL to
// compile.  The migrated CRUCIBLE_PRE evaluates `action == COMPILED`,
// finds it false, and executes `__builtin_trap()` inside `if consteval`
// — a non-constexpr call that poisons the surrounding constexpr
// evaluation into "non-constant condition".  With the OLD vanilla
// pre() this fixture would have compiled clean on the un-patched build
// (silent bypass), letting an out-of-arm payload read slip through.
//
// Distinct from the companion fixture (compiled_op_index_on_record):
// this fixture pins the migrated CRUCIBLE_PRE on compiled_status(); the
// companion pins the second migrated accessor compiled_op_index().  Same
// predicate shape (`action == COMPILED`), two distinct migrated function
// bodies — defense-in-depth per the §XXI mint-pattern HS14 discipline.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/CrucibleContext.h>

namespace {

// Default-constructed DispatchResult has action == RECORD, so
// compiled_status()'s migrated precondition (action == COMPILED) is
// violated.  CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr crucible::ReplayStatus witness = [] {
    crucible::DispatchResult result{};   // action == RECORD
    return result.compiled_status();     // pre(action == COMPILED) VIOLATED
}();

}  // namespace

int main() { return 0; }
