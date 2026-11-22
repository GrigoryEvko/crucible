// Negative-compile test. This file must fail to compile.
//
// A lattice names itself with a name() that gives a TextView.  The lattice
// below declares a name() of another type.  lattice_name refuses it, so the
// name never falls back to the sentinel "<unnamed lattice>" with no error.

#include <foundation/algebra/Lattice.h>

// A text of a type that is not a TextView.
struct ForeignName {
    char first = 'F';
};

struct ForeignNamedLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return false; }
    [[nodiscard]] static constexpr element_type top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(element_type lhs, element_type rhs) noexcept { return !lhs || rhs; }
    [[nodiscard]] static constexpr element_type join(element_type lhs, element_type rhs) noexcept { return lhs || rhs; }
    [[nodiscard]] static constexpr element_type meet(element_type lhs, element_type rhs) noexcept { return lhs && rhs; }
    [[nodiscard]] static consteval ForeignName name() noexcept { return ForeignName{}; }
};

static_assert(::foundation::algebra::lattice_name<ForeignNamedLattice>().size() != 0);

int main() { return 0; }
