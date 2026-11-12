// The check file of fixy/collision/Report.h asks that each declaration of
// the spawn family has a reading in protocol_reading_ of fixy/Collision.h.
// The walk units compile that check file after each header that can add to
// the namespace (test/layer/walk-checks.txt), so the check sees each spawn
// atom of the tree.  The check passes on the tree, so no test shows it
// answering no.
//
// This plants a class template in the spawn family before the check file,
// because the walk lists the members of the namespace where the check
// instantiates it.  The relation has no reading for the template, and the
// check must name it.

#include <fixy/Collision.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a spawn template of the base with no reading")
namespace fixy::atom::spawn {
template <::fixy::atom::ctrl::rationale Rationale>
struct probe_unread_spawn;
}  // namespace fixy::atom::spawn
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

#include "../../layer/checks/fixy/collision/Report.cpp"

int main() { return 0; }
