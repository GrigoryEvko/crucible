// The check file of fixy/Collision.h asks roster_join_diagnostic() if each
// `*_atom_roster` alias of fixy::atom::detail is joined into
// fixy::collision::all_atom_roster.  The walk units compile that check file
// after each header that can add to the namespace
// (test/layer/walk-checks.txt), so the check sees each roster of the tree.
// The check passes on the tree, so no test shows it answering no.
//
// This plants a roster with a member outside the population, and a roster
// that only gives a joined family a second name.  The diagnostic must name
// the first roster and only the first: the check reads the relation, and
// not each new declaration.
//
// The rosters come before the check file, because the walk lists the
// members of the namespace where the check instantiates it.

#include <fixy/Atom.h>
#include <fixy/atoms/Sync.h>

#include <tuple>

namespace fixy::atom::detail {
using probe_orphan_atom_roster = std::tuple<int>;
using probe_joined_atom_roster = sync_atom_roster;
}  // namespace fixy::atom::detail

#include "../../layer/checks/fixy/Collision.cpp"

int main() { return 0; }
