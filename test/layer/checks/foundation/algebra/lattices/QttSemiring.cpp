// The compile-time checks of foundation/algebra/lattices/QttSemiring.h.

#include <foundation/algebra/lattices/QttSemiring.h>

namespace foundation::algebra::lattices {

namespace detail::qtt_self_test {

static_assert(::foundation::reflect::enum_count<QttGrade> == 3,
              "QttGrade must hold exactly the three grades 0, 1 and ω.");

// The hand-written switch is the one place where a new grade can be
// missed, and this walk covers each grade.  The name of each At<Grade>
// comes from the same switch.
[[nodiscard]] consteval bool every_qtt_grade_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^QttGrade));
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

// The chain order, the exhaustive lattice axioms and the shape of every
// At<grade>.  The At name check inside accepts the hand-written symbols
// because it asks only for a non-empty name that is not the sentinel.
static_assert(verify_chain_lattice<QttSemiring>(),
              "QttSemiring: the chain order or the pinned grades diverged from the "
              "QttGrade enumerator list.");

static_assert(Semiring<QttSemiring>);

[[nodiscard]] consteval bool exhaustive_semiring_check() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^QttGrade));
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

static_assert(QttSemiring::add(QttGrade::One, QttGrade::One) == QttGrade::Omega,
              "QTT additive saturation: One+One must equal Omega (linearity loss).");
static_assert(QttSemiring::mul(QttGrade::Zero, QttGrade::Omega) == QttGrade::Zero,
              "QTT multiplicative absorption: Zero·anything = Zero.");
static_assert(QttSemiring::mul(QttGrade::One, QttGrade::Omega) == QttGrade::Omega,
              "QTT multiplicative identity: One·x = x.");

static_assert(QttSemiring::join(QttGrade::One, QttGrade::One) == QttGrade::One,
              "Lattice join is idempotent: One ∨ One = One (chain max), unlike "
              "semiring add One+One = Omega.");

static_assert(QttSemiring::name() == "QttSemiring");
static_assert(QttSemiring::bottom() == QttGrade::Zero && QttSemiring::top() == QttGrade::Omega);
static_assert(claim_orientation_v<QttSemiring> == ClaimOrientation::stronger_is_higher);
static_assert(QttSemiring::At<QttGrade::One>::name() == "QttSemiring::At<1>");
static_assert(QttSemiring::At<QttGrade::Omega>::name() == "QttSemiring::At<\xCF\x89>");
static_assert(QttSemiring::At<static_cast<QttGrade>(9)>::name() == "QttSemiring::At<?>");
static_assert(qtt_grade_name(QttGrade::Zero) == "0");
static_assert(qtt_grade_name(QttGrade::One) == "1");

static_assert(qtt::Erased::grade == QttGrade::Zero);
static_assert(qtt::LinearGrade::grade == QttGrade::One);
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
