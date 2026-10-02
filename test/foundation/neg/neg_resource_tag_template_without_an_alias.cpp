// The check file of foundation/effects/Resources.h asks each class
// template in namespace resource for a top-level alias of its name in
// foundation::effects.  Each tag template of the catalog has one, so the
// check never answers no on the tree.
//
// This plants a class template in namespace resource and gives it no
// alias.  Its specializations carry no kind, so the walk that pairs each
// axis with one tag template does not read it.  The walk reads the
// members that the translation unit declares before the walk runs, so the
// fixture plants the template first and includes the check file after it.

#include <foundation/effects/Resources.h>

#include <cstdint>

namespace foundation::effects::resource {
template <std::uint64_t N>
struct UnaliasedBudget {};
}  // namespace foundation::effects::resource

#include "../../layer/checks/foundation/effects/Resources.cpp"

int main() { return 0; }
