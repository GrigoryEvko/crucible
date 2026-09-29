#pragma once

// The quantitative-type-theory grade semiring over {0, 1, ω}, counting
// how many times a value may be used.
//
// One carrier, two independent structures.  The chain order
// Zero ⊑ One ⊑ Omega gives leq, join and meet.  The semiring gives add
// and mul.  They are different functions: One + One is Omega because two
// uses lose linearity, while join(One, One) stays One because a lattice
// join is idempotent.  Reaching for join where add belongs silently
// keeps a value linear that is no longer linear.
//
// At<Grade> pins a grade in the type and carries no runtime state, so
// its ops are all trivially identity: a one-element lattice has nothing
// to compare.
//
// The grade names are the QTT symbols 0, 1 and ω, not the enumerator
// identifiers.  Because of this, qtt_grade_name stays hand-written, where
// every other lattice reads its names by reflection.  The name of
// At<Grade> comes from qtt_grade_name.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class QttGrade : std::int8_t {
    Zero = 0,
    One = 1,
    Omega = 2,
};

// The name that qtt_grade_name gives for a value outside the enum.
inline constexpr std::string_view unknown_qtt_grade_name = "<unknown QttGrade>";

[[nodiscard]] consteval std::string_view qtt_grade_name(QttGrade g) noexcept {
    switch (g) {
        case QttGrade::Zero:
            return "0";
        case QttGrade::One:
            return "1";
        case QttGrade::Omega:
            return "\xCF\x89";  // UTF-8 ω
        default:
            return unknown_qtt_grade_name;
    }
}

namespace detail {

// "QttSemiring::At<symbol>", or "QttSemiring::At<?>" for a value outside
// the enum.  The text lives in static storage.
template <QttGrade Grade>
[[nodiscard]] consteval std::string_view make_qtt_at_name() {
    const std::string_view symbol = qtt_grade_name(Grade);
    std::string text{"QttSemiring::At<"};
    text += symbol == unknown_qtt_grade_name ? std::string_view{"?"} : symbol;
    text += '>';
    return std::define_static_string(text);
}

}  // namespace detail

// A grade that permits more uses is the stronger claim: a move from one
// use to many can duplicate a linear value.
struct QttSemiring : EnumChainLattice<QttSemiring, QttGrade, ClaimOrientation::stronger_is_higher> {
    [[nodiscard]] static constexpr element_type zero() noexcept { return QttGrade::Zero; }
    [[nodiscard]] static constexpr element_type one() noexcept { return QttGrade::One; }

    // Sum of two usage counts.  Two separate uses of the same value
    // saturate to Omega, which is where linearity is lost.
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        if (a == QttGrade::Zero) return b;
        if (b == QttGrade::Zero) return a;
        return QttGrade::Omega;
    }

    // Composition of usage counts under application: a function used at
    // grade r, applied to an argument at grade s, uses that argument at
    // grade r·s.
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        if (a == QttGrade::Zero || b == QttGrade::Zero) return QttGrade::Zero;
        if (a == QttGrade::One) return b;
        if (b == QttGrade::One) return a;
        return QttGrade::Omega;
    }

    // name() hides the reflected one of PinnedAt because the grade
    // names are symbols, not identifiers.
    template <QttGrade Grade>
    struct At : PinnedAt<QttSemiring, Grade> {
        static constexpr QttGrade grade = Grade;

        [[nodiscard]] static consteval std::string_view name() noexcept { return detail::make_qtt_at_name<Grade>(); }
    };
};

// The grade for one use is named LinearGrade, not Linear, because the
// wrapper built on top of it owns the bare name.
namespace qtt {
using Erased = QttSemiring::At<QttGrade::Zero>;
using LinearGrade = QttSemiring::At<QttGrade::One>;
using Unrestricted = QttSemiring::At<QttGrade::Omega>;
}  // namespace qtt

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

// The carrier of a linear value.  The runtime smoke test in
// test/foundation/test_lattices_core.cpp builds one.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using LinearGraded = Graded<ModalityKind::Absolute, qtt::LinearGrade, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, EightByteValue);

}  // namespace detail::qtt_self_test

}  // namespace foundation::algebra::lattices
