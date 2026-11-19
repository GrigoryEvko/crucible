// Tier 4 refuses two atoms on Trust.  Trust is a Lattice axis in the table
// of fixy/Axis.h, so it carries one grade, and the pack does not say which
// of the two levels verified the binding.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::trust<::fixy::tags::trust::Verified>,
                                ::fixy::atom::trust<::fixy::tags::trust::Tested>> refused{};
    return 0;
}
