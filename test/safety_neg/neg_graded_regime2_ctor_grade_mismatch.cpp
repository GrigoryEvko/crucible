// HS14 negative-compile fixture (fix-13, regime-2):
//
// The Graded<M, L, T> T==element_type specialization's two-arg ctor
// DISCARDS the witness grade after a guard.  The guard MUST fire at
// consteval — where a vanilla foldable `this`-free `pre()` clause would
// have been silently skipped by the documented GCC 16.1.1 bypass family
// (or degraded to nothing under contract_evaluation_semantic=ignore).
//
// This fixture constructs a regime-2 Graded with a NON-equivalent
// value/grade from a `consteval`/`static_assert` context over a real
// partial-order lattice (MonotoneLattice<int>, leq == std::less_equal).
// value=5, grade=10 are NOT lattice-equivalent (5 <= 10 holds but
// 10 <= 5 fails), so the in-body `contract_assert` makes the consteval
// call non-constant → compilation MUST fail.
//
// Expected: NON-zero exit with a contract/trap diagnostic.  A passing
// (zero-exit) compile is the bug this fixture guards against.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>
#include <crucible/algebra/lattices/MonotoneLattice.h>

namespace {

using ::crucible::algebra::Graded;
using ::crucible::algebra::ModalityKind;
using ::crucible::algebra::lattices::MonotoneLattice;

// Regime-2: T == element_type (int).  MonotoneLattice<int>'s leq is a
// true partial order, so 5 and 10 are NOT lattice-equivalent.
using GMonotoneInt =
    Graded<ModalityKind::Absolute, MonotoneLattice<int>, int>;

// consteval forces the guard to evaluate at compile time; the false
// equivalence makes the call non-constant.
consteval int forge_mismatched_grade() {
    GMonotoneInt forged{5, 10};  // value=5, grade=10 — NOT equivalent
    return forged.peek();
}

// The static_assert pulls forge_mismatched_grade() into a constant-
// expression context; the non-constant contract_assert poisons it.
static_assert(forge_mismatched_grade() == 5,
              "fixture must fail to compile: regime-2 two-arg ctor "
              "grade-mismatch guard did not fire at consteval");

}  // namespace
