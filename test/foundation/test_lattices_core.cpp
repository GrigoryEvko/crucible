// Sentinel TU for the core-wrapper lattices and the saturation helpers.
// It includes each header, so each is compiled under the project flags,
// and it runs each operation on an argument that the optimizer cannot
// see.  Each result is compared with the value that the lattice or the
// semiring defines.
//
// It also pins the storage regime each lattice selects when it grades a
// value, because that is the property a wrapper author relies on and
// the one a lattice edit can silently change:
//
//   empty grade (EBO)         Bool, Trust, Conf::At, Qtt::At  sizeof(T)
//   grade is the value        Monotone                        sizeof(T)
//   grade derived from value  SeqPrefix with grade_of         sizeof(T)
//   two fields                Staleness, Fractional           more

#include <foundation/Saturate.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/algebra/lattices/TrustLattice.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <source_location>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;

struct pred_positive {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept {
        return v > T{0};
    }
};
struct source_internal {};

struct Payload {
    std::uint64_t a{0};
    std::uint64_t b{0};
};

// The authority for the carriers built below.  It hands its key out,
// which an authority in production code never does.
struct test_authority {
    [[nodiscard]] static constexpr fa::grade_key<test_authority> key() noexcept {
        return fa::grade_key<test_authority>{};
    }
};

// Regime 1: the grade is empty and collapses.
using RefinedInt = fa::Graded<fa::ModalityKind::Absolute, fl::BoolLattice<pred_positive>, int>;
using TaggedInt = fa::Graded<fa::ModalityKind::RelativeMonad, fl::TrustLattice<source_internal>, int>;
using SecretInt = fa::Graded<fa::ModalityKind::Comonad, fl::conf::SecretTier, int>;
using LinearInt = fa::Graded<fa::ModalityKind::Absolute, fl::qtt::LinearGrade, int>;
static_assert(sizeof(RefinedInt) == sizeof(int));
static_assert(sizeof(TaggedInt) == sizeof(int));
static_assert(sizeof(SecretInt) == sizeof(int));
static_assert(sizeof(LinearInt) == sizeof(int));
static_assert(sizeof(fa::Graded<fa::ModalityKind::Absolute, fl::qtt::LinearGrade, Payload>) == sizeof(Payload));

// Regime 2: the grade is the value.
using MonotonicU64 =
    fa::Graded<fa::ModalityKind::Absolute, fl::MonotoneLattice<std::uint64_t, std::less<std::uint64_t>>, std::uint64_t>;
static_assert(sizeof(MonotonicU64) == sizeof(std::uint64_t));

// Regime 3: the grade is derived from the value through grade_of.
struct Log {
    std::size_t count{0};
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
};
struct Event {};
using AppendOnlyLog = fa::Graded<fa::ModalityKind::Absolute, fl::SeqPrefixLattice<Event>, Log>;
static_assert(fa::LatticeDerivesGrade<fl::SeqPrefixLattice<Event>, Log>);
static_assert(sizeof(AppendOnlyLog) == sizeof(Log));

// Regime 4: value and grade both stored.
using StaleU64 = fa::Graded<fa::ModalityKind::Absolute, fl::StalenessSemiring, std::uint64_t>;
using SharedU64 = fa::Graded<fa::ModalityKind::Absolute, fl::DualLattice<fl::FractionalLattice>, std::uint64_t>;
static_assert(sizeof(StaleU64) == sizeof(std::uint64_t) + sizeof(fl::StalenessSemiring::element_type));
static_assert(sizeof(SharedU64) == sizeof(std::uint64_t) + sizeof(fl::Rational));

// The two semirings are also lattices, and the two orders are distinct
// structures on one carrier.
static_assert(fa::Semiring<fl::QttSemiring> && fa::BoundedLattice<fl::QttSemiring>);
static_assert(fa::Semiring<fl::StalenessSemiring> && fa::BoundedLattice<fl::StalenessSemiring>);
static_assert(fa::Semiring<fl::FractionalLattice> && fa::BoundedLattice<fl::FractionalLattice>);

int failed_checks = 0;

void expect(bool holds, std::source_location where = std::source_location::current()) noexcept {
    if (holds) return;
    std::fprintf(stderr, "check failed at %s:%u\n", where.file_name(), where.line());
    ++failed_checks;
}

// The value comes back through a call that no optimization looks into, so
// each operation below runs on a run-time argument, and each contract
// predicate on the way is evaluated and not folded.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

// Equality on a floating-point value is an error under the project
// warning set, so a double is compared by its bits.
[[nodiscard]] bool same_bits(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

// A reduced rational is compared member by member.  Its operator== cross
// multiplies, so it cannot tell 3/4 from 6/8.
[[nodiscard]] bool is_exactly(fl::Rational value, std::int64_t num, std::int64_t den) noexcept {
    return value.num == num && value.den == den;
}

void saturate_runs_at_run_time() {
    using ::foundation::sat::add_sat;
    using ::foundation::sat::mul_sat;
    using ::foundation::sat::sub_sat;
    const unsigned char high = opaque<unsigned char>(250);
    const unsigned char step = opaque<unsigned char>(10);
    expect(add_sat(high, step) == 255);
    expect(add_sat(step, step) == 20);
    expect(sub_sat(step, high) == 0);
    expect(sub_sat(high, step) == 240);
    expect(mul_sat(high, step) == 255);
    expect(mul_sat(step, step) == 100);

    const signed char negative = opaque<signed char>(-120);
    const signed char ten = opaque<signed char>(10);
    expect(sub_sat(negative, ten) == -128);
    expect(add_sat(negative, ten) == -110);

    const int million = opaque(1000000);
    expect(add_sat(million, million) == 2000000);
    expect(mul_sat(million, million) == std::numeric_limits<int>::max());
    expect(mul_sat(-million, million) == std::numeric_limits<int>::min());
}

void bool_lattice_runs_at_run_time() {
    using namespace fl::detail::bool_lattice_self_test;
    using L = fl::BoolLattice<positive>;
    const L::element_type a = opaque(L::bottom());
    const L::element_type b = opaque(L::top());
    expect(L::leq(a, b) && L::leq(b, a));
    expect(L::join(a, b) == L::top());
    expect(L::meet(a, b) == L::bottom());

    // The lattice holds only the identity of each predicate.  The checks
    // themselves are what a wrapper runs at construction.
    expect(positive::check(opaque(1)) && !positive::check(opaque(0)));
    expect(non_negative::check(opaque(0)) && !non_negative::check(opaque(-1)));
    expect(non_zero::check(opaque(-1)) && !non_zero::check(opaque(0)));

    const OneByteValue value{opaque('*')};
    const RefinedPositive<OneByteValue> initial{test_authority::key(), value, L::bottom()};
    const auto widened = initial.weaken(L::top());
    auto composed = initial.compose(widened);
    expect(widened.peek().c == '*' && composed.peek().c == '*');
    RefinedPositive<OneByteValue> moved = std::move(composed).compose(initial);
    expect(std::move(moved).consume().c == '*');
}

void trust_lattice_runs_at_run_time() {
    using namespace fl::detail::trust_lattice_self_test;
    using L = fl::TrustLattice<source::Sanitized>;
    const L::element_type a = opaque(L::bottom());
    const L::element_type b = opaque(L::top());
    expect(L::leq(a, b) && L::leq(b, a));
    expect(L::join(a, b) == L::top());
    expect(L::meet(a, b) == L::bottom());

    const OneByteValue value{opaque('*')};
    const TaggedSanitized<OneByteValue> initial{test_authority::key(), value, L::bottom()};
    const auto composed = initial.compose(initial.weaken(L::top()));
    expect(composed.peek().c == '*');

    // inject() is reachable only because the modality is RelativeMonad.
    auto injected =
        TaggedSanitized<OneByteValue>::inject(test_authority::key(), OneByteValue{opaque('c')}, L::bottom());
    expect(std::move(injected).consume().c == 'c');
}

void conf_lattice_runs_at_run_time() {
    using namespace fl::detail::conf_lattice_self_test;
    using fl::Conf;
    using fl::ConfLattice;
    const Conf lower = opaque(Conf::Public);
    const Conf upper = opaque(Conf::Secret);
    expect(ConfLattice::leq(lower, upper));
    expect(!ConfLattice::leq(upper, lower));
    expect(ConfLattice::join(lower, upper) == Conf::Secret);
    expect(ConfLattice::join(upper, lower) == Conf::Secret);
    expect(ConfLattice::meet(lower, upper) == Conf::Public);
    expect(ConfLattice::bottom() == Conf::Public);
    expect(ConfLattice::top() == Conf::Secret);

    const OneByteValue value{opaque('*')};
    const SecretGraded<OneByteValue> initial{test_authority::key(), value, fl::conf::SecretTier::bottom()};
    auto composed = initial.compose(initial.weaken(fl::conf::SecretTier::top()));

    // extract() is reachable only because the modality is Comonad.
    expect(std::move(composed).extract().c == '*');

    // A pinned element converts to the classification it pins.
    const fl::conf::SecretTier::element_type secret_grade = opaque(fl::conf::SecretTier::top());
    expect(static_cast<Conf>(secret_grade) == Conf::Secret);
    const fl::conf::PublicTier::element_type public_grade = opaque(fl::conf::PublicTier::top());
    expect(static_cast<Conf>(public_grade) == Conf::Public);
}

void qtt_semiring_runs_at_run_time() {
    using namespace fl::detail::qtt_self_test;
    using fl::QttGrade;
    using fl::QttSemiring;
    const QttGrade zero = opaque(QttGrade::Zero);
    const QttGrade one = opaque(QttGrade::One);
    const QttGrade omega = opaque(QttGrade::Omega);
    expect(QttSemiring::leq(zero, one) && QttSemiring::leq(one, omega));
    expect(!QttSemiring::leq(omega, one));
    expect(QttSemiring::join(one, omega) == QttGrade::Omega);
    expect(QttSemiring::meet(one, omega) == QttGrade::One);
    expect(QttSemiring::join(one, one) == QttGrade::One);

    // Two uses of a value are not one use: add saturates where join does not.
    expect(QttSemiring::add(one, one) == QttGrade::Omega);
    expect(QttSemiring::add(zero, one) == QttGrade::One);
    expect(QttSemiring::mul(zero, omega) == QttGrade::Zero);
    expect(QttSemiring::mul(one, omega) == QttGrade::Omega);
    expect(QttSemiring::mul(omega, omega) == QttGrade::Omega);
    expect(QttSemiring::zero() == QttGrade::Zero);
    expect(QttSemiring::one() == QttGrade::One);

    const OneByteValue value{opaque('*')};
    const LinearGraded<OneByteValue> initial{test_authority::key(), value, fl::qtt::LinearGrade::bottom()};
    auto composed = initial.compose(initial.weaken(fl::qtt::LinearGrade::top()));
    expect(composed.peek().c == '*');
    expect(std::move(composed).consume().c == '*');
}

void monotone_lattice_runs_at_run_time() {
    using namespace fl::detail::monotone_lattice_self_test;
    const std::uint64_t low = opaque(std::uint64_t{100});
    const std::uint64_t high = opaque(std::uint64_t{1000});

    expect(MonU64Less::leq(low, high) && !MonU64Less::leq(high, low));
    expect(MonU64Less::join(low, high) == 1000 && MonU64Less::meet(low, high) == 100);
    expect(MonU64Less::bottom() == 0);
    expect(MonU64Less::top() == std::numeric_limits<std::uint64_t>::max());

    // The reversed comparison reverses the order: the join is the smaller
    // number.
    expect(MonU64Greater::leq(high, low) && !MonU64Greater::leq(low, high));
    expect(MonU64Greater::join(low, high) == 100 && MonU64Greater::meet(low, high) == 1000);

    // The value is its grade, so a rebuild at a higher grade is a larger
    // value.
    const MonotonicGraded<std::uint64_t> initial{low};
    expect(initial.grade() == 100 && initial.weaken(low).grade() == 100);
    auto raised = initial.weaken(high);
    expect(raised.grade() == 1000 && raised.peek() == 1000);
    const auto composed = initial.compose(raised);
    expect(composed.grade() == 1000);
    auto moved = std::move(raised).weaken(high);
    expect(std::move(moved).consume() == 1000);

    const double minus_one = opaque(-1.0);
    const double zero = opaque(0.0);
    const double infinity = opaque(std::numeric_limits<double>::infinity());
    expect(MonF64Less::leq(minus_one, zero) && !MonF64Less::leq(zero, minus_one));
    expect(same_bits(MonF64Less::join(minus_one, infinity), std::numeric_limits<double>::infinity()));
    expect(same_bits(MonF64Less::meet(minus_one, infinity), -1.0));
    expect(MonF64Less::is_nan_safe(minus_one) && MonF64Less::is_nan_safe(zero) && MonF64Less::is_nan_safe(infinity));

    // A NaN never reaches leq, join or meet: the invariant there would
    // abort the test.  The test that keeps it out answers no.
    expect(!MonF64Less::is_nan_safe(opaque(std::nan(""))));
}

void seq_prefix_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::seq_prefix_lattice_self_test;
    const std::size_t n_a = opaque(std::size_t{5});
    const std::size_t n_b = opaque(std::size_t{17});
    Length<EventA> a{n_a};
    Length<EventA> b{n_b};

    expect(LatA::leq(a, b) && !LatA::leq(b, a));
    expect(LatA::join(a, b).length == 17 && LatA::meet(a, b).length == 5);
    expect(LatA::join(b, a).length == 17 && LatA::meet(b, a).length == 5);
    expect(LatA::bottom().length == 0);
    expect(LatA::top().length == std::numeric_limits<std::size_t>::max());

    // A longer prefix is the stronger claim.  The prefix is derived from
    // the log, and it is never stored beside a value.
    AppendOnlyGraded<MiniLog> log{MiniLog{n_b}};
    [[maybe_unused]] auto g1 = log.grade();
    [[maybe_unused]] auto v1 = log.peek().size();
    [[maybe_unused]] auto v2 = std::move(log).consume().size();
}

void staleness_semiring_runs_at_run_time() {
    using namespace fl::detail::staleness_semiring_self_test;
    using fl::StalenessSemiring;
    const StalenessSemiring::element_type fresher{opaque(std::uint64_t{5})};
    const StalenessSemiring::element_type staler{opaque(std::uint64_t{17})};
    expect(StalenessSemiring::leq(fresher, staler) && !StalenessSemiring::leq(staler, fresher));
    expect(StalenessSemiring::join(fresher, staler).value == 17);
    expect(StalenessSemiring::meet(fresher, staler).value == 5);

    // Tropical add picks the fresher estimate, and tropical mul adds the
    // steps along a chain.
    expect(StalenessSemiring::add(fresher, staler).value == 5);
    expect(StalenessSemiring::mul(fresher, staler).value == 22);
    expect(StalenessSemiring::mul(fresher, StalenessSemiring::top()).is_infinite());
    const StalenessSemiring::element_type near_top{opaque(std::numeric_limits<std::uint64_t>::max() - 5)};
    expect(StalenessSemiring::mul(near_top, staler).is_infinite());
    expect(fl::staleness::at(opaque(std::uint64_t{5})) == fresher);

    const OneByteValue value{opaque('*')};
    const StaleGraded<OneByteValue> initial{test_authority::key(), value, StalenessSemiring::bottom()};
    const auto raised = initial.weaken(fresher).weaken(staler);
    expect(raised.grade().value == 17 && raised.peek().c == '*');
    auto composed = initial.compose(raised);
    expect(composed.grade().value == 17);
    expect(std::move(composed).consume().c == '*');
}

void fractional_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::fractional_lattice_self_test;
    const Rational a{opaque(std::int64_t{1}), opaque(std::int64_t{4})};
    const Rational b{opaque(std::int64_t{1}), opaque(std::int64_t{2})};
    expect(FractionalLattice::leq(a, b) && !FractionalLattice::leq(b, a));
    expect(is_exactly(FractionalLattice::join(a, b), 1, 2));
    expect(is_exactly(FractionalLattice::meet(a, b), 1, 4));

    // add and mul hand back the reduced form.
    expect(is_exactly(FractionalLattice::add(a, b), 3, 4));
    expect(is_exactly(FractionalLattice::add(b, b), 1, 1));
    expect(is_exactly(FractionalLattice::mul(a, b), 1, 8));
    expect(is_exactly(simplify(Rational{opaque(std::int64_t{2}), opaque(std::int64_t{6})}), 1, 3));

    // Only the comparison is exercised at the bound.  Adding or multiplying two
    // shares this large produces an unreduced denominator past the bound even
    // where the reduced result would fit, which would test reduction behaviour
    // rather than the comparison this probe is about.
    const Rational large_lo{1, opaque(Rational::MAX_SAFE_MAGNITUDE)};
    const Rational large_hi{opaque(Rational::MAX_SAFE_MAGNITUDE - 1), Rational::MAX_SAFE_MAGNITUDE};
    expect(FractionalLattice::leq(large_lo, large_hi) && !FractionalLattice::leq(large_hi, large_lo));
    expect(is_exactly(FractionalLattice::join(large_lo, large_hi), Rational::MAX_SAFE_MAGNITUDE - 1,
                      Rational::MAX_SAFE_MAGNITUDE));
    expect(is_exactly(FractionalLattice::meet(large_lo, large_hi), 1, Rational::MAX_SAFE_MAGNITUDE));

    // A share is stored through the order dual.  A weaker grade is a
    // smaller share, and the shares below are built in descending
    // sequence.  A request for a larger share violates the precondition.
    OneByteValue v{42};
    SharedPermissionGraded<OneByteValue> initial{test_authority::key(), v, FractionalLattice::top()};
    auto widened = initial.weaken(Rational{3, 4});
    auto widened_max = widened.weaken(FractionalLattice::bottom());
    auto composed = initial.compose(widened_max);
    auto rv_widen = std::move(widened_max).weaken(FractionalLattice::bottom());

    // Composing into a separate handle lets the result be consumed without
    // aliasing either operand, which is what reaches the rvalue overloads.
    SharedPermissionGraded<OneByteValue> for_consume = rv_widen.compose(composed);
    OneByteValue consumed = std::move(for_consume).consume();

    // compose keeps the payload and grades it at the join of the two grades.
    using SharedLattice = SharedPermissionGraded<OneByteValue>::lattice_type;
    expect(composed.grade() == SharedLattice::join(initial.grade(), rv_widen.grade()));
    expect(composed.peek().c == 42 && consumed.c == 42);
}

}  // namespace

int main() {
    saturate_runs_at_run_time();
    bool_lattice_runs_at_run_time();
    trust_lattice_runs_at_run_time();
    conf_lattice_runs_at_run_time();
    qtt_semiring_runs_at_run_time();
    monotone_lattice_runs_at_run_time();
    seq_prefix_lattice_runs_at_run_time();
    staleness_semiring_runs_at_run_time();
    fractional_lattice_runs_at_run_time();

    // Each regime built at runtime with non-constant operands.
    int value = 7;  // deliberately not constexpr
    RefinedInt refined{test_authority::key(), value, fl::BoolLattice<pred_positive>::bottom()};
    MonotonicU64 monotonic{static_cast<std::uint64_t>(value)};
    AppendOnlyLog log = AppendOnlyLog::at_bottom();
    StaleU64 stale{test_authority::key(), static_cast<std::uint64_t>(value),
                   fl::staleness::at(static_cast<std::uint64_t>(value))};
    SharedU64 shared{test_authority::key(), static_cast<std::uint64_t>(value), fl::Rational{1, 2}};
    if (refined.peek() != value) return 1;
    if (monotonic.grade() != static_cast<std::uint64_t>(value)) return 2;
    if (log.grade().length != 0) return 3;
    if (stale.grade().value != static_cast<std::uint64_t>(value)) return 4;
    if (!(shared.grade() == fl::Rational{2, 4})) return 5;
    return failed_checks == 0 ? 0 : 6;
}
