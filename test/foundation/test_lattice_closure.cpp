// Each operation of each lattice keeps its result in the carrier of the
// lattice, or it refuses its operands.  The carrier is the order of the
// lattice, from bottom up to top, and the predicate is_well_formed of the
// element when the element has one.  A value outside the carrier makes each
// later leq compare against a grade that no element names.
//
// join and meet refuse no two values of the carrier, so a refusal of them
// ends the test.  add and mul run in a child process of the child door of
// fixy/os/Process.h, because an operation can refuse its operands with a
// check that ends the process.  A refusal prints the report of the check,
// and the parent requires that report.
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
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/core/Number.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Abort.h>

#include <concepts>
#include <cstdint>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fp = ::fixy::process;
using ::fixy::TestRunnerCtx;

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

// The text of the child whose result is outside the carrier.
constexpr ::fixy::TextView kLeftTheCarrier{"the result of an operation is outside the carrier of its lattice"};

// Runs operation in a child of the child door.  The child exits with the
// code 0 when the result is in the carrier of L.  A refusal of the operands
// ends the child by SIGABRT with the report of a check of the base: a
// contract or a fatal invariant.  A result outside the carrier ends the child
// with kLeftTheCarrier, which the parent refuses.
// Complexity: one child process.
template <typename L, typename Operation>
void check_refused_or_in_carrier(TestRunnerCtx const& test, Operation const& operation) {
    fp::Child child = fp::spawn_child(test, [&operation] noexcept {
                          if (!is_in_carrier<L>(operation())) {
                              ::fixy::fatal("the result of an operation is outside the carrier of its lattice");
                          }
                      }).expect("the test starts the child of an operation");
    fp::ChildReport const report = fp::wait(std::move(child)).expect("the test waits for the child of an operation");
    bool const is_kept = report.is_exit_with(0);
    bool const is_refused = report.is_signaled_by(::foundation::os::kAbortSignal)
                         && ::fixy::find(report.output(), ::fixy::TextView{"foundation: "}).is_some()
                         && ::fixy::find(report.output(), kLeftTheCarrier).is_none();
    if (!is_kept && !is_refused) [[unlikely]] {
        ::fixy::fatal("an operation neither kept its result in the carrier nor refused its operands:\n{}",
                      report.output());
    }
}

// Each operation of L at one pair of witnesses.  An add or a mul refuses
// its operands, or its result is in the carrier.
template <typename L>
void check_pair(TestRunnerCtx const& test, fa::LatticeElement<L> a, fa::LatticeElement<L> b) {
    CRUCIBLE_FATAL_INVARIANT(is_in_carrier<L>(a) && is_in_carrier<L>(b));
    CRUCIBLE_FATAL_INVARIANT(is_in_carrier<L>(L::join(opaque(a), opaque(b))));
    CRUCIBLE_FATAL_INVARIANT(is_in_carrier<L>(L::meet(opaque(a), opaque(b))));
    if constexpr (fa::SemiringShape<L>) {
        check_refused_or_in_carrier<L>(test, [a, b] noexcept { return L::add(opaque(a), opaque(b)); });
        check_refused_or_in_carrier<L>(test, [a, b] noexcept { return L::mul(opaque(a), opaque(b)); });
    }
}

template <typename L, typename... Witnesses>
void check_row(TestRunnerCtx const& test, fa::LatticeElement<L> a, Witnesses... witnesses) {
    (check_pair<L>(test, a, witnesses), ...);
}

// Each operation of L at each pair of its bottom, its top and the extra
// witnesses.  Complexity: quadratic in the witnesses, with two child
// processes for each pair of a semiring.
template <typename L, typename... Extra>
void check_closure(TestRunnerCtx const& test, Extra... extra) {
    const auto check_each_pair = [&test](auto... witnesses) { (check_row<L>(test, witnesses, witnesses...), ...); };
    if constexpr (fa::HasBottom<L> && fa::HasTop<L>) {
        check_each_pair(L::bottom(), L::top(), fa::LatticeElement<L>{extra}...);
    } else if constexpr (fa::HasBottom<L>) {
        check_each_pair(L::bottom(), fa::LatticeElement<L>{extra}...);
    } else {
        check_each_pair(fa::LatticeElement<L>{extra}...);
    }
}

struct ClosureEvent {};

void each_lattice_keeps_its_carrier(TestRunnerCtx const& test) {
    using fl::Rational;
    constexpr std::int64_t bound = Rational::MAX_SAFE_MAGNITUDE;
    check_closure<fl::FractionalLattice>(test, Rational{1, 4}, Rational{1, 3}, Rational{1, 2}, Rational{2, 3},
                                         Rational{3, 4}, Rational{1, bound}, Rational{1, bound - 1},
                                         Rational{bound - 1, bound});
    check_closure<fl::DualLattice<fl::FractionalLattice>>(test, Rational{1, 3}, Rational{1, bound});
    check_closure<fl::StalenessSemiring>(test, fl::staleness::at(1), fl::staleness::at(100),
                                         fl::staleness::at(::foundation::core::max_of<std::uint64_t> - 1));
    // The QTT semiring is a join-semilattice with no meet, so it is no
    // lattice of this walk.  Its check file walks each pair and triple of
    // its four grades through leq, join, add and mul.
    check_closure<fl::MonotoneLattice<std::uint64_t>>(test, std::uint64_t{1}, std::uint64_t{1000});
    check_closure<fl::MonotoneLattice<std::int32_t>>(test, std::int32_t{-1}, std::int32_t{0}, std::int32_t{1});
    check_closure<fl::MonotoneLattice<double>>(test, -::foundation::core::infinity_of<double>, -0.0, 0.0,
                                               ::foundation::core::min_subnormal_of<double>,
                                               ::foundation::core::infinity_of<double>);
    check_closure<fl::SeqPrefixLattice<ClosureEvent>>(test, fl::Length<ClosureEvent>::at(3));
    check_closure<fl::AffinityLattice>(test);
    check_closure<fl::NumaNodeLattice>(test);
    check_closure<fl::EpochLattice>(test, fl::EpochLattice::successor(fl::EpochLattice::bottom()));
    check_closure<fl::PeakBytesLattice>(test);
    check_closure<fl::HappensBeforeLattice<4>>(test);
}

// Each refusal runs in a child through expect_dies of the Check family.
void fractional_lattice_refuses_outside_its_domain(TestRunnerCtx const& test) {
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
    ::fixy::check::expect_dies(
        test, [half, three_quarters] noexcept { (void)FractionalLattice::add(half, three_quarters); },
        ::fixy::TextView{"foundation: contract violation: is_sum_defined(a, b)"});

    // The reduced denominator of 1/(2^31 - 1) + 1/2^31 is above the bound.
    ::fixy::check::expect_dies(
        test, [least, next_to_least] noexcept { (void)FractionalLattice::add(next_to_least, least); },
        ::fixy::TextView{"foundation: fatal invariant failed: sum.is_well_formed()"});

    // The reduced denominator of 1/2^31 * 1/2^31 is above the bound.
    ::fixy::check::expect_dies(
        test, [least] noexcept { (void)FractionalLattice::mul(least, least); },
        ::fixy::TextView{"foundation: fatal invariant failed: product.is_well_formed()"});

    // The controls: a sum of exactly one, and a product at the bound.
    CRUCIBLE_FATAL_INVARIANT(FractionalLattice::add(half, half) == FractionalLattice::top());
    CRUCIBLE_FATAL_INVARIANT(FractionalLattice::mul(least, FractionalLattice::top()) == least);
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    each_lattice_keeps_its_carrier(test);
    fractional_lattice_refuses_outside_its_domain(test);
    return 0;
}
