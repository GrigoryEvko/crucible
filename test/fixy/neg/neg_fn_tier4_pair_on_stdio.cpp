// Tier 4 refuses two atoms on Stdio.  A binding can write to two streams,
// so the code of the axis can state a set.  But Stdio is a Lattice axis in
// the table of fixy/Axis.h, and no design for a set of atoms exists, so
// tier 4 refuses the pair and fails closed.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stderr>,
                                ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>> refused{};
    return 0;
}
