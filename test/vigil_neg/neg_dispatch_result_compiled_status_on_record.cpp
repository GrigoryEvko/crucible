// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for the precondition of
// DispatchResult::compiled_status().
//
// BACKGROUND
// ──────────
// DispatchResult in CrucibleContext.h is a discriminated pair: `action` is
// the discriminant, and `status` and `op_index` are the payload of the
// COMPILED arm.  compiled_status() reads that payload, so its body starts
// with CRUCIBLE_PRE(action == Action::COMPILED).
//
// On the un-patched distro GCC 16.1.1, a P2900 `pre()` clause whose
// predicate reads a class member through `this->` is silently skipped at
// consteval for a function with a foldable body (the documented
// consteval-bypass family).  compiled_status() has such a body
// (`return status;`).  CRUCIBLE_PRE lives in the function BODY, not in the
// clause position, so it fires on the patched and the un-patched builds.
//
// WHAT THIS FIXTURE PROVES
// ────────────────────────
// The accessor is constexpr, so the constant evaluation below enters its
// body.  A default DispatchResult has action == RECORD, the check finds
// `action == COMPILED` false, and CRUCIBLE_PRE executes __builtin_trap()
// inside `if consteval`.  That call is not a constant expression, so the
// initializer of `witness` is not one either, and the build fails.  Without
// the check, the evaluation returns the meaningless status of the RECORD arm
// and the file compiles.
//
// Distinct from the companion fixture (compiled_op_index_on_record): this
// fixture pins the check in compiled_status(), and the companion pins the
// check in compiled_op_index().  The two checks have the same predicate in
// two function bodies, so each needs its own witness.
//
// Expected diagnostic (the CMakeLists regexes): the constant evaluation of
// result.compiled_status() reaches the CRUCIBLE_PRE of CrucibleContext.h.
// test/test_vigil_dispatch.cpp makes the same call on a COMPILED result in
// the same constant context, and that call compiles.

#include <crucible/CrucibleContext.h>

namespace {

// Default-constructed DispatchResult has action == RECORD, so the
// precondition of compiled_status() (action == COMPILED) is violated.
// CRUCIBLE_PRE's __builtin_trap fires at consteval.
[[maybe_unused]] constexpr crucible::ReplayStatus witness = [] {
    crucible::DispatchResult result{};  // action == RECORD
    return result.compiled_status();  // CRUCIBLE_PRE(action == COMPILED) VIOLATED
}();

}  // namespace

int main() { return 0; }
