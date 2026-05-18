// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-032 — HS14 fixture #2.  Asserts that the
// `WrapCrashReturnKey` passkey CANNOT be move-constructed.
//
// Pre-fix the compiler implicitly synthesized a PUBLIC move
// ctor on `WrapCrashReturnKey` (an empty class with only a
// private default ctor).  Any path that obtained an rvalue
// reference — return-by-value from a hypothetical friend
// method, a `std::move` on a captured lambda, the right-hand
// side of a `decltype(auto)` chain — could mint a fresh
// instance via `auto bad = std::move(k);`.  Post-fix the
// move ctor is `= delete("fixy-A2-032: passkey cannot be
// moved")`, so the same expression is a hard compile error.
//
// Companion fixture #1 (`neg_wrap_crash_return_key_copy_construct`)
// pins the copy-ctor side; together they witness that the
// passkey is fungibility-free under both copy AND move.  The
// CrashEvent ctor's `WrapCrashReturnKey` value parameter is
// reached only via C++17 mandatory copy-elision from a prvalue
// — no construction happens on the production path.
//
// Why this matters (passkey-fungibility class):
//   The H-25 closure depends on `WrapCrashReturnKey` being
//   the unforgeable witness of authority.  If the key can be
//   duplicated (copy OR move) post-mint, then "exactly one"
//   authorization collapses to "at least one, then unlimited
//   replays."  Deleting both copy AND move forecloses the
//   entire class of duplication attacks at zero call-site cost.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "use of deleted function" | "passkey cannot be moved"
//   | "fixy-A2-032" | "deleted"

#include <crucible/bridges/CrashTransport.h>

#include <utility>

namespace {

// Force-instantiate a function whose body move-constructs a
// `WrapCrashReturnKey` from an rvalue reference parameter.
// Taking the key by `&&` compiles (parameter type only).  The
// body's `auto bad = std::move(k);` is the move that fires
// the deleted ctor.
[[maybe_unused]] void move_construction_must_fail(
    ::crucible::safety::proto::WrapCrashReturnKey&& k) {
    auto bad = std::move(k);
    (void)bad;
}

}  // namespace

int main() {
    return 0;
}
