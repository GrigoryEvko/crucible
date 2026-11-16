// The compile-time checks of foundation/core/Product.h.

#include <foundation/core/Product.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::product_checks {

// A value whose add is neither associative nor commutative, so a change of
// the shape of the tree or of the order of two operands changes the code.
struct Code {
    std::uint64_t value = 0;

    [[nodiscard]] friend constexpr Code operator+(Code left, Code right) noexcept {
        return Code{rotl(left.value, 7) * 0x9E3779B97F4A7C15u + (right.value ^ 0xD1B54A32D192ED03u)};
    }
};

// Leaf i has the code i + 1.
struct CodeLeaves {
    [[nodiscard]] constexpr Code operator()(std::size_t leaf) const noexcept { return Code{leaf + 1}; }
};

// The tree of tree_of_leaves_ is the tree of reduce_by_tree for each count
// from 1 to 64, and from a first leaf other than zero.
[[nodiscard]] consteval bool has_the_tree_of_reduce_by_tree() {
    FixedArray<Code, 64> leaves{};
    std::uint64_t next = 1;
    for (Code& leaf : leaves) {
        leaf = Code{next++};
    }
    auto const add = [](Code const& left, Code const& right) noexcept { return left + right; };
    for (std::size_t count = 1; count <= 64; ++count) {
        View<Code const> const part = View<Code const>{leaves.view()}.window(0, count).expect("the part fits");
        Code const expected = reduce_by_tree(part, add).expect("the part holds a leaf");
        if (tree_of_leaves_<Code>(CodeLeaves{}, 0, count).value != expected.value) return false;
    }
    View<Code const> const shifted = View<Code const>{leaves.view()}.window(5, 37).expect("the part fits");
    return tree_of_leaves_<Code>(CodeLeaves{}, 5, 37).value == reduce_by_tree(shifted, add).expect("leaves").value;
}
static_assert(has_the_tree_of_reduce_by_tree());

// The roles of the two factors are two types of Side, of the layout of the
// view.
static_assert(!std::is_same_v<LeftFactorOf<float>, RightFactorOf<float>>);
static_assert(std::is_same_v<decltype(side_as<side::LeftFactor>(std::declval<StridedView<float const, 2>>())),
                             LeftFactorOf<float>>);
static_assert(sizeof(LeftFactorOf<double>) == sizeof(StridedView<double const, 2>));
static_assert(std::is_trivially_copyable_v<RightFactorOf<float>>
              == std::is_trivially_copyable_v<StridedView<float const, 2>>);

// The fixture neg_core_side_swap holds the refusal of a call with the two
// factors in the other order, and neg_core_side_from_payload the refusal of a
// plain view in place of a role.

}  // namespace detail::product_checks

}  // namespace foundation::core
