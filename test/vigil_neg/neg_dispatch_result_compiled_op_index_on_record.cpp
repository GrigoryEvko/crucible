// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for the precondition of
// DispatchResult::compiled_op_index().
//
// Companion to neg_dispatch_result_compiled_status_on_record.cpp, which
// gives the full background.  compiled_op_index() reads the payload of the
// COMPILED arm, and CRUCIBLE_PRE(action == Action::COMPILED) in its body
// refuses a read from the RECORD arm.  A vanilla P2900 `pre` clause that
// reads `this->action` is skipped at consteval for a foldable body such as
// `return op_index;`, so the check runs from the body.
//
// WHAT THIS FIXTURE PROVES
// ────────────────────────
// The accessor is constexpr, so the constant evaluation below enters its
// body.  A default DispatchResult has action == RECORD, the check finds
// `action == COMPILED` false, and CRUCIBLE_PRE executes __builtin_trap(),
// which is not a constant expression.  The initializer of `witness` is then
// not a constant expression, and the build fails.  Without the check, the
// evaluation returns the meaningless op_index of the RECORD arm and the file
// compiles.
//
// Expected diagnostic (the CMakeLists regexes): the constant evaluation of
// result.compiled_op_index() reaches the CRUCIBLE_PRE of CrucibleContext.h.
// test/test_vigil_dispatch.cpp makes the same call on a COMPILED result in
// the same constant context, and that call compiles.

#include <crucible/CrucibleContext.h>

namespace {

// Default-constructed DispatchResult has action == RECORD, so the
// precondition of compiled_op_index() (action == COMPILED) is violated.
// CRUCIBLE_PRE's __builtin_trap fires at consteval.
[[maybe_unused]] constexpr crucible::OpIndex witness = [] {
    crucible::DispatchResult result{};  // action == RECORD
    return result.compiled_op_index();  // CRUCIBLE_PRE(action == COMPILED) VIOLATED
}();

}  // namespace

int main() { return 0; }
