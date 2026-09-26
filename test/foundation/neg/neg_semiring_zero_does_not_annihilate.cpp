// Here add and mul are both the maximum.  Each is associative and
// commutative, and zero is the identity of add, but zero does not
// annihilate under mul and one is no identity of mul, so the laws at
// zero() and one() refuse it.

#include <foundation/algebra/Lattice.h>

namespace {

struct MaxMax {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type one() noexcept { return 1; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept { return a < b ? b : a; }
};

}  // namespace

static_assert(::foundation::algebra::verify_semiring_axioms_at<MaxMax>(0, 1, 1));

int main() { return 0; }
