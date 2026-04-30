// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an `OpaqueLifetime<PER_REQUEST, T>` value to
// `Cipher::commit_per_fleet`, whose `requires` clause demands
// `OpaqueLifetime::satisfies<PER_FLEET>` — the load-bearing rejection
// for the inferlet cross-request data leak documented in
// OpaqueLifetime.h docblock + 25_04_2026.md §16 SessionOpaqueState.
//
// THE LOAD-BEARING REJECTION FOR FOUND-G12 (OpaqueLifetime production
// call site).  Cipher::commit_per_fleet routes its (already-lifetime-
// pinned) input to publish_cold (S3/GCS, Raft-replicated, fleet-
// durable).  The caller's source value MUST be PER_FLEET-pinned —
// no other lifetime scope is safe to durably commit across fleet
// boundaries.  A PER_REQUEST-pinned value (e.g., a PdaState for
// grammar-constrained decoding, an inferlet user state, a per-query
// KV-cache slice) becomes a CROSS-REQUEST DATA LEAK if ever flushed
// to PER_FLEET storage.
//
// Lattice direction (LifetimeLattice.h):
//     PER_REQUEST(narrowest) ⊑ PER_PROGRAM ⊑ PER_FLEET(widest)
//
// satisfies<Required> = leq(Required, Self).  For PER_REQUEST to
// satisfy PER_FLEET, we'd need leq(PER_FLEET, PER_REQUEST) — but
// PER_FLEET is STRICTLY WIDER, so leq(PER_FLEET, PER_REQUEST) is
// FALSE.  The requires-clause rejects the call.
//
// Concrete bug-class this catches: an inferlet author declares user
// state OpaqueLifetime<PER_REQUEST, PdaState>{state} — semantically
// scoped to a single grammar-constrained decode.  A refactor adds
// "checkpoint user state to durable storage" to the request-end
// teardown path and accidentally targets commit_per_fleet (instead of
// the correct DROP-or-commit_per_request).  Today: silent leak — the
// user's PdaState writes to S3 and is later replayed on a sibling
// request, contaminating its grammar context.  With this fixture in
// place: the call rejects at compile time with a structured
// requires-clause diagnostic naming PER_REQUEST::satisfies<PER_FLEET>
// as the failed predicate.  The bug never reaches main; the test
// reddens at the PR.
//
// Symmetric matrix-fill fixtures (FOUND-G11) cover the wrapper-
// surface rejections (assign / swap / equality / cross-tier mixing).
// THIS fixture exercises the CONSUMER-FENCE rejection at the
// production-call site of FOUND-G12 — without it, the most
// important real-world bug class (cross-request leak via tier
// promotion) is not exercised by the neg-compile harness, leaving
// an undetected hole.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-scope flow.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/safety/OpaqueLifetime.h>

#include <utility>

using crucible::Cipher;
using crucible::Arena;
using crucible::ContentHash;
using crucible::MetaLog;
using crucible::RegionNode;
using crucible::safety::OpaqueLifetime;
using crucible::safety::Lifetime_v;

int main() {
    Cipher c;     // Closed Cipher — commit_per_fleet's requires-clause
                  // rejects BEFORE we'd hit any runtime contract.
    Arena arena;
    MetaLog log;
    auto* region = arena.alloc_obj<RegionNode>();
    region->content_hash = ContentHash{0xCAFEBABE};

    // Pinned at PER_REQUEST — origin is a context where the value is
    // semantically scoped to a single inference request (e.g., an
    // inferlet PdaState, a per-query KV-cache slice, a request-local
    // grammar constraint).
    OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>
        request_scoped{region};

    // Should FAIL: commit_per_fleet requires
    //   is_opaque_lifetime_v<W>
    //     && W::template satisfies<Lifetime_v::PER_FLEET>
    // The first clause holds (W IS an OpaqueLifetime), but the second
    // is FALSE — PER_REQUEST::satisfies<PER_FLEET> evaluates to
    // leq(PER_FLEET, PER_REQUEST) = false.  The constraint fails;
    // the call is rejected.  Without this fence, a request-scoped
    // PdaState would silently flow into S3 cold storage as a fleet-
    // durable artifact — and reappear as input contamination on a
    // sibling request the next time anyone replays from cold.
    auto pinned = c.commit_per_fleet(std::move(request_scoped), &log);
    return static_cast<int>(static_cast<bool>(std::move(pinned).consume()));
}
