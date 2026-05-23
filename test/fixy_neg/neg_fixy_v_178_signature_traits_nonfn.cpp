// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-178 fixy::wrap::signature_traits guarantee fixture
// (class: NON-FUNCTION NTTP).
//
// signature_traits<auto FnPtr> reflects the pointed-to FUNCTION's
// signature.  Materializing `arity` forces
// `std::meta::parameters_of(^^remove_pointer_t<decltype(FnPtr)>)` — but
// here FnPtr is the int value 42, so remove_pointer_t<int> == int and
// `parameters_of(^^int)` is ill-formed (int is not a function type).
// The fixy::wrap:: re-export inherits the substrate's reflection
// requirement: introspecting a non-function through the umbrella reds.
//
// Distinct mismatch class from the GradedExtract fixture (concept-
// constrained alias over a non-Graded type) and the DecideOracle
// fixture (std::integral constraint on a double argument).
//
// Expected diagnostic: parameters_of / not a function / constant
// expression / reflection.

#include <crucible/fixy/Wrap.h>

namespace wrap = crucible::fixy::wrap;

// Forcing arity_v<42> materializes signature_traits<42>::params, which
// reflects ^^int and asks parameters_of(^^int) — ill-formed.
static_assert(wrap::arity_v<42> == 0,
              "unreachable — signature_traits<42> cannot reflect a non-function");

int main() { return 0; }
