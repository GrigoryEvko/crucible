// Sentinel TU for the core-wrapper lattices and the saturation helpers.
// Each header carries its own static_asserts and an inline
// runtime_smoke_test; this file includes each and calls each once.
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
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/algebra/lattices/TrustLattice.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
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
using SharedU64 = fa::Graded<fa::ModalityKind::Absolute, fl::FractionalLattice, std::uint64_t>;
static_assert(sizeof(StaleU64) == sizeof(std::uint64_t) + sizeof(fl::StalenessSemiring::element_type));
static_assert(sizeof(SharedU64) == sizeof(std::uint64_t) + sizeof(fl::Rational));

// The two semirings are also lattices, and the two orders are distinct
// structures on one carrier.
static_assert(fa::Semiring<fl::QttSemiring> && fa::BoundedLattice<fl::QttSemiring>);
static_assert(fa::Semiring<fl::StalenessSemiring> && fa::BoundedLattice<fl::StalenessSemiring>);
static_assert(fa::Semiring<fl::FractionalLattice> && fa::BoundedLattice<fl::FractionalLattice>);

// Each body below was an inline runtime_smoke_test in its header,
// compiled into every translation unit that included it.  They are
// moved verbatim, and the function-scope using-directives reproduce the
// name lookup each body had inside its header.  The static_assert walls
// stayed behind, because each reads a shipped lattice against its own
// grades.

void saturate_runs_at_run_time() {
    using namespace ::foundation::sat;
    using namespace ::foundation::sat::detail::saturate_self_test;
    unsigned char lo = 250;  // deliberately not constexpr
    unsigned char step = 10;
    [[maybe_unused]] unsigned char clamped_up = add_sat(lo, step);
    [[maybe_unused]] unsigned char clamped_down = sub_sat(step, lo);
    [[maybe_unused]] unsigned char clamped_mul = mul_sat(lo, step);
    int x = 1000000;
    [[maybe_unused]] int sum = add_sat(x, x);
    [[maybe_unused]] int prod = mul_sat(x, x);
}

void bool_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::bool_lattice_self_test;
    using L = BoolLattice<positive>;
    L::element_type a{};
    L::element_type b{};
    [[maybe_unused]] bool l = L::leq(a, b);
    [[maybe_unused]] L::element_type j = L::join(a, b);
    [[maybe_unused]] L::element_type m = L::meet(a, b);

    OneByteValue v{42};
    RefinedPositive<OneByteValue> initial{v, L::bottom()};
    auto widened = initial.weaken(L::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(L::top());
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
}

void trust_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::trust_lattice_self_test;
    using L = TrustLattice<source::Sanitized>;
    L::element_type a{};
    L::element_type b{};
    [[maybe_unused]] bool l = L::leq(a, b);
    [[maybe_unused]] L::element_type j = L::join(a, b);
    [[maybe_unused]] L::element_type m = L::meet(a, b);

    OneByteValue v{42};
    TaggedSanitized<OneByteValue> initial{v, L::bottom()};
    auto widened = initial.weaken(L::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(L::top());

    // inject() is reachable only because the modality is RelativeMonad.
    auto injected = TaggedSanitized<OneByteValue>::inject(OneByteValue{99}, L::bottom());

    [[maybe_unused]] auto g = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(injected).consume().c;
}

void conf_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::conf_lattice_self_test;
    Conf a = Conf::Public;
    Conf b = Conf::Secret;
    [[maybe_unused]] bool l1 = ConfLattice::leq(a, b);
    [[maybe_unused]] Conf j1 = ConfLattice::join(a, b);
    [[maybe_unused]] Conf m1 = ConfLattice::meet(a, b);
    [[maybe_unused]] Conf bot = ConfLattice::bottom();
    [[maybe_unused]] Conf top = ConfLattice::top();

    OneByteValue v{42};
    SecretGraded<OneByteValue> initial{v, conf::SecretTier::bottom()};
    auto widened = initial.weaken(conf::SecretTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(conf::SecretTier::top());

    // extract() is reachable only because the modality is Comonad.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    conf::SecretTier::element_type e{};
    [[maybe_unused]] Conf c = e;
}

void qtt_semiring_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::qtt_self_test;
    QttGrade a = QttGrade::Zero;
    QttGrade b = QttGrade::One;
    QttGrade c = QttGrade::Omega;
    [[maybe_unused]] bool l1 = QttSemiring::leq(a, b);
    [[maybe_unused]] QttGrade j1 = QttSemiring::join(b, c);
    [[maybe_unused]] QttGrade m1 = QttSemiring::meet(b, c);
    [[maybe_unused]] QttGrade ad1 = QttSemiring::add(b, b);
    [[maybe_unused]] QttGrade mu1 = QttSemiring::mul(c, c);
    [[maybe_unused]] QttGrade zr = QttSemiring::zero();
    [[maybe_unused]] QttGrade on = QttSemiring::one();

    OneByteValue v{42};
    LinearGraded<OneByteValue> initial{v, qtt::LinearGrade::bottom()};
    auto widened = initial.weaken(qtt::LinearGrade::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(qtt::LinearGrade::top());
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto g2 = rv_widen.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
}

void monotone_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::monotone_lattice_self_test;
    std::uint64_t a = 100;
    std::uint64_t b = 1000;

    [[maybe_unused]] bool l = MonU64Less::leq(a, b);
    [[maybe_unused]] std::uint64_t j = MonU64Less::join(a, b);
    [[maybe_unused]] std::uint64_t m = MonU64Less::meet(a, b);

    [[maybe_unused]] std::uint64_t bot = MonU64Less::bottom();
    [[maybe_unused]] std::uint64_t top = MonU64Less::top();

    [[maybe_unused]] bool gl = MonU64Greater::leq(b, a);
    [[maybe_unused]] std::uint64_t gj = MonU64Greater::join(a, b);

    // The single-argument constructor sets value and grade together.
    // The two-argument form asserts that its arguments are already
    // lattice-equivalent, which this collapsed shape always makes true.
    MonotonicGraded<std::uint64_t> initial{a};
    auto widened = initial.weaken(a);
    auto widened2 = widened.weaken(b);
    auto composed = initial.compose(widened2);
    auto rv_widen = std::move(widened2).weaken(b);
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek();
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume();
    [[maybe_unused]] auto g2 = rv_widen.grade();

    double fa_v = -1.0;
    double fb_v = 0.0;
    double fc_v = std::numeric_limits<double>::infinity();
    [[maybe_unused]] bool fl1 = MonF64Less::leq(fa_v, fb_v);
    [[maybe_unused]] double fj1 = MonF64Less::join(fa_v, fc_v);
    [[maybe_unused]] double fm1 = MonF64Less::meet(fa_v, fc_v);
    [[maybe_unused]] bool fnan_a = MonF64Less::is_nan_safe(fa_v);
    [[maybe_unused]] bool fnan_b = MonF64Less::is_nan_safe(fb_v);
    [[maybe_unused]] bool fnan_inf = MonF64Less::is_nan_safe(fc_v);

    // This value never reaches leq, join or meet.  Passing it would
    // trip the invariant and abort the smoke test.
    double fnan = std::nan("");
    [[maybe_unused]] bool fnan_fired = !MonF64Less::is_nan_safe(fnan);
}

void seq_prefix_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::seq_prefix_lattice_self_test;
    std::size_t n_a = 5;
    std::size_t n_b = 17;
    Length<EventA> a{n_a};
    Length<EventA> b{n_b};

    [[maybe_unused]] bool l = LatA::leq(a, b);
    [[maybe_unused]] LatA::element_type j = LatA::join(a, b);
    [[maybe_unused]] LatA::element_type m = LatA::meet(a, b);
    [[maybe_unused]] LatA::element_type bot = LatA::bottom();
    [[maybe_unused]] LatA::element_type top = LatA::top();

    OneByteValue v{42};
    AppendOnlyGraded<OneByteValue> initial{v, LatA::bottom()};
    auto widened = initial.weaken(a);
    auto widened2 = widened.weaken(b);
    auto composed = initial.compose(widened2);
    auto rv_widen = std::move(widened2).weaken(b);
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto g2 = rv_widen.grade();
}

void staleness_semiring_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::staleness_semiring_self_test;
    std::uint64_t n_a = 5;
    std::uint64_t n_b = 17;
    auto a = StalenessSemiring::element_type{n_a};
    auto b = StalenessSemiring::element_type{n_b};

    [[maybe_unused]] bool l = StalenessSemiring::leq(a, b);
    [[maybe_unused]] StalenessSemiring::element_type j = StalenessSemiring::join(a, b);
    [[maybe_unused]] StalenessSemiring::element_type m = StalenessSemiring::meet(a, b);

    [[maybe_unused]] auto sum = StalenessSemiring::add(a, b);
    [[maybe_unused]] auto prod = StalenessSemiring::mul(a, b);
    [[maybe_unused]] auto absb = StalenessSemiring::mul(a, StalenessSemiring::top());

    [[maybe_unused]] auto at_n = staleness::at(n_a);

    OneByteValue v{42};
    StaleGraded<OneByteValue> initial{v, StalenessSemiring::bottom()};
    auto widened = initial.weaken(a);
    auto widened2 = widened.weaken(b);
    auto composed = initial.compose(widened2);
    auto rv_widen = std::move(widened2).weaken(b);
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto g2 = rv_widen.grade();
}

void fractional_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::fractional_lattice_self_test;
    std::int64_t numA = 1, denA = 4;
    std::int64_t numB = 1, denB = 2;
    Rational a{numA, denA};
    Rational b{numB, denB};

    [[maybe_unused]] bool l = FractionalLattice::leq(a, b);
    [[maybe_unused]] Rational j = FractionalLattice::join(a, b);
    [[maybe_unused]] Rational m = FractionalLattice::meet(a, b);

    [[maybe_unused]] Rational s = FractionalLattice::add(a, b);
    [[maybe_unused]] Rational p = FractionalLattice::mul(a, b);
    [[maybe_unused]] Rational ss = simplify(Rational{numA + numB, denA + denB});

    // Only the comparison is exercised at the bound.  Adding or multiplying two
    // shares this large produces an unreduced denominator past the bound even
    // where the reduced result would fit, which would test reduction behaviour
    // rather than the comparison this probe is about.
    Rational large_lo{1, Rational::MAX_SAFE_MAGNITUDE};
    Rational large_hi{Rational::MAX_SAFE_MAGNITUDE - 1, Rational::MAX_SAFE_MAGNITUDE};
    [[maybe_unused]] bool large_leq = FractionalLattice::leq(large_lo, large_hi);
    [[maybe_unused]] Rational large_join = FractionalLattice::join(large_lo, large_hi);
    [[maybe_unused]] Rational large_meet = FractionalLattice::meet(large_lo, large_hi);

    // Weakening only ever moves up the order, so the shares below are built in
    // ascending sequence.  Requesting a smaller grade violates the
    // precondition.
    OneByteValue v{42};
    SharedPermissionGraded<OneByteValue> initial{v, FractionalLattice::bottom()};
    auto widened = initial.weaken(Rational{3, 4});
    auto widened_max = widened.weaken(FractionalLattice::top());
    auto composed = initial.compose(widened_max);
    auto rv_widen = std::move(widened_max).weaken(FractionalLattice::top());

    // Composing into a separate handle lets the result be consumed without
    // aliasing either operand, which is what reaches the rvalue overloads.
    SharedPermissionGraded<OneByteValue> for_consume = rv_widen.compose(composed);
    OneByteValue consumed = std::move(for_consume).consume();

    [[maybe_unused]] auto g = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = consumed.c;
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
    RefinedInt refined{value, fl::BoolLattice<pred_positive>::bottom()};
    MonotonicU64 monotonic{static_cast<std::uint64_t>(value)};
    AppendOnlyLog log = AppendOnlyLog::at_bottom();
    StaleU64 stale{static_cast<std::uint64_t>(value), fl::staleness::at(static_cast<std::uint64_t>(value))};
    SharedU64 shared{static_cast<std::uint64_t>(value), fl::Rational{1, 2}};
    if (refined.peek() != value) return 1;
    if (monotonic.grade() != static_cast<std::uint64_t>(value)) return 2;
    if (log.grade().length != 0) return 3;
    if (stale.grade().value != static_cast<std::uint64_t>(value)) return 4;
    if (!(shared.grade() == fl::Rational{2, 4})) return 5;
    return 0;
}
