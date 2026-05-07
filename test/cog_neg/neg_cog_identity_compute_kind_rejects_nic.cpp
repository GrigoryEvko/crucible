// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for cog::IsComputeKind (GAPS-185 #1210).
//
// Premise: IsComputeKind<K> partitions the 21-atom CogKind universe
// into compute Cogs (Gpu / CpuCore / GpuPackage / CpuSocket — the
// substrates that own a per-Cog Mimic instance per GAPS-188) and
// the rest (NIC ports, switches, transceivers, PSU rails, ...).
// Downstream factories that bind compiled kernels to a Cog must
// constrain on this concept; passing a non-compute CogKind would
// produce a Mimic stub with no compile path and trigger a
// "Mimic-instance-without-compute-Cog" runtime error in the
// kernel cache.
//
// Why this is the load-bearing soundness gate:
//
// A future per-Cog Mimic factory (mint_cog_mimic<CogKind K>(...) per
// GAPS-188) constrains on IsComputeKind so the type system rejects
// the "GpuMimic for NicPort" misuse at template-substitution time.
// Without this gate, a typo or refactor that swaps `CogKind::Gpu`
// for `CogKind::NicPort` in a call site silently produces a Mimic
// stub bound to a non-compute Cog, deferring the failure to runtime
// where it surfaces as a confusing "no kernel for this kind" abort
// far from the source of the bug.
//
// Companion fixture: neg_cog_identity_content_hash_zero_uuid.cpp
//   * That one tests rejection at the DATA-INVARIANT gate — the pre
//     clause on content_hash refuses zero-uuid CogIdentity.  Distinct
//     mismatch class (precondition contract violation).
//   * This one tests rejection at the CONCEPT gate — the IsComputeKind
//     requires-clause refuses non-compute CogKind atoms at
//     template substitution time.  Distinct mismatch class (concept
//     substitution failure on a constrained template parameter).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" / "constraints not
// satisfied" / "IsComputeKind" / "kernel_count_for" / "CogKind::NicPort"
// / "GAPS-185" pointing at the static_assert call site below.

#include <crucible/cog/CogIdentity.h>

namespace cog = crucible::cog;

// Mock of the future GAPS-188 mint_cog_mimic factory shape: a
// function templated on a CogKind value, constrained on
// IsComputeKind.  Calling it with a non-compute CogKind fails the
// concept gate at substitution time.
template <cog::CogKind K>
    requires cog::IsComputeKind<K>
constexpr int kernel_count_for() noexcept { return 1; }

// CogKind::NicPort is a network-fabric atom — not a compute atom.
// IsComputeKind<NicPort> is false, so the requires-clause refuses
// the substitution and the build fails here at the call site.
static_assert(kernel_count_for<cog::CogKind::NicPort>() == 1,
    "GAPS-185: cog::IsComputeKind concept MUST refuse non-compute "
    "CogKind values.  If this static_assert ever evaluates, a future "
    "mint_cog_mimic factory would accept NicPort as a compute "
    "target and produce a Mimic stub with no kernel-compile path — "
    "kernel-binding partition defense compromised.");

int main() { return 0; }
