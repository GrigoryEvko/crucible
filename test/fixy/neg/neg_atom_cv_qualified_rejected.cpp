// An atom is a zero-state phantom marker, so a cv-qualified one only
// comes from a `decltype` taken on a runtime variable.  IsAtom rejects
// it rather than stripping the qualifier: the same-as clause fails, and
// the compiler names the clause.

#include <fixy/Atom.h>

namespace {

template <::fixy::atom::IsAtom G>
constexpr int engage() noexcept {
    return static_cast<int>(G::axis);
}

const auto held = ::fixy::atom::affine{};

}  // namespace

int main() { return engage<decltype(held)>(); }
