// The check file of fixy/Collision.h asks sample_set_diagnostic() if a
// `*_atom_samples` alias of fixy::atom::detail is joined into
// fixy::collision::all_atom_roster.  A sample set stays outside the
// population, and this half of the relation keeps a rename from moving a
// family out of the roster check.  The walk units compile that check file
// after each header that can add to the namespace
// (test/layer/walk-checks.txt).  The check passes on the tree, so no test
// shows it answering no.
//
// This plants a sample set that names a joined family, and a sample set
// outside the population.  The diagnostic must name the first set and only
// the first.
//
// The sets come before the check file, because the walk lists the members
// of the namespace where the check instantiates it.

#include <fixy/Atom.h>
#include <fixy/atoms/Sync.h>

#include <tuple>

namespace fixy::atom::detail {
using probe_wrong_atom_samples = sync_atom_roster;
using probe_ok_atom_samples = std::tuple<int>;
}  // namespace fixy::atom::detail

#include "../../layer/checks/fixy/Collision.cpp"

int main() { return 0; }
