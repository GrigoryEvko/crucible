// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-033 — HS14 fixture.  Asserts that instantiating
// `CrashEvent<PeerTag, Resource, SurvivorTags...>` with a
// `Resource` whose move ctor is NOT `noexcept` is rejected at
// compile time.
//
// Why this matters (silent-terminate-mid-recovery class):
//   `CrashEvent`'s ctor is `noexcept`.  If `Resource`'s move
//   could throw, the field move-init would call `std::terminate`
//   exactly during crash recovery — the moment when the design
//   wants graceful permission inheritance and bounded latency.
//   Pre-fix this was a latent footgun: a refactor that added a
//   non-noexcept member to a Resource type would silently
//   reintroduce mid-recovery termination.
//
// Post-fix `CrashEvent` has a `static_assert(
// std::is_nothrow_move_constructible_v<Resource>)` in its class
// body.  Instantiating it with a throwing-move Resource fires
// the assert at compile time — the entire class of bug becomes
// structurally unreachable.
//
// Companion to fixy-A2-033's by-value → rvalue-ref ctor
// signature change: that change saves one move per crash event
// (~3µs for a 16KB Resource); this assert prevents the
// optimization from regressing into a terminate-mid-recovery
// hazard if the discipline drifts.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "fixy-A2-033" | "nothrow-move-constructible" |
//   "static assertion failed" | "noexcept"

#include <crucible/bridges/CrashTransport.h>

namespace proto = crucible::safety::proto;

namespace {

struct DeadPeer {};
struct SurvivorA {};

// Resource type whose move ctor is INTENTIONALLY not noexcept.
// CrashEvent's class-body static_assert must fire on this.
struct ThrowingMoveResource {
    ThrowingMoveResource() = default;
    ThrowingMoveResource(const ThrowingMoveResource&) = default;
    // The load-bearing line: noexcept(false) move ctor.
    ThrowingMoveResource(ThrowingMoveResource&&) noexcept(false) {}
};

}  // namespace

namespace crucible::permissions {

template <>
struct survivor_registry<DeadPeer> {
    using type = inheritance_list<SurvivorA>;
};

}  // namespace crucible::permissions

namespace {

// Force CrashEvent instantiation by referring to its size.  The
// class body's `static_assert(std::is_nothrow_move_constructible_v
// <Resource>)` fires at this point — Resource = ThrowingMoveResource
// is move-constructible but NOT nothrow-move-constructible.
using BadEvent = proto::CrashEvent<DeadPeer, ThrowingMoveResource, SurvivorA>;
[[maybe_unused]] constexpr auto bad_size = sizeof(BadEvent);

}  // namespace

int main() {
    return 0;
}
