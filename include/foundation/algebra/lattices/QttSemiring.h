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

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class QttGrade : std::int8_t {
    Zero = 0,
    One = 1,
    Omega = 2,
};

inline constexpr std::size_t qtt_grade_count = std::meta::enumerators_of(^^QttGrade).size();

[[nodiscard]] consteval std::string_view qtt_grade_name(QttGrade g) noexcept {
    switch (g) {
        case QttGrade::Zero:
            return "0";
        case QttGrade::One:
            return "1";
        case QttGrade::Omega:
            return "\xCF\x89";  // UTF-8 ω
        default:
            return std::string_view{"<unknown QttGrade>"};
    }
}

struct QttSemiring {
    using element_type = QttGrade;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return QttGrade::Zero; }
    [[nodiscard]] static constexpr element_type top() noexcept { return QttGrade::Omega; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return std::to_underlying(a) <= std::to_underlying(b);
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return leq(a, b) ? b : a;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;
    }

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

    [[nodiscard]] static consteval std::string_view name() noexcept { return "QttSemiring"; }

    template <QttGrade Grade>
    struct At {
        struct element_type {
            using grade_value_type = QttGrade;
            [[nodiscard]] constexpr operator grade_value_type() const noexcept { return Grade; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr QttGrade grade = Grade;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (Grade) {
                case QttGrade::Zero:
                    return "QttSemiring::At<0>";
                case QttGrade::One:
                    return "QttSemiring::At<1>";
                case QttGrade::Omega:
                    return "QttSemiring::At<\xCF\x89>";  // UTF-8 ω
                default:
                    return "QttSemiring::At<?>";
            }
        }
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

static_assert(qtt_grade_count == 3, "QttGrade must hold exactly the three grades 0, 1 and ω.");

[[nodiscard]] consteval bool every_qtt_grade_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^QttGrade));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (qtt_grade_name([:en:]) == std::string_view{"<unknown QttGrade>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_qtt_grade_has_name(), "qtt_grade_name() has no arm for at least one grade, so that grade "
                                          "reports the '<unknown QttGrade>' sentinel.");

static_assert(Lattice<QttSemiring>);
static_assert(BoundedLattice<QttSemiring>);
static_assert(Semiring<QttSemiring>);

static_assert(Lattice<QttSemiring::At<QttGrade::Zero>>);
static_assert(Lattice<QttSemiring::At<QttGrade::One>>);
static_assert(Lattice<QttSemiring::At<QttGrade::Omega>>);
static_assert(BoundedLattice<QttSemiring::At<QttGrade::One>>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<QttSemiring::At<QttGrade::Zero>::element_type>);
static_assert(std::is_empty_v<QttSemiring::At<QttGrade::One>::element_type>);
static_assert(std::is_empty_v<QttSemiring::At<QttGrade::Omega>::element_type>);

[[nodiscard]] consteval bool exhaustive_lattice_check() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^QttGrade));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_bounded_lattice_axioms_at<QttSemiring>([:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(exhaustive_lattice_check(), "QttSemiring's chain-order lattice axioms must hold at every "
                                          "(QttGrade)³ triple.");

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
static_assert(QttSemiring::At<QttGrade::One>::name() == "QttSemiring::At<1>");
static_assert(qtt_grade_name(QttGrade::Zero) == "0");
static_assert(qtt_grade_name(QttGrade::One) == "1");

[[nodiscard]] consteval bool every_at_grade_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^QttGrade));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        // A splice in template-argument position needs the parentheses
        // to keep `<:` from lexing as a digraph.
        if (QttSemiring::At<([:en:])>::name() == std::string_view{"QttSemiring::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_grade_has_name(), "QttSemiring::At<Grade>::name() has no arm for at least one grade, "
                                         "so that grade reports the 'QttSemiring::At<?>' sentinel.");

static_assert(qtt::Erased::grade == QttGrade::Zero);
static_assert(qtt::LinearGrade::grade == QttGrade::One);
static_assert(qtt::Unrestricted::grade == QttGrade::Omega);

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
// The arithmetic witnesses pin the collapse across the
// trivially-default-constructible split as well as the class one.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, double);

inline void runtime_smoke_test() {
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

}  // namespace detail::qtt_self_test

}  // namespace foundation::algebra::lattices
