// The compile-time checks of foundation/algebra/lattices/FlatLattice.h.

#include <foundation/algebra/lattices/FlatLattice.h>

#include <cstdint>

namespace foundation::algebra::lattices {

namespace detail::flat_lattice_self_test {

// Three siblings between one bottom and one top.  The numbers of the
// siblings sit on each side of the bounds, so an order that compares
// numbers fails the laws.
enum class Color : std::uint8_t {
    Red = 0,
    Unset = 1,
    Green = 2,
    Every = 3,
    Blue = 4,
};

using ColorLattice = FlatLattice<Color, Color::Unset, Color::Every>;

static_assert(Lattice<ColorLattice> && BoundedLattice<ColorLattice> && !UnboundedLattice<ColorLattice>);
static_assert(!Semiring<ColorLattice>);
static_assert(ColorLattice::claim_orientation == ClaimOrientation::stronger_is_higher);
static_assert(ColorLattice::bottom() == Color::Unset && ColorLattice::top() == Color::Every);

// The laws at each triple of the five values.  Complexity: cubic in the
// values, at compile time.
template <typename... Values>
[[nodiscard]] consteval bool laws_hold_for_pair(Color first, Color second, Values... values) {
    return (verify_bounded_lattice_axioms_at<ColorLattice>(first, second, values) && ...);
}

template <typename... Values>
[[nodiscard]] consteval bool laws_hold_for_first(Color first, Values... values) {
    return (laws_hold_for_pair(first, values, values...) && ...);
}

template <typename... Values>
[[nodiscard]] consteval bool laws_hold(Values... values) {
    return (laws_hold_for_first(values, values...) && ...);
}

static_assert(laws_hold(Color::Red, Color::Unset, Color::Green, Color::Every, Color::Blue));

// Two siblings are unordered.  Their join is the top and their meet is the
// bottom, whatever their numbers.
static_assert(!ColorLattice::leq(Color::Red, Color::Green) && !ColorLattice::leq(Color::Green, Color::Red));
static_assert(!ColorLattice::leq(Color::Blue, Color::Green));
static_assert(ColorLattice::join(Color::Red, Color::Blue) == Color::Every);
static_assert(ColorLattice::meet(Color::Red, Color::Blue) == Color::Unset);
static_assert(ColorLattice::join(Color::Unset, Color::Green) == Color::Green);
static_assert(ColorLattice::meet(Color::Every, Color::Green) == Color::Green);
static_assert(ColorLattice::leq(Color::Unset, Color::Red) && ColorLattice::leq(Color::Blue, Color::Every));
static_assert(!ColorLattice::leq(Color::Every, Color::Red) && !ColorLattice::leq(Color::Red, Color::Unset));

// Three siblings make M3, which is not distributive.
static_assert(!verify_distributive_lattice<ColorLattice>(Color::Red, Color::Green, Color::Blue));

// The template takes a scoped enum with two different bounds.
template <typename Element, Element Bottom, Element Top>
inline constexpr bool is_flat_lattice = requires { typename FlatLattice<Element, Bottom, Top>::element_type; };

enum PlainColor : std::uint8_t {
    PlainRed = 0,
    PlainBlue = 1,
};

static_assert(is_flat_lattice<Color, Color::Unset, Color::Every>);
static_assert(!is_flat_lattice<Color, Color::Every, Color::Every>);
static_assert(!is_flat_lattice<PlainColor, PlainRed, PlainBlue>);
static_assert(FlatLatticeElement<Color> && !FlatLatticeElement<PlainColor> && !FlatLatticeElement<int>);

}  // namespace detail::flat_lattice_self_test

}  // namespace foundation::algebra::lattices
