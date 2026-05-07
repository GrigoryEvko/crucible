// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for cog::IsComputeKind (GAPS-185 #1210).
//
// Premise: IsComputeKind<K> partitions the 21-atom CogKind universe
// into the strict subset where `cog_family_v<K> == CogFamily::Compute`
// (Gpu / CpuCore / GpuPackage / CpuSocket) and everything else.
// IsComputeKind is the NARROW gate for code that genuinely only runs
// on compute substrates — kernel-search workers (Mimic MAP-Elites),
// partition-optimiser compute-tile placement, etc.  It is NOT the
// gate for CogMimic<K> (GAPS-188) — CogMimic admits the broader
// IsMimicSubstrate (Compute ∪ Network ∪ Memory ∪ Bus families) per
// §3.7 of the networking design where every substrate Cog (NIC ports,
// switches, DRAM channels) gets its own Mimic instance with its own
// substrate-specific emit shape.
//
// Why this is the load-bearing soundness gate:
//
// Compute-only consumers (kernel-search workers, MAP-Elites archives)
// constrain on IsComputeKind so the type system rejects the "kernel
// search on a NicPort" misuse at template-substitution time.  Without
// this gate, a typo or refactor that swaps `CogKind::Gpu` for
// `CogKind::NicPort` in a kernel-only call site silently produces a
// no-op stub, deferring the failure to runtime where it surfaces as
// a confusing "no kernel for this kind" abort far from the source.
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

// Mock of a future compute-only consumer (e.g., MAP-Elites kernel-
// search worker; partition-optimiser compute-tile assignment).  The
// function is templated on a CogKind value, constrained on
// IsComputeKind.  Calling it with a non-compute CogKind fails the
// concept gate at substitution time.
template <cog::CogKind K>
    requires cog::IsComputeKind<K>
constexpr int kernel_count_for() noexcept { return 1; }

// CogKind::NicPort is a Network-family atom — IsComputeKind<NicPort>
// is false because cog_family_v<NicPort> == CogFamily::Network.  The
// requires-clause refuses the substitution and the build fails here
// at the call site.  (NicPort is still a Mimic substrate — it admits
// CogMimic<NicPort> via the broader IsMimicSubstrate gate — but is
// excluded from compute-only kernel-search dispatch.)
static_assert(kernel_count_for<cog::CogKind::NicPort>() == 1,
    "GAPS-185: cog::IsComputeKind concept MUST refuse non-Compute-"
    "family CogKind values.  If this static_assert ever evaluates, a "
    "compute-only consumer (MAP-Elites kernel search, partition-"
    "optimiser compute-tile placement) would accept NicPort and "
    "trigger a confusing 'no kernel for this kind' abort at runtime.");

int main() { return 0; }
