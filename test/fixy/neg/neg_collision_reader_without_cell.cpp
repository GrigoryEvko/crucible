// The check file of fixy/Collision.h asks that each reader of
// fixy::collision and fixy::collision::detail has a cell that holds.  The
// check passes on the tree, so no test shows it answering no.
//
// This plants a concept in fixy::collision::detail before the check file,
// because the walk lists the members of the namespace where the check runs.
// The concept has no cell and no row of the helpers, and the check must name
// it.

#include <fixy/Collision.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a reader of the base with no cell")
namespace fixy::collision::detail {
template <class G>
concept HasPlantedReading = requires { G::planted; };
}  // namespace fixy::collision::detail
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

#include "../../layer/checks/fixy/Collision.cpp"

int main() { return 0; }
