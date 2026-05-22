// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-216 HS14 fixture #1 of 2 for fixy::handle::EpochVersioned:
// axis-swap rejection — passing a Generation where an Epoch is
// expected at the EpochVersioned<T> constructor is a type error.
//
// Violation: the two strong-typed newtypes Epoch and Generation are
// each `struct { uint64_t value; }` with NO implicit ctor from the
// other axis.  The substrate EpochVersioned<T>::EpochVersioned(T,
// Epoch, Generation) constructor demands the parameters in axis-
// canonical order; swapping them is a substrate-level compile error
// preserved exactly through the fixy::handle:: alias.  If the alias
// ever drifted (e.g. via a shadowed `Epoch` typedef that took a
// `Generation` argument silently), this fixture would let the swap
// compile — at which point a Canopy admission gate could read a
// per-Relay restart counter as a fleet epoch and silently re-admit
// pre-reshard checkpoints (CRUCIBLE.md §L13/§L14 disaster).
//
// Distinct from fixture #2 (bridge cross-T combine):
//   * Fixture #1 — Strong-typed AXIS distinction (Epoch ≠ Generation)
//     at the constructor parameter type.  Same T (int), swapped axes.
//   * Fixture #2 — Cross-T composition rejection on combine_max.
//     Same axis layout, different T (int vs double).
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Expected diagnostic: cannot convert / no matching function for call
// to EpochVersioned<int>::EpochVersioned with (int, Generation, Epoch)
// — the ctor signature is (T, Epoch, Generation).

#include <crucible/fixy/Handle.h>

int main() {
    namespace fh = ::crucible::fixy::handle;

    // Canonical (correct) construction would be:
    //     fh::EpochVersioned<int>{42, fh::Epoch{5}, fh::Generation{2}};
    // Below: the (Epoch, Generation) pair is SWAPPED — Generation
    // arrives where Epoch is expected, and Epoch where Generation is
    // expected.  Each strong-typed newtype rejects construction from
    // the other; the compiler reports "no matching constructor".
    fh::EpochVersioned<int> bad{
        42,
        fh::Generation{1},  // expected fh::Epoch{...}
        fh::Epoch{2}        // expected fh::Generation{...}
    };
    (void)bad;
    return 0;
}
