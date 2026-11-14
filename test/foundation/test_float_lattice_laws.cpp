// The laws of each lattice over a floating-point type hold bit for bit at
// the special values: the two infinities, the two zeros and the subnormals.
// Equality of value is not enough.  -0 and +0 are equal values with
// different bits, and a join that gives -0 for one order of its operands
// and +0 for the other gives two images of one grade.
//
// The lattices are MonotoneLattice over float and double, and the order
// dual of each.  The base names the special values of these two types.
// Each law runs at each pair or each triple of the witnesses, and each
// witness must be inside the order, from bottom up to top.  The check file
// of the header holds the same laws for the order std::greater at compile
// time.  A Graded value whose grade is an infinity must also build.

#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/core/Number.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fc = ::foundation::core;

// The value comes back through a call that no optimization looks into, so
// each operation below runs on a run-time argument.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

// True when the two values have the same bits.
template <typename T>
[[nodiscard]] bool same_bits(T left, T right) noexcept {
    if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
        return fc::bit_cast<std::uint32_t>(left) == fc::bit_cast<std::uint32_t>(right);
    } else {
        return fc::bit_cast<std::uint64_t>(left) == fc::bit_cast<std::uint64_t>(right);
    }
}

// The laws of L at one triple of witnesses.
template <typename L>
void check_triple(fa::LatticeElement<L> a, fa::LatticeElement<L> b, fa::LatticeElement<L> c) {
    a = opaque(a);
    b = opaque(b);
    c = opaque(c);
    CRUCIBLE_FATAL_INVARIANT(L::leq(L::bottom(), a) && L::leq(a, L::top()));
    CRUCIBLE_FATAL_INVARIANT(same_bits(L::join(L::bottom(), a), a) && same_bits(L::meet(L::top(), a), a));
    CRUCIBLE_FATAL_INVARIANT(same_bits(L::join(a, a), a) && same_bits(L::meet(a, a), a));
    CRUCIBLE_FATAL_INVARIANT(same_bits(L::join(a, b), L::join(b, a)));
    CRUCIBLE_FATAL_INVARIANT(same_bits(L::meet(a, b), L::meet(b, a)));
    CRUCIBLE_FATAL_INVARIANT(same_bits(L::join(a, L::meet(a, b)), a) && same_bits(L::meet(a, L::join(a, b)), a));
    CRUCIBLE_FATAL_INVARIANT(!(L::leq(a, b) && L::leq(b, a)) || same_bits(a, b));
    CRUCIBLE_FATAL_INVARIANT(L::leq(a, b) == same_bits(L::join(a, b), b));
    CRUCIBLE_FATAL_INVARIANT(L::leq(a, b) == same_bits(L::meet(a, b), a));
    CRUCIBLE_FATAL_INVARIANT(same_bits(L::join(L::join(a, b), c), L::join(a, L::join(b, c))));
    CRUCIBLE_FATAL_INVARIANT(same_bits(L::meet(L::meet(a, b), c), L::meet(a, L::meet(b, c))));
}

template <typename L, typename... Witnesses>
void check_pair(fa::LatticeElement<L> a, fa::LatticeElement<L> b, Witnesses... witnesses) {
    (check_triple<L>(a, b, witnesses), ...);
}

template <typename L, typename... Witnesses>
void check_first(fa::LatticeElement<L> a, Witnesses... witnesses) {
    (check_pair<L>(a, witnesses, witnesses...), ...);
}

// Each triple of the witnesses.  Complexity: cubic in the witnesses.
template <typename L, typename... Witnesses>
void check_laws(Witnesses... witnesses) {
    (check_first<L>(witnesses, witnesses...), ...);
}

// The special values of T, from the lowest up: the infinities, the limits,
// the least normals, the least subnormals and the two zeros.
template <typename T>
void check_laws_at_the_special_values(T infinity, T highest, T normal, T subnormal) {
    using Less = fl::MonotoneLattice<T>;
    using Dual = fl::DualLattice<Less>;
    check_laws<Less>(-infinity, -highest, T{-1}, -normal, -subnormal, -T{0}, T{0}, subnormal, normal, T{1}, highest,
                     infinity);
    check_laws<Dual>(-infinity, -highest, T{-1}, -normal, -subnormal, -T{0}, T{0}, subnormal, normal, T{1}, highest,
                     infinity);
}

// A value graded by itself builds at each infinity.
void graded_value_takes_an_infinity() {
    using Monotonic = fa::Graded<fa::ModalityKind::Absolute, fl::MonotoneLattice<double>, double>;
    const double infinity = opaque(fc::infinity_of<double>);
    const Monotonic high{infinity};
    const Monotonic low{-infinity};
    CRUCIBLE_FATAL_INVARIANT(same_bits(high.grade(), infinity) && same_bits(low.grade(), -infinity));
}

}  // namespace

int main() {
    check_laws_at_the_special_values<float>(fc::infinity_of<float>, fc::max_of<float>, fc::min_normal_of<float>,
                                            fc::min_subnormal_of<float>);
    check_laws_at_the_special_values<double>(fc::infinity_of<double>, fc::max_of<double>, fc::min_normal_of<double>,
                                             fc::min_subnormal_of<double>);
    graded_value_takes_an_infinity();
    return 0;
}
