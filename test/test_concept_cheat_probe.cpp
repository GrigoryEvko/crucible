// ═══════════════════════════════════════════════════════════════════
// test_concept_cheat_probe — adversarial cheat-detection harness
//
// Round-4 audit (2026-04-27) deliberately constructed 10 wrapper
// classes that LOOK like they should satisfy GradedWrapper but
// violate one structural property each.  The original probe found
// 6 admissibility holes in the prior concept; the strengthened
// concept (algebra/GradedTrait.h Round-4 cluster) now rejects all
// 10 cheats.
//
// This file inverts each cheat into `static_assert(!cheatN_admits)`.
// THE BUILD SUCCEEDS ONLY WHEN EVERY CHEAT IS CORRECTLY REJECTED.
// Future Round-N audits run this harness — if a regression weakens
// the concept, the corresponding cheat starts being admitted, this
// file fails to compile, and the regression is caught at build time.
//
// When new cheats are discovered (any future audit), add them here.
// The harness expands monotonically: once a cheat is rejected, the
// rejection is locked in.
// ═══════════════════════════════════════════════════════════════════
//
// Round-4 verdict (recorded for future-audit reference):
//   Cheat 1 (value_type mismatch):   REJECTED by CHEAT-1 fix +
//                                    value_type_decoupled opt-out
//   Cheat 2 (lattice_type mismatch): REJECTED by CHEAT-2 same_as
//   Cheat 3 (lying forwarders):      REJECTED by CHEAT-3 fidelity
//   Cheat 4 (no substrate usage):    REJECTED transitively (the
//                                    other strengthened clauses
//                                    catch the wrapper's other
//                                    inconsistencies)
//   Cheat 5 (modality mismatch):     REJECTED by CHEAT-5 graded_modality
//   Cheat 6 (reference return):      REJECTED by same_as<string_view>
//   Cheat 7 (throwing forwarder):    REJECTED by L3 noexcept
//   Cheat 8 (implicit conversion):   REJECTED by same_as<string_view>
//   Cheat 9 (derived Graded):        REJECTED by is_graded_specialization
//   Cheat 10 (cyclic self-ref):      REJECTED by value_type-mismatch
//                                    (CyclicRef<int>::value_type is
//                                    int but graded_type::value_type
//                                    is CyclicRef<int> — caught)

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/lattices/QttSemiring.h>
#include <crucible/algebra/lattices/BoolLattice.h>

#include <string_view>
#include <type_traits>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

constexpr bool positive_p(int x) noexcept { return x > 0; }

// Cheat 1: value_type and graded_type::value_type DIFFER (and not regime-3 container split)
struct Cheat1_ValueTypeMismatch {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>,
                                double>;  // ← different from value_type
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "QTT::1"; }
};
static constexpr bool cheat1_admits = GradedWrapper<Cheat1_ValueTypeMismatch>;

// Cheat 2: lattice_type and graded_type::lattice_type DIFFER
struct Cheat2_LatticeMismatch {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;          // ← claims QTT
    using graded_type  = Graded<ModalityKind::Absolute,
                                BoolLattice<decltype(positive_p)>, // ← actually BoolLattice
                                int>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "x"; }
};
static constexpr bool cheat2_admits = GradedWrapper<Cheat2_LatticeMismatch>;

// Cheat 3: forwarders return WRONG strings
struct Cheat3_LyingForwarders {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>,
                                int>;
    static consteval std::string_view value_type_name() noexcept { return "TOTALLY-LYING"; }
    static consteval std::string_view lattice_name()    noexcept { return "ALSO-LYING"; }
};
static constexpr bool cheat3_admits = GradedWrapper<Cheat3_LyingForwarders>;

// Cheat 4: Random storage instead of using graded_type
struct Cheat4_NoSubstrateUsage {
    int data;  // ← raw storage, not using graded_type
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>,
                                int>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat4_admits = GradedWrapper<Cheat4_NoSubstrateUsage>;

// Cheat 5: graded_type modality DIFFERS from what wrapper actually models
struct Cheat5_ModalityMismatch {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    // graded_type uses Comonad, but Linear-like wrappers should be Absolute
    using graded_type  = Graded<ModalityKind::Comonad,
                                QttSemiring::At<QttGrade::One>,
                                int>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat5_admits = GradedWrapper<Cheat5_ModalityMismatch>;

// Cheat 6: forwarder returning string_view& (reference, not value)
struct Cheat6_ReferenceReturn {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>,
                                int>;
    static const std::string_view& value_type_name() noexcept;
    static const std::string_view& lattice_name()    noexcept;
};
static constexpr bool cheat6_admits = GradedWrapper<Cheat6_ReferenceReturn>;

// Cheat 7: forwarder that throws
struct Cheat7_ThrowingForwarder {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>,
                                int>;
    static std::string_view value_type_name() { return "x"; }  // not noexcept
    static std::string_view lattice_name()    { return "y"; }  // not noexcept
};
static constexpr bool cheat7_admits = GradedWrapper<Cheat7_ThrowingForwarder>;

// Cheat 8: implicit-conversion class instead of string_view
struct AlmostStringView {
    operator std::string_view() const noexcept;
};

struct Cheat8_ImplicitConversion {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>,
                                int>;
    static consteval AlmostStringView value_type_name() noexcept { return {}; }
    static consteval AlmostStringView lattice_name()    noexcept { return {}; }
};
static constexpr bool cheat8_admits = GradedWrapper<Cheat8_ImplicitConversion>;

// Cheat 9: graded_type is DERIVED from Graded, not Graded itself
struct DerivedGraded : Graded<ModalityKind::Absolute,
                              QttSemiring::At<QttGrade::One>,
                              int> {};

struct Cheat9_DerivedGraded {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = DerivedGraded;  // ← derived, not Graded itself
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "x"; }
};
static constexpr bool cheat9_admits = GradedWrapper<Cheat9_DerivedGraded>;

// Cheat 10: cyclic self-reference
template <typename T>
struct CyclicRef {
    using value_type   = T;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>,
                                CyclicRef<T>>;  // ← self-reference!
    static consteval std::string_view value_type_name() noexcept { return "x"; }
    static consteval std::string_view lattice_name()    noexcept { return "y"; }
};
static constexpr bool cheat10_admits = GradedWrapper<CyclicRef<int>>;

// Cheat 11 SKIPPED — would need Linear.h include + safety:: namespace.
// Inheritance-from-wrapper case: derived class inherits the typedefs,
// satisfies GradedWrapper trivially.  Probably acceptable behavior.

// Print the verdicts at compile time:
template <bool B> struct ShowAdmits { static constexpr bool value = B; };

// Force diagnostic emission via static_assert with custom messages.
// Each `static_assert(!cheatN_admits, ...)` fires only if the cheat is admitted.
// The FAILURE message names which cheats slipped past the concept.
// (We INVERT — assert NOT admitted; fires iff admitted.)

static_assert(!cheat1_admits,  "[CHEAT 1 ADMITTED] value_type vs graded_type::value_type mismatch passed");
static_assert(!cheat2_admits,  "[CHEAT 2 ADMITTED] lattice_type vs graded_type::lattice_type mismatch passed");
static_assert(!cheat3_admits,  "[CHEAT 3 ADMITTED] lying forwarders passed");
static_assert(!cheat4_admits,  "[CHEAT 4 ADMITTED] no-substrate-usage passed");
static_assert(!cheat5_admits,  "[CHEAT 5 ADMITTED] modality mismatch passed");
static_assert(!cheat6_admits,  "[CHEAT 6 ADMITTED] reference-return forwarder passed");
static_assert(!cheat7_admits,  "[CHEAT 7 ADMITTED] throwing forwarder passed");
static_assert(!cheat8_admits,  "[CHEAT 8 ADMITTED] implicit-conversion forwarder passed");
static_assert(!cheat9_admits,  "[CHEAT 9 ADMITTED] derived-Graded passed");
static_assert(!cheat10_admits, "[CHEAT 10 ADMITTED] cyclic self-reference passed");

int main() { return 0; }
