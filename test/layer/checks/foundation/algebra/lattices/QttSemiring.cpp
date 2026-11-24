// The compile-time checks of foundation/algebra/lattices/QttSemiring.h.

#include <foundation/algebra/lattices/QttSemiring.h>

#include <foundation/reflect/ReflectionList.h>

namespace foundation::algebra::lattices {

namespace detail::qtt_self_test {

static_assert(::foundation::reflect::enum_count<QttGrade> == 4,
              "QttGrade must hold exactly the four grades 0, 1, ≤1 and ω.");

// The grades, read one time for each walk below.
inline constexpr ::foundation::reflect::ReflectionList enumerators =
    ::foundation::reflect::reflection_list(std::meta::enumerators_of(^^QttGrade));

// The hand-written switch is the one place where a new grade can be
// missed, and this walk covers each grade.  The name of each At<Grade>
// comes from the same switch.
[[nodiscard]] consteval bool every_qtt_grade_has_name() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (qtt_grade_name([:en:]) == unknown_qtt_grade_name) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_qtt_grade_has_name(), "qtt_grade_name() has no arm for at least one grade, so that grade "
                                          "reports the '<unknown QttGrade>' sentinel.");

// ── The table of the theory ──────────────────────────────────────────
//
// Each grade is an interval of use counts, and the model below computes
// the order, the join, add and mul of the theory from the intervals.  The
// order is the inclusion of the intervals.  The join is the least grade
// that holds the two intervals, add sums two intervals and mul multiplies
// them, and each result rounds up to the least grade whose interval holds
// it.  The checks compare each pair of grades of the header with the
// model.  unbounded_uses stands for ∞: it is above each finite count that
// the model reaches.
inline constexpr int unbounded_uses = 1000;

struct use_interval {
    int least = 0;
    int most = 0;
};

[[nodiscard]] consteval use_interval interval_of(QttGrade grade) noexcept {
    switch (grade) {
        case QttGrade::Zero:
            return {.least = 0, .most = 0};
        case QttGrade::One:
            return {.least = 1, .most = 1};
        case QttGrade::AtMostOne:
            return {.least = 0, .most = 1};
        case QttGrade::Omega:
        default:
            return {.least = 0, .most = unbounded_uses};
    }
}

[[nodiscard]] consteval bool holds(use_interval outer, use_interval inner) noexcept {
    return outer.least <= inner.least && inner.most <= outer.most;
}

[[nodiscard]] consteval int capped(int uses) noexcept { return uses < unbounded_uses ? uses : unbounded_uses; }

// The least grade whose interval holds the interval given, in the order of
// the size of the intervals.
[[nodiscard]] consteval QttGrade least_grade_holding(use_interval uses) noexcept {
    if (holds(interval_of(QttGrade::Zero), uses)) return QttGrade::Zero;
    if (holds(interval_of(QttGrade::One), uses)) return QttGrade::One;
    if (holds(interval_of(QttGrade::AtMostOne), uses)) return QttGrade::AtMostOne;
    return QttGrade::Omega;
}

[[nodiscard]] consteval bool pair_agrees_with_the_intervals(QttGrade left, QttGrade right) noexcept {
    const use_interval a = interval_of(left);
    const use_interval b = interval_of(right);
    const use_interval hull{.least = a.least < b.least ? a.least : b.least, .most = a.most > b.most ? a.most : b.most};
    const use_interval sum{.least = capped(a.least + b.least), .most = capped(a.most + b.most)};
    const use_interval product{.least = capped(a.least * b.least), .most = capped(a.most * b.most)};
    return QttSemiring::leq(left, right) == holds(b, a) && QttSemiring::join(left, right) == least_grade_holding(hull)
        && QttSemiring::add(left, right) == least_grade_holding(sum)
        && QttSemiring::mul(left, right) == least_grade_holding(product);
}

[[nodiscard]] consteval bool each_pair_agrees_with_the_intervals() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            if (!pair_agrees_with_the_intervals([:ea:], [:eb:])) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(each_pair_agrees_with_the_intervals(),
              "QttSemiring: leq, join, add or mul differs from the interval table of the theory.");

// 0 and 1 are incomparable, so they have no meet: a value used exactly one
// time does not stand where a value must not be used, and the reverse.
static_assert(!QttSemiring::leq(QttGrade::Zero, QttGrade::One) && !QttSemiring::leq(QttGrade::One, QttGrade::Zero),
              "the erased grade and the linear grade must be incomparable");
static_assert(QttSemiring::join(QttGrade::Zero, QttGrade::One) == QttGrade::AtMostOne);

// The carrier has no meet, so it is no lattice, and it is a semiring.
static_assert(!LatticeShape<QttSemiring>);
static_assert(Semiring<QttSemiring>);

[[nodiscard]] consteval bool exhaustive_semiring_check() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_semiring_axioms_at<QttSemiring>([:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(exhaustive_semiring_check(), "QttSemiring's semiring axioms must hold at every (QttGrade)³ "
                                           "triple.");

// The join-semilattice laws at each triple: leq is a partial order, and
// join is the least upper bound under it.
[[nodiscard]] consteval bool triple_obeys_the_join_laws(QttGrade a, QttGrade b, QttGrade c) noexcept {
    const QttGrade upper = QttSemiring::join(a, b);
    const bool is_order = QttSemiring::leq(a, a) && (!QttSemiring::leq(a, b) || !QttSemiring::leq(b, a) || a == b)
                       && (!QttSemiring::leq(a, b) || !QttSemiring::leq(b, c) || QttSemiring::leq(a, c));
    const bool is_least_upper = upper == QttSemiring::join(b, a) && QttSemiring::leq(a, upper)
                             && QttSemiring::leq(b, upper)
                             && (!QttSemiring::leq(a, c) || !QttSemiring::leq(b, c) || QttSemiring::leq(upper, c));
    return is_order && is_least_upper;
}

[[nodiscard]] consteval bool join_semilattice_laws_hold() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!triple_obeys_the_join_laws([:ea:], [:eb:], [:ec:])) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(join_semilattice_laws_hold());

// The ordinals are the identities that cache keys hold, and not the order.
static_assert(static_cast<int>(QttGrade::Zero) == 0 && static_cast<int>(QttGrade::One) == 1
              && static_cast<int>(QttGrade::Omega) == 2 && static_cast<int>(QttGrade::AtMostOne) == 3);

static_assert(QttSemiring::add(QttGrade::One, QttGrade::One) == QttGrade::Omega,
              "QTT additive saturation: One+One must equal Omega (linearity loss).");
static_assert(QttSemiring::mul(QttGrade::AtMostOne, QttGrade::AtMostOne) == QttGrade::AtMostOne,
              "an affine use of an affine value stays affine");
static_assert(QttSemiring::join(QttGrade::One, QttGrade::One) == QttGrade::One,
              "Lattice join is idempotent: One ∨ One = One, unlike semiring add One+One = Omega.");

static_assert(claim_orientation_t<QttSemiring>::value == ClaimOrientation::stronger_is_higher);
static_assert(QttSemiring::At<QttGrade::One>::name() == "QttSemiring::At<1>");
static_assert(QttSemiring::At<QttGrade::Omega>::name() == "QttSemiring::At<\xCF\x89>");
static_assert(QttSemiring::At<QttGrade::AtMostOne>::name()
              == "QttSemiring::At<\xE2\x89\xA4"
                 "1>");
static_assert(QttSemiring::At<static_cast<QttGrade>(9)>::name() == "QttSemiring::At<?>");
static_assert(qtt_grade_name(QttGrade::Zero) == "0");
static_assert(qtt_grade_name(QttGrade::One) == "1");

static_assert(qtt::Erased::grade == QttGrade::Zero);
static_assert(qtt::LinearGrade::grade == QttGrade::One);
static_assert(qtt::AffineGrade::grade == QttGrade::AtMostOne);
static_assert(qtt::Unrestricted::grade == QttGrade::Omega);

// The payloads of a linear value.  The runtime smoke test in
// test/foundation/test_lattices_core.cpp builds one.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, EightByteValue);

}  // namespace detail::qtt_self_test

}  // namespace foundation::algebra::lattices
