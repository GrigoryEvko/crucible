// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V401 (FIXY-V-268, widened FIXY-FOUND-062),
// Cluster variant:
//
//     marks_hot_path NOT REQUIRED — V401 fires on the SCOPE × BARRIER
//     composition regardless of hot-path marking.
//   scope_at_or_above_v<Cluster, F::type_t> == true  (Cluster / Gpu / System)
//   ∧ barrier_at_or_above_v<AcqRel, F::type_t> == false (barrier ⊏ AcqRel)
//   ⇒ ill-formed
//
// This fixture probes the Cluster tier specifically — the case
// FIXY-FOUND-062 added to V401's catch set.  Before FIXY-FOUND-062 the
// predicate threshold was Gpu, so {Gpu, System} were caught but Cluster
// silently passed.  Hopper's PTX `.cluster` scope realizes a thread-block
// cluster (up to 8 CTAs sharing distributed shared memory and synchronizing
// via `cluster.sync`).  A `.cluster`-scope publication under-fenced (None /
// CompilerBarrier / AcquireLoad / ReleaseStore) widens visibility ACROSS
// CTAs but never establishes the two-sided ordering cross-CTA readers
// require — the exact same silent weak-memory race profile as a `.gpu`
// publish under-fenced.
//
// HS14 #2 of 2 for V401 (FIXY-FOUND-062 widening).  Pairs with:
//   1. Wait<Gpu, T>     — original device-wide tier
//      (neg_collision_V401_scope_strength.cpp)
//   2. Wait<Cluster, T> — Hopper cross-CTA tier (this).
// Both probe distinct lattice positions in the rejected region; a
// regression narrowing `scope_at_or_above_v<Cluster, S>` back to
// `scope_at_or_above_v<Gpu, S>` would silently let Cluster publishes
// slip through — this fixture catches that.
//
// Concrete bug-class this catches: a future refactor narrowing V401's
// scope threshold back to Gpu would restore the pre-FIXY-FOUND-062
// under-rejection for the Hopper thread-block-cluster case.  This
// fixture pins the rejection at the source-code declaration boundary
// where the scope/strength contract belongs.
//
// Expected diagnostic substring: "V401:".

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/ScopedFence.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using MS = crucible::algebra::lattices::MemoryScope;
using BS = crucible::algebra::lattices::BarrierStrength;

namespace neg_collision_v401_cluster {
// A Cluster-scope publication guarded only by a None barrier — V401 fires
// after the FIXY-FOUND-062 widening (was silently legal pre-fix).
using Bad = fn::Fn<sf::BarrierGuarded<BS::None, sf::ScopedFence<MS::Cluster, int>>>;
}  // namespace neg_collision_v401_cluster

[[maybe_unused]] neg_collision_v401_cluster::Bad the_fixture{};

int main() { return 0; }
