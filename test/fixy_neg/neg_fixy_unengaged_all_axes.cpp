// NEGATIVE-COMPILE TEST (MULTI-CELL).  This file MUST FAIL TO COMPILE.
//
// fix-19 pilot consolidation.  This single translation unit replaces 22
// one-file-one-assertion fixtures (neg_fixy_unengaged_<axis>.cpp) with
// one TU carrying 22 independent cells.
//
// WHY THIS CONSOLIDATION IS SOUND (and a blind merge of arbitrary
// neg-compile fixtures is NOT):
//
//   A negative-compile TU verifies a soundness gate by FAILING to
//   compile.  A *fatal* compile error stops the compiler at the first
//   failure, so N independent fatal-error cells cannot be verified
//   independently from one compile — the first error masks the rest.
//   The general one-file-one-assertion harness exists precisely for
//   that reason.
//
//   The `FixyNotEngaged_<Axis>` family is the special case where merging
//   IS sound: each cell fails via a `static_assert` (a NON-FATAL
//   diagnostic — GCC error-recovers and keeps compiling) whose message
//   carries a UNIQUE grep token `FixyNotEngaged_<Axis>:`.  Empirically
//   (g++-16p -fsyntax-only) all 22 cells emit their distinct named
//   diagnostics in one TU with no cross-contamination.  The driver is
//   invoked with all 22 regexes and requires EVERY one to appear, so a
//   cell that stops failing — or whose diagnostic drifts — reddens the
//   merged test naming the exact silent axis.  Per-cell independence is
//   preserved; only the file/registration count drops 22 -> 1.
//
//   See test/CMakeLists.txt `crucible_neg_compile_fixy_multi_test` and
//   the binary-per-TU soundness note above that function.

#include "_fixy_neg_pack.h"

// One cell per DimensionAxis that has a registered unengaged-axis
// witness.  Order mirrors the historical per-file registration order in
// test/CMakeLists.txt.  Each FIXY_NEG_FIXTURE(Axis) expands to a fresh
// <Axis>Probe / <Axis>Tag typedef family plus a `static_assert(... &&
// false, "FixyNotEngaged_<Axis>: ...")` — distinct symbols per cell, no
// collision across cells.
FIXY_NEG_FIXTURE(Usage);
FIXY_NEG_FIXTURE(Effect);
FIXY_NEG_FIXTURE(Security);
FIXY_NEG_FIXTURE(Protocol);
FIXY_NEG_FIXTURE(Lifetime);
FIXY_NEG_FIXTURE(Provenance);
FIXY_NEG_FIXTURE(Trust);
FIXY_NEG_FIXTURE(Representation);
FIXY_NEG_FIXTURE(Complexity);
FIXY_NEG_FIXTURE(Precision);
FIXY_NEG_FIXTURE(Mutation);
FIXY_NEG_FIXTURE(Staleness);
FIXY_NEG_FIXTURE(Type);
FIXY_NEG_FIXTURE(Refinement);
FIXY_NEG_FIXTURE(Observability);
FIXY_NEG_FIXTURE(Space);
FIXY_NEG_FIXTURE(Overflow);
FIXY_NEG_FIXTURE(Reentrancy);
FIXY_NEG_FIXTURE(Size);
FIXY_NEG_FIXTURE(Version);
FIXY_NEG_FIXTURE(Synchronization);
FIXY_NEG_FIXTURE(Regime);

int main() { return 0; }
