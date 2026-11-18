// The positive counts with a sum below 16 have no zero, so they have no unit
// of add, and they are no partial commutative monoid.  A claim of the laws
// does not compile.

#include <foundation/algebra/Lattice.h>

namespace {

struct PositiveCount {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr bool is_sum_defined(element_type a, element_type b) noexcept { return a + b <= 15; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        return static_cast<element_type>(a + b);
    }
};

constexpr PositiveCount::element_type kOne = 1;
constexpr PositiveCount::element_type kTwo = 2;

}  // namespace

static_assert(::foundation::algebra::verify_partial_commutative_monoid_over<PositiveCount>(kOne, kTwo));

int main() { return 0; }
