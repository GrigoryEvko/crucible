// A binding at the affine Usage grade is used at most once, so it has no
// copy assignment.  A drop is permitted, and a copy is not.

#include <fixy/Atom.h>
#include <fixy/Fn.h>

int main() {
    auto const first = ::fixy::mint_fn<int, ::fixy::atom::affine>(1);
    auto second = ::fixy::mint_fn<int, ::fixy::atom::affine>(2);
    second = first;
    return second.value() - first.value();
}
