// L003: a borrow x a detached spawn.
//
// A detached child is never joined, so it can run after the caller's
// frame has unwound, and a borrow into that frame dangles.  L002 reads a
// suspension or a Bg row and this pack names neither, so the pack trips
// L003 alone.

#include <fixy/Fn.h>
#include <fixy/os/Spawn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::borrow,
                                ::fixy::atom::spawn::detach_with<"logger drain outlives the owner">>
        refused{};
    return 0;
}
