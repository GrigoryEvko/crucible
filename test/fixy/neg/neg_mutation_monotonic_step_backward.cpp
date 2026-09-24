// Monotonic::advance carries CRUCIBLE_PRE(lattice_type::leq(current,
// new_value)).  A backward step in a constant expression reaches the
// macro's consteval trap, so the static_assert below has a non-constant
// condition, and the expansion note names Monotonic::advance.

#include <fixy/Mutation.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    fixy::Monotonic<std::uint32_t> counter = fixy::mint_monotonic<std::uint32_t>(10u);
    counter.advance(5u);
    return 0;
}

static_assert(under_test() == 0);

}  // namespace

int main() { return 0; }
