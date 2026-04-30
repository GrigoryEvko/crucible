// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an `OpaqueLifetime<PER_REQUEST, T>` value to
// `Cipher::commit_per_program`, whose `requires` clause demands
// `OpaqueLifetime::satisfies<PER_PROGRAM>`.  Companion to the main
// FOUND-G12 fixture (PER_REQUEST → commit_per_fleet) — covers the
// SECONDARY leak fence in the lifetime → tier matrix.
//
// SECONDARY LEAK CLASS: a PER_REQUEST-scoped value (e.g., per-query
// KV-cache slice, per-decode PdaState, request-local grammar
// constraint) cannot reach commit_per_program — the Warm tier
// persists across program-restart but dies at process boundary.  A
// PER_REQUEST value flushed to Warm would be VALID FROM THE CIPHER'S
// PERSPECTIVE (NVMe-resident, durable across reboots) but
// SEMANTICALLY ROTTEN — it would reappear on the next program start
// and be replayed as if request-scoped, contaminating subsequent
// inferences.
//
// Lattice direction (LifetimeLattice.h):
//     PER_REQUEST(narrowest) ⊑ PER_PROGRAM ⊑ PER_FLEET(widest)
//
// satisfies<Required> = leq(Required, Self).  PER_REQUEST::
// satisfies<PER_PROGRAM> = leq(PER_PROGRAM, PER_REQUEST) = false.
// The requires-clause rejects.
//
// Concrete bug-class this catches: a refactor moves "checkpoint
// inferlet user state to a local recovery file" from request-end
// teardown (correct: drop / commit_per_request only) to a
// program-shutdown hook (incorrect: commit_per_program).  Today:
// silent corruption — the saved state survives reboot and is
// replayed as if newly minted.  With this fixture: the call rejects
// at compile time naming PER_REQUEST::satisfies<PER_PROGRAM> as the
// failed predicate.
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
    Cipher c;
    Arena arena;
    MetaLog log;
    auto* region = arena.alloc_obj<RegionNode>(
        crucible::effects::Test{}.alloc);
    region->content_hash = ContentHash{0xDEADBEEF};

    OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>
        request_scoped{region};

    // Should FAIL: commit_per_program requires satisfies<PER_PROGRAM>;
    // PER_REQUEST is strictly weaker → leq(PER_PROGRAM, PER_REQUEST)
    // is FALSE → the constraint fails.  Without this fence, request-
    // scoped data leaks across program restart via the Warm-tier
    // recovery path.
    auto pinned = c.commit_per_program(std::move(request_scoped), &log);
    return static_cast<int>(static_cast<bool>(std::move(pinned).consume()));
}
