// Each operation of each lattice keeps its result in the carrier of the
// lattice, or it refuses its operands.  The carrier is the order of the
// lattice, from bottom up to top, and the predicate is_well_formed of the
// element when the element has one.  A value outside the carrier makes each
// later leq compare against a grade that no element names.
//
// join and meet refuse no two values of the carrier, so a refusal of them
// ends the test.  add and mul run under the abort probe, because an
// operation can refuse its operands with a contract that ends the process.
// The tree has no door that starts a child process, so the probe catches
// the abort on this thread.  A refusal prints the report of the contract.
//
// The lattices are the lattices whose element is no scoped enum.  The check
// file of an enum lattice walks each enumerator, so the closure of its join
// and its meet is a compile-time fact.
//
// The second part gives the refusals of FractionalLattice: a sum above one,
// and a sum or a product whose reduced denominator is above the bound.

#include <foundation/Platform.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/NumaNodeLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/core/Number.h>

#include "abort_probe.h"

#include <concepts>
#include <cstdint>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
using ::crucible::test::aborts;

// The value comes back through a call that no optimization looks into, so
// each operation below runs on a run-time argument.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

// True when value is in the carrier of L.  The predicate of the element
// comes first, so leq never reads a value outside that predicate.
template <typename L>
[[nodiscard]] bool is_in_carrier(fa::LatticeElement<L> const& value) noexcept {
    if constexpr (requires {
                      { value.is_well_formed() } -> std::convertible_to<bool>;
                  }) {
        if (!value.is_well_formed()) return false;
    }
    bool is_inside = true;
    if constexpr (fa::HasBottom<L>) is_inside = is_inside && L::leq(L::bottom(), value);
    if constexpr (fa::HasTop<L>) is_inside = is_inside && L::leq(value, L::top());
    return is_inside;
}

// Each operation of L at one pair of witnesses.  A refused add or mul ends
// the probe, and each other result must be in the carrier.
template <typename L>
void check_pair(fa::LatticeElement<L> a, fa::LatticeElement<L> b) {
    using Element = fa::LatticeElement<L>;
    CRUCIBLE_FATAL_INVARIANT(is_in_carrier<L>(a) && is_in_carrier<L>(b));
    CRUCIBLE_FATAL_INVARIANT(is_in_carrier<L>(L::join(opaque(a), opaque(b))));
    CRUCIBLE_FATAL_INVARIANT(is_in_carrier<L>(L::meet(opaque(a), opaque(b))));
    if constexpr (fa::SemiringShape<L>) {
        Element sum{};
        const bool is_sum_refused = aborts([&sum, a, b] { sum = L::add(opaque(a), opaque(b)); });
        CRUCIBLE_FATAL_INVARIANT(is_sum_refused || is_in_carrier<L>(sum));
        Element product{};
        const bool is_product_refused = aborts([&product, a, b] { product = L::mul(opaque(a), opaque(b)); });
        CRUCIBLE_FATAL_INVARIANT(is_product_refused || is_in_carrier<L>(product));
    }
}

template <typename L, typename... Witnesses>
void check_row(fa::LatticeElement<L> a, Witnesses... witnesses) {
    (check_pair<L>(a, witnesses), ...);
}

// Each operation of L at each pair of its bottom, its top and the extra
// witnesses.  Complexity: quadratic in the witnesses.
template <typename L, typename... Extra>
void check_closure(Extra... extra) {
    const auto check_each_pair = [](auto... witnesses) { (check_row<L>(witnesses, witnesses...), ...); };
    if constexpr (fa::HasBottom<L> && fa::HasTop<L>) {
        check_each_pair(L::bottom(), L::top(), fa::LatticeElement<L>{extra}...);
    } else if constexpr (fa::HasBottom<L>) {
        check_each_pair(L::bottom(), fa::LatticeElement<L>{extra}...);
    } else {
        check_each_pair(fa::LatticeElement<L>{extra}...);
    }
}

struct ClosureEvent {};

void each_lattice_keeps_its_carrier() {
    using fl::Rational;
    constexpr std::int64_t bound = Rational::MAX_SAFE_MAGNITUDE;
    check_closure<fl::FractionalLattice>(Rational{1, 4}, Rational{1, 3}, Rational{1, 2}, Rational{2, 3}, Rational{3, 4},
                                         Rational{1, bound}, Rational{1, bound - 1}, Rational{bound - 1, bound});
    check_closure<fl::DualLattice<fl::FractionalLattice>>(Rational{1, 3}, Rational{1, bound});
    check_closure<fl::StalenessSemiring>(fl::staleness::at(1), fl::staleness::at(100),
                                         fl::staleness::at(::foundation::core::max_of<std::uint64_t> - 1));
    check_closure<fl::QttSemiring>(fl::QttGrade::One);
    check_closure<fl::MonotoneLattice<std::uint64_t>>(std::uint64_t{1}, std::uint64_t{1000});
    check_closure<fl::MonotoneLattice<std::int32_t>>(std::int32_t{-1}, std::int32_t{0}, std::int32_t{1});
    check_closure<fl::SeqPrefixLattice<ClosureEvent>>(fl::Length<ClosureEvent>::at(3));
    check_closure<fl::AffinityLattice>();
    check_closure<fl::NumaNodeLattice>();
    check_closure<fl::EpochLattice>(fl::EpochLattice::successor(fl::EpochLattice::bottom()));
    check_closure<fl::PeakBytesLattice>();
    check_closure<fl::HappensBeforeLattice<4>>();
}

void fractional_lattice_refuses_outside_its_domain() {
    using fl::FractionalLattice;
    using fl::Rational;
    const Rational half{opaque(std::int64_t{1}), opaque(std::int64_t{2})};
    const Rational three_quarters{opaque(std::int64_t{3}), opaque(std::int64_t{4})};
    const std::int64_t bound = opaque(Rational::MAX_SAFE_MAGNITUDE);

    const Rational least{1, bound};
    const Rational next_to_least{1, bound - 1};

    // A share above one is outside the unit interval.
    const Rational three_halves{opaque(std::int64_t{3}), opaque(std::int64_t{2})};
    CRUCIBLE_FATAL_INVARIANT(!three_halves.is_well_formed());

    // The sum of 1/2 and 3/4 is above one.
    const bool is_large_sum_refused =
        aborts([half, three_quarters] { (void)FractionalLattice::add(half, three_quarters); });
    CRUCIBLE_FATAL_INVARIANT(is_large_sum_refused);

    // The reduced denominator of 1/(2^31 - 1) + 1/2^31 is above the bound.
    const bool is_wide_sum_refused =
        aborts([least, next_to_least] { (void)FractionalLattice::add(next_to_least, least); });
    CRUCIBLE_FATAL_INVARIANT(is_wide_sum_refused);

    // The reduced denominator of 1/2^31 * 1/2^31 is above the bound.
    const bool is_wide_product_refused = aborts([least] { (void)FractionalLattice::mul(least, least); });
    CRUCIBLE_FATAL_INVARIANT(is_wide_product_refused);

    // The controls: a sum of exactly one, and a product at the bound.
    CRUCIBLE_FATAL_INVARIANT(FractionalLattice::add(half, half) == FractionalLattice::top());
    CRUCIBLE_FATAL_INVARIANT(FractionalLattice::mul(least, FractionalLattice::top()) == least);
}

}  // namespace

int main() {
    each_lattice_keeps_its_carrier();
    fractional_lattice_refuses_outside_its_domain();
    return 0;
}
