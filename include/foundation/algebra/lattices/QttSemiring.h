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

namespace detail {

// The carrier of a linear value.  The check file of this header and
// test/foundation/test_lattices_core.cpp name it.
template <typename T>
using LinearGraded = Graded<ModalityKind::Absolute, qtt::LinearGrade, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
