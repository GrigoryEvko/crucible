// A chain that states no claim orientation cannot say which way weaken()
// moves a claim.  Graded refuses such a lattice as a stored grade: an
// unknown reading is refused, never admitted.  The same chain grades a
// value that is its own grade, which the self-test in Graded.h shows.

#include <foundation/algebra/Graded.h>

namespace {

struct UnstatedChain {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 3; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
};

}  // namespace

int main() {
    namespace fa = ::foundation::algebra;
    using Authority = fa::detail::graded_self_test::self_test_authority;
    fa::Graded<fa::ModalityKind::Absolute, UnstatedChain, int> const graded{Authority::key(), 1,
                                                                           UnstatedChain::bottom()};
    return graded.peek();
}
