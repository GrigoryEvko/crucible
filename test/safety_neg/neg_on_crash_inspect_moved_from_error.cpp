// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-030 — HS14 fixture #2.  Asserts that the `proto::on_crash`
// helper's return cannot have `.error()` (or any other member)
// accessed via chained-member syntax.
//
// Pre-fix `on_crash` returned the input `Expected&&` after moving
// out `result.error()` into the handler.  A caller writing
//   proto::on_crash(*result_ptr, [](auto){}).error()
// or
//   proto::on_crash(*result_ptr, [](auto){}).transform_error([](auto){...})
// silently read MOVED-FROM data: the inner error had been
// `std::move`d into the handler invocation.  Worse — `Expected`'s
// API gives no signal that the access is unsound; the same
// `.error()` member function works pre-handler and post-handler.
//
// Companion fixture #1 (`neg_on_crash_capture_return_value`) pins
// the variable-capture failure mode (`auto leftover = on_crash(...)`).
// This fixture pins the symmetric, more-insidious failure mode:
// CHAINED member access on the temporary return.  Capturing into a
// variable at least surfaces in a code review as "what does the
// developer do with `leftover`?" — chained access hides inside a
// single expression and reads moved-from data inline.
//
// Why this matters (chained-access ubiquity):
//   The pre-fix Expected return invited `.transform_error()`,
//   `.value_or()`, `.error()` chaining as a "natural" idiom.  The
//   surface looked like the C++23 monadic Expected API.  Every
//   such call read moved-from data after the handler ran.  Post-fix
//   the chain itself is a type error: `void` has no members.
//
// Different access pattern than fixture #1 (`.error()` chain vs
// variable capture) pins that the rejection is direction-agnostic:
// post-fix void return blocks BOTH the capture-then-inspect path
// AND the inline-chain path.  Together the pair witnesses fixy-
// A2-030's structural contract.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "void"-related error: "request for member 'error' in something
//   not a class structure or union", "void value not ignored as it
//   ought to be", "fixy-A2-030".

#include <crucible/bridges/CrashTransport.h>
#include <crucible/permissions/PermissionInherit.h>

#include <expected>
#include <utility>

namespace proto = crucible::safety::proto;

namespace {

// Same minimal valid setup as fixture #1 — survivor list matches so
// only the post-fix void-return discipline can be the source of the
// compile error.
struct DeadPeer {};
struct SurvivorA {};
struct Channel {};

}  // namespace

namespace crucible::permissions {

template <>
struct survivor_registry<DeadPeer> {
    using type = inheritance_list<SurvivorA>;
};

}  // namespace crucible::permissions

namespace {

// Force-instantiate a function that exercises chained member access
// on the helper's return.  Wrapping in a function (not just main)
// keeps the failure inside a typed expression rather than the
// `main` body's eager fold-evaluation.
[[maybe_unused]] auto chained_access_must_fail() {
    using GoodExpected =
        std::expected<int, proto::CrashEvent<DeadPeer, Channel, SurvivorA>>;

    GoodExpected* good = nullptr;

    // fixy-A2-030 post-fix: on_crash returns void.  Chaining
    // `.error()` (or any other member) on the temporary return
    // MUST fail to compile — `void` is not a class type and has no
    // members.
    return proto::on_crash(*good, [](auto) {}).error();
}

}  // namespace

int main() {
    (void)chained_access_must_fail();
    return 0;
}
