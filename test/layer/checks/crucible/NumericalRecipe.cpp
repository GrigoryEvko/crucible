// The compile-time checks of crucible/NumericalRecipe.h.

#include <crucible/NumericalRecipe.h>

namespace crucible {

static_assert(sizeof(NumericalRecipe) == 16, "NumericalRecipe must stay 16 bytes: both the intern table and the "
                                             "kernel nodes that point at it depend on the layout");

}  // namespace crucible
