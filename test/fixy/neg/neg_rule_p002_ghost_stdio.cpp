// P002: ghost x an emitting surface.  A ghost binding is erased at
// codegen, and a stdio write is emitted code by definition.  P010
// catches the same contradiction through the effect row; the Stdio and
// SyscallSurface axes are the other two doors to it, and a pack that
// takes one of those reaches no effect row at all.
//
// The pack trips P002 alone.  P010 reads the Effect grade, which stays
// at its strict empty-row pole here, and the corpus entry
// ghost_runtime_observable reads that same row, so neither fires.  That
// is what makes this fixture the witness that P002 is not redundant:
// remove it and this binding is admitted.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::ghost,
                                ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>>
        refused{};
    return 0;
}
