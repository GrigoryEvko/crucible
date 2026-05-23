// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-178 fixy::wrap::GradedExtract guarantee fixture
// (class: NON-GRADED-WRAPPER ARGUMENT).
//
// value_type_of_t<W> is constrained `requires IsGradedWrapper<W>`
// (safety/GradedExtract.h).  A bare `int` is NOT a GradedWrapper — it
// has none of the graded_type / lattice_type / value_type / modality
// surface — so the alias has no valid definition and the constraint
// is unsatisfied.  The fixy::wrap:: re-export preserves the substrate
// constraint: drilling into a non-wrapper through the umbrella reds.
//
// Distinct mismatch class from the signature_traits fixture (non-fn
// reflection) and the DecideOracle fixture (non-integral oracle arg).
//
// Expected diagnostic: constraints not satisfied / IsGradedWrapper /
// no type / template constraint failure.

#include <crucible/fixy/Wrap.h>

namespace wrap = crucible::fixy::wrap;

// int is not a GradedWrapper → value_type_of_t<int> constraint fails.
using Bad = wrap::value_type_of_t<int>;
static_assert(sizeof(Bad) > 0,
              "unreachable — value_type_of_t<int> has no definition");

int main() { return 0; }
