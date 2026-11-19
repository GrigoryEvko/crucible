// Tier 4 refuses two atoms on SyscallSurface.  The code of the axis can
// state a set: the share flags of a mapping stack on one primary mode.  But
// SyscallSurface is a Lattice axis in the table of fixy/Axis.h, and no design
// for a set of atoms exists, so tier 4 refuses the pair and fails closed.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::mmap::with_prot<::fixy::mmap::prot::ReadOnly>,
                                ::fixy::atom::mmap::with_share<::fixy::mmap::share::Private>> refused{};
    return 0;
}
