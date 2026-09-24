// A final type that derives atom_base but names no axis is half the
// recipe.  IsAtom requires `G::axis`, so the required expression is
// invalid and the compiler says which one.

#include <fixy/Atom.h>

namespace {

struct axisless final : ::fixy::atom::atom_base {};

template <::fixy::atom::IsAtom G>
constexpr int engage() noexcept {
    return static_cast<int>(G::axis);
}

}  // namespace

int main() { return engage<axisless>(); }
