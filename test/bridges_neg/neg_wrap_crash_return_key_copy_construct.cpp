// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-032 — HS14 fixture #1.  Asserts that the
// `WrapCrashReturnKey` passkey CANNOT be copy-constructed.
//
// Pre-fix the compiler implicitly synthesized a PUBLIC copy
// ctor on `WrapCrashReturnKey` (an empty class with only a
// private default ctor).  A reference to a key — say one
// obtained via lambda capture, template-friend cheat, or a
// hypothetical friend method that returned by value — could
// be duplicated via `auto bad = k;`.  Post-fix the copy ctor
// is `= delete("fixy-A2-032: passkey cannot be copied")`,
// so this same expression becomes a hard compile error.
//
// Companion fixture #2 (`neg_wrap_crash_return_key_move_construct`)
// pins the move-ctor side; together they witness that an
// adversary holding a `WrapCrashReturnKey&` (or `&&`) cannot
// turn the borrow into ownership.  The CrashEvent ctor still
// works because C++17 mandatory copy-elision binds the
// `WrapCrashReturnAuthorizer::mint()` prvalue directly to the
// CrashEvent ctor's value parameter — no copy/move construction
// is invoked on the production path.
//
// Different access pattern than fixture #2 (copy vs move) pins
// that the rejection is direction-agnostic: post-fix BOTH the
// `auto bad = k;` (lvalue → lvalue copy) and the
// `auto bad = std::move(k);` (rvalue → lvalue move) paths
// are rejected by the type system, not by access control.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "use of deleted function" | "passkey cannot be copied"
//   | "fixy-A2-032" | "deleted"

#include <crucible/bridges/CrashTransport.h>

namespace {

// Force-instantiate a function whose body copy-constructs a
// `WrapCrashReturnKey`.  Taking the key by reference compiles
// (only the parameter type, not its visibility, matters); the
// body's `auto bad = k;` is the copy that fires the deleted ctor.
[[maybe_unused]] void copy_construction_must_fail(
    const ::crucible::safety::proto::WrapCrashReturnKey& k) {
    auto bad = k;
    (void)bad;
}

}  // namespace

int main() {
    return 0;
}
