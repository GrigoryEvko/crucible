// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-085 HS14 fixture #2 of 2 for fixy/sync/SyncPrim.h:
// HotPathSyncPrimSafe<Hot, permit_mutex> is the LOAD-BEARING reject
// gate — admitting any banned sync-primitive permit on a Hot-tier
// callable MUST be a compile error.
//
// Why this matters: V-085 encodes CLAUDE.md §IX.5's latency-hierarchy
// ban surface (futex/condvar/mutex/atomic::wait/sleep_for forbidden
// on hot path — 1-5 µs latency exceeds the ~40 ns intra-socket
// budget).  A regression that drops the Hot-tier conjunction in
// HotPathSyncPrimSafe — or admits `pack_contains_banned_sync_prim_v`
// past a Hot-tier annotation — would let a futex-blocking syscall
// flow through a TraceRing::try_push-style hot-path entry without
// the type system rejecting.  This fixture pins the load-bearing
// gate so the discipline cannot drift silently.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// LOAD-BEARING REJECT half — the HotPathSyncPrimSafe<Hot, permit_*>
// concept MUST be unsatisfied for ANY banned permit.  Sibling
// fixture `neg_fixy_sync_sync_prim_cv_qualified_permit_rejected.cpp`
// exercises the CV-REF PROPAGATION half (cv/ref pierce in
// IsBannedSyncPrim).
//
// Expected diagnostic: "constraints not satisfied" / "associated
// constraints are not satisfied" / "HotPathSyncPrimSafe" /
// "permit_mutex".

#include <crucible/fixy/sync/SyncPrim.h>

namespace fixy_sync_prim = crucible::fixy::sync::sync_prim;
namespace safety_alias   = crucible::safety;

// Concept-constrained function template: a Hot-tier callable whose
// `Permits...` pack admits no banned sync primitive.  This models
// the production discipline: at a TraceRing::try_push-style hot
// entry, the function declares its `Permits...` pack (initially
// empty, or restricted to non-banned tags), and the concept on the
// signature rejects any caller passing a permit_*.
template <typename... Permits>
    requires fixy_sync_prim::HotPathSyncPrimSafe<
        safety_alias::HotPathTier_v::Hot, Permits...>
void hot_path_callable() {
    // Body intentionally empty — the concept is the gate.
}

int main() {
    // STRAINING CALL: instantiate the hot-path callable with
    // permit_mutex in the Permits pack.  HotPathSyncPrimSafe<Hot,
    // permit_mutex> evaluates to FALSE because pack_contains_banned
    // _sync_prim_v<permit_mutex> is true AND Tier == Hot.  The
    // requires-clause fails; the call site is a hard compile error.
    //
    // If this fixture COMPILES, the type system has lost the
    // load-bearing discipline that prevents futex-blocking
    // primitives from entering the hot path.
    hot_path_callable<fixy_sync_prim::permit_mutex>();
    return 0;
}
