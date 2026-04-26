#pragma once

// ── crucible::algebra::lattices::QttSemiring ────────────────────────
//
// Atkey 2018 quantitative-type-theory (QTT) three-grade semiring
// {0, 1, ω}, +, ·, 0, 1.  Foundational for Linear<T> per
// 25_04_2026.md §2.3:
//
//     using Linear<T> = Graded<Absolute, QttSemiring::At<QttGrade::One>, T>;
//
// References:
//   Atkey (FLoC 2018). Syntax and Semantics of Quantitative Type Theory.
//   Brady (2021).      Idris 2: Quantitative Type Theory in Practice.
//
// ── Algebraic structure ─────────────────────────────────────────────
//
// QttSemiring carries TWO independent algebraic structures over the
// same carrier QttGrade ∈ {Zero, One, Omega}:
//
//   1. Lattice (chain order):  Zero ⊑ One ⊑ Omega
//      - leq:   pointwise on the chain
//      - join:  max under chain order
//      - meet:  min under chain order
//      - bottom = Zero, top = Omega
//
//   2. Semiring (Atkey QTT):
//      - add: Zero+x = x; One+One = Omega; ω+x = ω        (additive)
//      - mul: Zero·x = Zero; One·x = x; ω·ω = ω           (multiplicative)
//      - zero = Zero, one = One
//
//   These are DIFFERENT functions on the same carrier.  `add` is NOT
//   `join` — One+One = Omega but max(One, One) = One.  Both
//   structures satisfy their respective axioms (idempotency for
//   lattice, distributivity for semiring) independently.  The
//   Lattice + Semiring concepts in Lattice.h treat each separately;
//   QttSemiring satisfies BOTH.
//
// ── At<Grade>: singleton sub-lattice at a type-level grade ──────────
//
//   QttSemiring::At<QttGrade::One> is a single-element sub-lattice
//   whose element_type is EMPTY (the grade is encoded at the type
//   level via the template parameter, not stored at runtime).  Empty
//   element_type + `[[no_unique_address]] grade_` in Graded gives
//   sizeof(Graded<..., At<One>, T>) == sizeof(T) — the zero-overhead
//   path required by Linear<T>.
//
//   All At<Grade> operations are trivially identity (a singleton
//   lattice has only one element); they exist only to satisfy the
//   Lattice concept and let Graded<> instantiate.
//
//   Axiom coverage: TypeSafe — strong QttGrade enum; concept gates
//                   reject template-parameter typos at substitution
//                   time.  DetSafe — every operation is constexpr.
//   Runtime cost:   zero for At<Grade> over empty T; sizeof(T) for
//                   non-empty T (verified by ALGEBRA-15 #460 sweep).
//
// See feedback_algebra_runtime_smoke_test_discipline memory — every
// algebra/lattices/ header must include `runtime_smoke_test()` that
// exercises operations with non-constant arguments; pure
// static_assert tests miss the consteval/SFINAE/inline-body trap.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── QttGrade enum ───────────────────────────────────────────────────
enum class QttGrade : std::int8_t {
    Zero  = 0,
    One   = 1,
    Omega = 2,
};

// Cardinality + diagnostic name via reflection — auto-bumps when a
// new grade is added; name-coverage assertion in the self-test
// catches missing switch arms.
inline constexpr std::size_t qtt_grade_count =
    std::meta::enumerators_of(^^QttGrade).size();

[[nodiscard]] consteval std::string_view qtt_grade_name(QttGrade g) noexcept {
    switch (g) {
        case QttGrade::Zero:  return "0";
        case QttGrade::One:   return "1";
        case QttGrade::Omega: return "\xCF\x89";  // UTF-8 ω
    }
    return std::string_view{"<unknown QttGrade>"};
}

// ── Full QttSemiring (lattice + semiring) ───────────────────────────
struct QttSemiring {
    using element_type = QttGrade;

    // ── Lattice ops (chain order: Zero ⊑ One ⊑ Omega) ───────────────
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return QttGrade::Zero;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return QttGrade::Omega;
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return std::to_underlying(a) <= std::to_underlying(b);
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return leq(a, b) ? b : a;  // max under chain order
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;  // min under chain order
    }

    // ── Semiring ops (Atkey 2018 QTT) ───────────────────────────────
    [[nodiscard]] static constexpr element_type zero() noexcept {
        return QttGrade::Zero;
    }
    [[nodiscard]] static constexpr element_type one() noexcept {
        return QttGrade::One;
    }

    // add: sum of usage counts.  Zero is additive identity; One+One
    // saturates to Omega (two uses lose linearity); anything+Omega
    // stays Omega.  Verifies additive commutativity / associativity
    // by case analysis (proven in Lean per LEAN-2 #491).
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        if (a == QttGrade::Zero) return b;
        if (b == QttGrade::Zero) return a;
        // Both non-Zero: One+One = Omega; anything-with-Omega = Omega.
        return QttGrade::Omega;
    }

    // mul: compose usage counts.  Zero is multiplicatively absorbing;
    // One is multiplicative identity; Omega·Omega = Omega.  Used for
    // grade composition under function application: f at grade r
    // applied to argument at grade s produces grade r·s.
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        if (a == QttGrade::Zero || b == QttGrade::Zero) return QttGrade::Zero;
        if (a == QttGrade::One) return b;
        if (b == QttGrade::One) return a;
        return QttGrade::Omega;  // Omega · Omega = Omega
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "QttSemiring";
    }

    // ── At<Grade>: singleton sub-lattice at a type-level grade ──────
    //
    // The element_type is empty (struct with one stateless tag);
    // EBO via [[no_unique_address]] grade_ in Graded collapses the
    // grade to zero bytes.  Lattice operations are trivially identity
    // (a singleton lattice has only one element).
    //
    // Used by Linear<T> per 25_04_2026.md §2.3:
    //   using Linear<T> = Graded<Absolute, QttSemiring::At<One>, T>;
    template <QttGrade Grade>
    struct At {
        // Empty tag carrying Grade at the type level.  Comparison is
        // trivially true (only one value possible).  Conversion to
        // QttGrade lets external code recover the grade for
        // diagnostics / serialization.
        struct element_type {
            using grade_value_type = QttGrade;
            static constexpr grade_value_type value = Grade;
            [[nodiscard]] constexpr operator grade_value_type() const noexcept {
                return Grade;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr QttGrade grade = Grade;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (Grade) {
                case QttGrade::Zero:  return "QttSemiring::At<0>";
                case QttGrade::One:   return "QttSemiring::At<1>";
                case QttGrade::Omega: return "QttSemiring::At<\xCF\x89>";  // UTF-8 ω
            }
            return "QttSemiring::At<?>";
        }
    };
};

// ── Convenience aliases for the three positions ─────────────────────
//
// `LinearGrade` rather than `Linear` to avoid colliding with the
// future safety::Linear<T> wrapper alias from MIGRATE-1 (#461).
namespace qtt {
    using Erased       = QttSemiring::At<QttGrade::Zero>;
    using LinearGrade  = QttSemiring::At<QttGrade::One>;
    using Unrestricted = QttSemiring::At<QttGrade::Omega>;
}  // namespace qtt

// ── Self-test (compile-time + reflection-driven name coverage) ──────
namespace detail::qtt_self_test {

// Cardinality.
static_assert(qtt_grade_count == 3,
    "QttGrade catalog diverged from Atkey 2018 {0, 1, ω}; confirm intent.");

// Name coverage via reflection.
[[nodiscard]] consteval bool every_qtt_grade_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^QttGrade));
    template for (constexpr auto en : enumerators) {
        if (qtt_grade_name([:en:]) == std::string_view{"<unknown QttGrade>"}) {
            return false;
        }
    }
    return true;
}
static_assert(every_qtt_grade_has_name(),
    "qtt_grade_name() missing arm for at least one QttGrade — add the "
    "arm or the new grade leaks the '<unknown QttGrade>' sentinel.");

// Concept conformance.
static_assert(Lattice<QttSemiring>);
static_assert(BoundedLattice<QttSemiring>);
static_assert(Semiring<QttSemiring>);

static_assert(Lattice<QttSemiring::At<QttGrade::Zero>>);
static_assert(Lattice<QttSemiring::At<QttGrade::One>>);
static_assert(Lattice<QttSemiring::At<QttGrade::Omega>>);
static_assert(BoundedLattice<QttSemiring::At<QttGrade::One>>);

// At<Grade>::element_type must be EMPTY for EBO collapse (the whole
// point of the At<> sub-lattice).
static_assert(std::is_empty_v<QttSemiring::At<QttGrade::Zero>::element_type>);
static_assert(std::is_empty_v<QttSemiring::At<QttGrade::One>::element_type>);
static_assert(std::is_empty_v<QttSemiring::At<QttGrade::Omega>::element_type>);

// Lattice axioms hold under the chain order.
static_assert(verify_bounded_lattice_axioms_at<QttSemiring>(
    QttGrade::Zero, QttGrade::Zero, QttGrade::Zero));
static_assert(verify_bounded_lattice_axioms_at<QttSemiring>(
    QttGrade::Zero, QttGrade::One,  QttGrade::Omega));
static_assert(verify_bounded_lattice_axioms_at<QttSemiring>(
    QttGrade::One,  QttGrade::Omega, QttGrade::Zero));
static_assert(verify_bounded_lattice_axioms_at<QttSemiring>(
    QttGrade::Omega, QttGrade::Omega, QttGrade::Omega));

// Semiring axioms hold for the QTT operations.
static_assert(verify_semiring_axioms_at<QttSemiring>(
    QttGrade::Zero, QttGrade::Zero, QttGrade::Zero));
static_assert(verify_semiring_axioms_at<QttSemiring>(
    QttGrade::Zero, QttGrade::One, QttGrade::Omega));
static_assert(verify_semiring_axioms_at<QttSemiring>(
    QttGrade::One,  QttGrade::One, QttGrade::One));
static_assert(verify_semiring_axioms_at<QttSemiring>(
    QttGrade::Omega, QttGrade::Omega, QttGrade::Omega));

// Atkey-specific identities.
static_assert(QttSemiring::add(QttGrade::One, QttGrade::One) == QttGrade::Omega,
    "QTT additive saturation: One+One must equal Omega (linearity loss).");
static_assert(QttSemiring::mul(QttGrade::Zero, QttGrade::Omega) == QttGrade::Zero,
    "QTT multiplicative absorption: Zero·anything = Zero.");
static_assert(QttSemiring::mul(QttGrade::One, QttGrade::Omega) == QttGrade::Omega,
    "QTT multiplicative identity: One·x = x.");

// Lattice ops are NOT the same as semiring ops.
static_assert(QttSemiring::join(QttGrade::One, QttGrade::One) == QttGrade::One,
    "Lattice join is idempotent: One ∨ One = One (chain max), unlike "
    "semiring add One+One = Omega.");

// Diagnostic names.
static_assert(QttSemiring::name() == "QttSemiring");
static_assert(QttSemiring::At<QttGrade::One>::name() == "QttSemiring::At<1>");
static_assert(qtt_grade_name(QttGrade::Zero)  == "0");
static_assert(qtt_grade_name(QttGrade::One)   == "1");

// Convenience aliases resolve to the right At<> instantiations.
static_assert(qtt::Erased::grade       == QttGrade::Zero);
static_assert(qtt::LinearGrade::grade  == QttGrade::One);
static_assert(qtt::Unrestricted::grade == QttGrade::Omega);

// ── Layout invariants on Graded<...,At<Grade>,T> ────────────────────
//
// Empty grade type (At<>'s element_type is empty) collapses via EBO;
// sizeof(Graded<...>) == sizeof(T) for non-empty T.
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using LinearGraded = Graded<ModalityKind::Absolute, qtt::LinearGrade, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearGraded, EightByteValue);

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Exercises QttSemiring's lattice + semiring ops AND
// Graded<..., At<One>, T>::weaken/compose with non-constant
// arguments.  Catches the consteval-vs-constexpr trap that pure
// static_assert tests miss (per
// feedback_algebra_runtime_smoke_test_discipline memory).
inline void runtime_smoke_test() {
    // Full QttSemiring at runtime.
    QttGrade a = QttGrade::Zero;
    QttGrade b = QttGrade::One;
    QttGrade c = QttGrade::Omega;
    [[maybe_unused]] bool       l1  = QttSemiring::leq(a, b);
    [[maybe_unused]] QttGrade   j1  = QttSemiring::join(b, c);
    [[maybe_unused]] QttGrade   m1  = QttSemiring::meet(b, c);
    [[maybe_unused]] QttGrade   ad1 = QttSemiring::add(b, b);   // One+One = Omega
    [[maybe_unused]] QttGrade   mu1 = QttSemiring::mul(c, c);   // Omega·Omega = Omega
    [[maybe_unused]] QttGrade   zr  = QttSemiring::zero();
    [[maybe_unused]] QttGrade   on  = QttSemiring::one();

    // Graded over At<LinearGrade> at runtime.
    OneByteValue v{42};
    LinearGraded<OneByteValue> initial{v, qtt::LinearGrade::bottom()};
    auto widened   = initial.weaken(qtt::LinearGrade::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(qtt::LinearGrade::top());
    auto rv_comp   = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto g2 = rv_widen.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
}

}  // namespace detail::qtt_self_test

}  // namespace crucible::algebra::lattices
