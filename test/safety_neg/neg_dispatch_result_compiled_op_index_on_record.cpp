// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for the fix-10 consteval-bypass
// migration of DispatchResult::compiled_op_index().
//
// Companion to neg_dispatch_result_compiled_status_on_record.cpp.  Same
// rationale (see that fixture's doc-block for the full consteval-bypass
// background): CrucibleContext.h's DispatchResult::compiled_op_index()
// guarded its COMPILED-arm payload accessor with
//
//     pre (action == Action::COMPILED)   // OLD — vanilla P2900
//
// a `this->action` member-predicate that the un-patched distro GCC
// 16.1.1 silently bypasses at consteval for the foldable body
// (`return op_index;`).  fix-10 migrated it to the in-body CRUCIBLE_PRE
// macro, which fires on both the patched and un-patched toolchains.
//
// WHAT THIS FIXTURE PROVES
// ────────────────────────
// Calling compiled_op_index() on a default (RECORD) DispatchResult in a
// constant-expression context must FAIL: the migrated CRUCIBLE_PRE finds
// `action == COMPILED` false and traps at consteval.  With the OLD
// vanilla pre() this compiled clean on the un-patched build, leaking a
// meaningless op_index from the RECORD arm.
//
// Pairs with the compiled_status() fixture to cover BOTH migrated
// accessors of the same `action == COMPILED` predicate shape.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/CrucibleContext.h>

namespace {

// Default-constructed DispatchResult has action == RECORD, so
// compiled_op_index()'s migrated precondition (action == COMPILED) is
// violated.  CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr crucible::OpIndex witness = [] {
    crucible::DispatchResult result{};   // action == RECORD
    return result.compiled_op_index();   // pre(action == COMPILED) VIOLATED
}();

}  // namespace

int main() { return 0; }
