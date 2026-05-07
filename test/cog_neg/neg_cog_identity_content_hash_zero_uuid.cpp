// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for cog::content_hash (GAPS-185 #1210).
//
// Premise: content_hash takes a CogIdentity and produces a uint64_t
// KernelCache key axis from the (uuid, firmware_revision, bios_revision)
// triple.  A default-constructed CogIdentity has Uuid{0, 0} —
// `uuid.is_zero() == true` — which is the "uninitialized" sentinel.
// Hashing a zero-uuid identity would silently produce a content_hash
// dominated by firmware/bios bits and would COLLIDE across every
// uninitialized Cog in the topology, aliasing them into a single
// KernelCache slot.
//
// Why this is the load-bearing soundness gate:
//
// The whole point of content-addressing CogIdentity is to keep
// per-Cog kernel cache slots distinct so a firmware update on one
// die invalidates ONLY that die's compiled kernels — not the
// neighbor's.  If zero-uuid identities are accepted, a freshly
// allocated Cog wrapper that hasn't yet had its uuid populated
// (e.g., a probe in flight from GAPS-111 topology/Discovery.h)
// would silently inherit the kernel cache slot of every other
// pre-init Cog, returning stale or wrong-shape compiled code.
//
// Companion fixture: neg_cog_identity_compute_kind_rejects_nic.cpp
//   * That one tests rejection at the CONCEPT gate — IsComputeKind<K>
//     refuses non-compute CogKind atoms (NicPort, NvSwitch, ...) at
//     concept-substitution time.  Distinct mismatch class.
//   * This one tests rejection at the DATA-INVARIANT gate — the pre
//     clause on content_hash refuses zero-uuid CogIdentity values.
//     Distinct mismatch class (pre-condition contract violation, not
//     concept substitution failure).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "static assertion failed" / "contract" /
// "precondition" / "is_zero" / "non-constant" / "content_hash" /
// "GAPS-185" pointing at the static_assert call site below.

#include <crucible/cog/CogIdentity.h>

namespace cog = crucible::cog;

// Default-constructed CogIdentity — every field at its NSDMI default.
// The Uuid default constructor produces `Uuid{0, 0}`; .is_zero() is
// true.  The pre clause on content_hash refuses this input.
constexpr cog::CogIdentity ZERO_UUID_FIXTURE{};

// Calling content_hash on the default-constructed identity violates
// the pre clause `!c.uuid.is_zero()`.  In manifestly-constant-evaluated
// context (the static_assert below), the contract violation makes the
// call not a constant expression, so the static_assert is ill-formed
// — the build fails here.
static_assert(cog::content_hash(ZERO_UUID_FIXTURE) == 0,
    "GAPS-185: cog::content_hash MUST refuse zero-uuid CogIdentity at "
    "the precondition contract.  If this static_assert ever evaluates "
    "successfully, the KernelCache key axis is silently aliasing "
    "every uninitialized Cog into the same slot — invalid compiled "
    "kernels would replay across distinct hardware.");

int main() { return 0; }
