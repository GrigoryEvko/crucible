// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-030 — HS14 fixture #1.  Asserts that the `proto::on_crash`
// helper's return cannot be captured into a variable.
//
// Pre-fix `on_crash(Expected&& result, CrashHandler&& handler)`
// returned `Expected&&` after moving out `result.error()` into the
// handler — the returned Expected silently carried a moved-from
// error.  A caller writing `auto leftover = proto::on_crash(...);`
// then inspecting `leftover.error()` read undefined data:
//   * unique_ptr → nullptr (moved-from)
//   * trivially-moved struct → zero bytes
//   * partially-moved struct → undefined per-field
//
// The footgun was invisible at the type level: the same shape
// (`Expected&&`) carried both pre-handler-call (well-formed error)
// and post-handler-call (moved-from error) data with no structural
// distinction.  No `[[nodiscard]]`, no Tagged provenance, no
// state-machine ScopedView could close the hole — the underlying
// error_type lifetime was already gone.
//
// Post-fix `on_crash` returns `void`.  Capturing the return into a
// variable becomes a compile error because `auto` cannot deduce
// from void and `void` is not a complete type for storage.
//
// This fixture asserts the OLD shape: capturing the return value
// must FAIL to compile.  Sister fixture
// `neg_on_crash_inspect_moved_from_error.cpp` pins the symmetric
// failure mode (chaining member access on the return).
//
// Why this matters (silent-moved-from class):
//   on_crash sits on the crash-recovery hot path.  Every CNTP /
//   federation / Cipher-tier transport surface routes through it.
//   The pre-fix footgun was a class of bug indistinguishable from
//   "the recovery worked" until production logs surfaced silent
//   wrong recovery decisions days later.  Closing it structurally
//   (void return) makes the footgun a compile error at every call
//   site — past, present, and future.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "void"-related error: "void variable", "incomplete type",
//   "cannot deduce auto", "cannot declare variable of type void",
//   "fixy-A2-030".

#include <crucible/bridges/CrashTransport.h>
#include <crucible/permissions/PermissionInherit.h>

#include <expected>
#include <utility>

namespace proto = crucible::safety::proto;

namespace {

// Minimal valid setup: survivor list matches CrashEvent's pack so
// the in-helper survivor static_assert does NOT fire.  We want the
// failure to come ONLY from the post-fix void-return discipline,
// not from a survivor-list mismatch (which is covered separately
// by `neg_crash_event_missing_survivor.cpp`).
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

int main() {
    using GoodExpected =
        std::expected<int, proto::CrashEvent<DeadPeer, Channel, SurvivorA>>;

    GoodExpected* good = nullptr;

    // fixy-A2-030 post-fix: on_crash returns void.  Capturing the
    // return into a variable MUST fail at compile time — `auto`
    // cannot deduce from a void expression.
    auto leftover = proto::on_crash(*good, [](auto) {});
    static_cast<void>(leftover);

    return 0;
}
