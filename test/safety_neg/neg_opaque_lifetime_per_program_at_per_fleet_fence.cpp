// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an `OpaqueLifetime<PER_PROGRAM, T>` value to
// `Cipher::commit_per_fleet`, whose `requires` clause demands
// `OpaqueLifetime::satisfies<PER_FLEET>`.  Third matrix-fill fixture
// for FOUND-G12 — covers a DIFFERENT bug class than the PER_REQUEST
// fences: a PER_PROGRAM value durably-replicated across the fleet
// is not a per-request leak (no inferlet contamination), but a
// CROSS-PROGRAM-INSTANCE LEAK.
//
// CROSS-PROGRAM-INSTANCE LEAK CLASS: PER_PROGRAM data is
// program-instance-scoped (e.g., model-instance KV cache, per-Vigil
// scratch, in-process partial sums).  Replicating it across the
// fleet via Cold-tier S3/GCS makes it visible to OTHER program
// instances on OTHER Relays — including instances running DIFFERENT
// VERSIONS of the model (canary / blue-green deploys).  A canary
// model instance that picks up production-instance scratch state
// produces output that mixes two model versions.
//
// Lattice direction (LifetimeLattice.h):
//     PER_REQUEST(narrowest) ⊑ PER_PROGRAM ⊑ PER_FLEET(widest)
//
// satisfies<Required> = leq(Required, Self).  PER_PROGRAM::
// satisfies<PER_FLEET> = leq(PER_FLEET, PER_PROGRAM) = false.
//
// Concrete bug-class this catches: a "checkpoint everything to S3
// for total-cluster-recovery" sweep is added to the Keeper's reshard
// path; it iterates all live regions and calls commit_per_fleet
// indiscriminately.  Regions tagged PER_PROGRAM (model-instance
// scratch) silently flow into S3.  On reshard, ANY Relay (including
// canary / blue-green clones) reads them back as fleet-shared
// state.  Today: subtle output drift attributed to "model
// instability"; takes weeks to localize.  With this fixture: the
// call rejects at compile time, forcing the author to either drop
// the value or downgrade the commit target.
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
    region->content_hash = ContentHash{0xC0FFEE};

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>
        program_scoped{region};

    // Should FAIL: commit_per_fleet requires satisfies<PER_FLEET>;
    // PER_PROGRAM is strictly weaker → leq(PER_FLEET, PER_PROGRAM)
    // is FALSE → the constraint fails.  Captures the "indiscriminate
    // checkpoint to S3" regression class — program-instance state
    // silently shared across the fleet (canary contamination).
    auto pinned = c.commit_per_fleet(std::move(program_scoped), &log);
    return static_cast<int>(static_cast<bool>(std::move(pinned).consume()));
}
