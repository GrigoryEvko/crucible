// The left choice gives the first summand that is not zero.  zero is its unit
// and it is associative, so the gate at zero admits it.  But 1 + 2 is 1 and
// 2 + 1 is 2, so the claim of the laws at its witnesses refuses it.  The
// claim is the constraint of a variable, so the refusal names the claim.

#include <foundation/algebra/Lattice.h>

namespace {

struct LeftChoice {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 0; }
    [[nodiscard]] static constexpr bool is_sum_defined(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept { return a != 0 ? a : b; }
};

constexpr LeftChoice::element_type kNone = 0;
constexpr LeftChoice::element_type kFirst = 1;
constexpr LeftChoice::element_type kSecond = 2;

template <class Monoid>
    requires(::foundation::algebra::verify_partial_commutative_monoid_over<Monoid>(kNone, kFirst, kSecond))
constexpr bool is_claimed = true;

}  // namespace

static_assert(is_claimed<LeftChoice>);

int main() { return 0; }
