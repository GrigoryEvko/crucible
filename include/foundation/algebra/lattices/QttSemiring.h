#pragma once

// The quantitative-type-theory grade semiring over {0, 1, ≤1, ω}, counting
// how many times a value may be used.
//
// Each grade is an interval of use counts: 0 is [0, 0], the erased value
// that no run time reads; 1 is [1, 1], the linear value used exactly one
// time; ≤1 is [0, 1], the affine value used one time or never; ω is
// [0, ∞), the unrestricted value.  This is the linear quantitative type
// theory of Atkey (LICS 2018), with the affine grade, read through
// intervals.
//
// One carrier, two independent structures.  The order is the inclusion of
// the intervals: 0 ⊑ ≤1, 1 ⊑ ≤1 and ≤1 ⊑ ω, and 0 and 1 are incomparable.
// So a value used exactly one time never stands where a value must not be
// used, and the reverse.  0 and 1 have no lower bound, so the order has a
// join and no meet: the carrier is a join-semilattice with leq and join
// only, and it is not a lattice.
//
// The semiring gives add and mul.  add sums two intervals and mul
// multiplies them, and each result rounds up to the least grade that holds
// it.  So 1 + 1 is ω, while join(1, 1) stays 1: two uses lose linearity,
// and a join is idempotent.  Reaching for join where add belongs silently
// keeps a value linear that is no longer linear.
//
// The ordinal of a grade is its identity, and it is not the order.  The
// three first grades keep the ordinals that cache keys hold, and ≤1 takes
// the next free ordinal.
//
// At<Grade> pins a grade in the type and carries no runtime state, so
// its ops are all trivially identity: a one-element lattice has nothing
// to compare.
//
// The grade names are the QTT symbols 0, 1, ≤1 and ω, not the enumerator
// identifiers.  Because of this, qtt_grade_name stays hand-written, where
// every other lattice reads its names by reflection.  The name of
// At<Grade> comes from qtt_grade_name.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Anchor.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// The ordinal is the identity of a grade, not its place in the order.
enum class QttGrade : std::int8_t {
    Zero = 0,  // erased: [0, 0], no use at run time
    One = 1,  // linear: [1, 1], exactly one use
    Omega = 2,  // unrestricted: [0, ∞), any number of uses
    AtMostOne = 3,  // affine: [0, 1], one use or none
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
        case QttGrade::AtMostOne:
            return "\xE2\x89\xA4"
                   "1";  // UTF-8 ≤1
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
    ::foundation::reflect::anchored_t<std::meta::reflect_constant(Grade), std::string> text{"QttSemiring::At<"};
    text += symbol == unknown_qtt_grade_name ? std::string_view{"?"} : symbol;
    text += '>';
    return std::define_static_string(text);
}

}  // namespace detail

// A grade that permits more uses is the stronger claim: a move from one
// use to many can duplicate a linear value.
struct QttSemiring {
    using element_type = QttGrade;

    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    // a ⊑ b when each use count that a admits, b admits too.  ω admits
    // each count, ≤1 admits 0 and 1, and 0 and 1 admit only themselves.
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b || b == QttGrade::Omega) return true;
        return b == QttGrade::AtMostOne && (a == QttGrade::Zero || a == QttGrade::One);
    }

    // The least grade above both.  0 and 1 are the one incomparable pair,
    // and ≤1 is their least upper bound.
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (leq(a, b)) return b;
        if (leq(b, a)) return a;
        return QttGrade::AtMostOne;
    }

    [[nodiscard]] static constexpr element_type zero() noexcept { return QttGrade::Zero; }
    [[nodiscard]] static constexpr element_type one() noexcept { return QttGrade::One; }

    // The sum of two usage counts.  A sum of two grades other than 0 can
    // reach two uses, and only ω holds two uses, so the sum is ω.  That is
    // where linearity is lost.
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        if (a == QttGrade::Zero) return b;
        if (b == QttGrade::Zero) return a;
        return QttGrade::Omega;
    }

    // Composition of usage counts under application: a function used at
    // grade r, applied to an argument at grade s, uses that argument at
    // grade r·s.  [0, 1]·[0, 1] is [0, 1], and each other product with ω is
    // [0, ∞).
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        if (a == QttGrade::Zero || b == QttGrade::Zero) return QttGrade::Zero;
        if (a == QttGrade::One) return b;
        if (b == QttGrade::One) return a;
        if (a == QttGrade::AtMostOne && b == QttGrade::AtMostOne) return QttGrade::AtMostOne;
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

// One name for each grade.  The grades for one use and for at most one
// use are named LinearGrade and AffineGrade, not Linear and Affine,
// because the wrappers built on top of them own the bare names.
namespace qtt {
using Erased = QttSemiring::At<QttGrade::Zero>;
using LinearGrade = QttSemiring::At<QttGrade::One>;
using AffineGrade = QttSemiring::At<QttGrade::AtMostOne>;
using Unrestricted = QttSemiring::At<QttGrade::Omega>;
}  // namespace qtt

namespace detail {

// The carrier of a linear value.  The check file of this header and
// test/foundation/test_lattices_core.cpp name it.
template <typename T>
using LinearGraded = Graded<ModalityKind::Absolute, qtt::LinearGrade, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
