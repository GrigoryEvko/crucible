// Tier 4 refuses two atoms on Provenance.  Provenance is a Lattice axis in
// the table of fixy/Axis.h, so it carries one grade, and the pack does not
// say from which of the two sources the value comes.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::from_source<::fixy::tags::source::FromUser>,
                                ::fixy::atom::from_source<::fixy::tags::source::ForgePhase<'F'>>> refused{};
    return 0;
}
