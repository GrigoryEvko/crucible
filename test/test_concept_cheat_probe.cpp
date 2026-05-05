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
#include <crucible/safety/IsLinear.h>

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

// Wrapper-detector inheritance probe: a class derived from Linear<int>
// inherits Linear's public surface, but must not satisfy is_linear_v.  The
// detector is exact-template-specialization based, not "has Linear's typedefs".
struct CheatLinear_Derived : ::crucible::safety::Linear<int> {
    using ::crucible::safety::Linear<int>::Linear;
};
static_assert(!::crucible::safety::extract::is_linear_v<CheatLinear_Derived>,
    "[IS_LINEAR DERIVED ADMITTED] is_linear_v must reject Linear-derived "
    "lookalikes; only exact Linear<T> specializations are Linear wrappers.");

// ── Round-5 cheats (deeper adversarial probe, 2026-04-27) ──────────
//
// Cheats 11, 12 expose the ARCHITECTURAL LIMIT of concept enforcement:
// any user who specializes the trait machinery in the originating
// namespace can bypass any concept clause that depends on that
// trait.  No purely-concept-level defense is possible against
// deliberate trait-spec injection.  Defense is CI grep enforcement
// via scripts/check-trait-injection.sh plus review discipline:
// `is_graded_specialization`, `value_type_decoupled`, `graded_modality`
// specializations OUTSIDE the algebra/safety/permissions tree are
// rejected outside canonical substrate code and this intentional fixture.
//
// Cheats 14-17 expose syntax-shape rejections (correctly fired by
// the strengthened concept).
//
// Cheat 18 admits a function-template forwarder with a default
// template parameter — defensibly admissible (the call resolves
// like a regular function), but documented for completeness.

// Cheat 11 fixture: a fake "substrate" type at global namespace scope
// so trait specializations in crucible::algebra can name it via `::`.
struct Cheat11_FakeSubstrate {
    using value_type = int;
    using lattice_type = crucible::algebra::lattices::QttSemiring::At<
        crucible::algebra::lattices::QttGrade::One>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "QttSemiring::At<1>"; }
};

// Cheat 12 fixture: the wrapper itself must be at global namespace
// scope so its `value_type_decoupled` specialization is reachable.
struct Cheat12_DecoupledOptOut {
    using value_type   = double;        // ← intentionally mismatched with substrate's int
    using lattice_type = crucible::algebra::lattices::QttSemiring::At<
        crucible::algebra::lattices::QttGrade::One>;
    using graded_type  = crucible::algebra::Graded<
        crucible::algebra::ModalityKind::Absolute,
        crucible::algebra::lattices::QttSemiring::At<
            crucible::algebra::lattices::QttGrade::One>,
        int>;
    static constexpr crucible::algebra::ModalityKind modality =
        crucible::algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

// Trait-spec injection — the load-bearing escape mechanism.
namespace crucible::algebra {
template <> struct is_graded_specialization<::Cheat11_FakeSubstrate> : std::true_type {};
template <> struct graded_modality<::Cheat11_FakeSubstrate>
    : std::integral_constant<ModalityKind, ModalityKind::Absolute> {};
template <> struct value_type_decoupled<::Cheat12_DecoupledOptOut> : std::true_type {};
}  // namespace crucible::algebra

// Cheat 11: trait-spec injection — fake substrate is admitted because
// the user specialized is_graded_specialization + graded_modality.
struct Cheat11_TraitInjection {
    using value_type   = int;
    using lattice_type = crucible::algebra::lattices::QttSemiring::At<
        crucible::algebra::lattices::QttGrade::One>;
    using graded_type  = ::Cheat11_FakeSubstrate;
    static constexpr crucible::algebra::ModalityKind modality =
        crucible::algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat11_admits = GradedWrapper<Cheat11_TraitInjection>;

// Cheat 12: value_type_decoupled trait-spec — Cheat12_DecoupledOptOut
// has value_type=double but graded_type::value_type=int; opt-out
// admits the mismatch.
static constexpr bool cheat12_admits = GradedWrapper<::Cheat12_DecoupledOptOut>;

// Cheat 14: const-qualified return type from forwarder (rejected — same_as strict)
struct Cheat14_ConstReturn {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    static consteval const std::string_view value_type_name() noexcept { return "int"; }
    static consteval const std::string_view lattice_name()    noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat14_admits = GradedWrapper<Cheat14_ConstReturn>;

// Cheat 15: deleted forwarder (rejected — call is ill-formed)
struct Cheat15_DeletedForwarder {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept = delete("nope");
    static consteval std::string_view lattice_name()    noexcept = delete("nope");
};
static constexpr bool cheat15_admits = GradedWrapper<Cheat15_DeletedForwarder>;

// Cheat 16: variable instead of function (rejected — function-call syntax fails)
struct Cheat16_VariableNotFunction {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    static constexpr std::string_view value_type_name = "int";
    static constexpr std::string_view lattice_name    = "QttSemiring::At<1>";
};
static constexpr bool cheat16_admits = GradedWrapper<Cheat16_VariableNotFunction>;

// Cheat 17: non-static member function (rejected — static call requires static)
struct Cheat17_NonStaticMember {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    consteval std::string_view value_type_name() const noexcept { return "int"; }
    consteval std::string_view lattice_name()    const noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat17_admits = GradedWrapper<Cheat17_NonStaticMember>;

// Cheat 18: function template forwarder with default template arg
// (admitted; defensible — call resolves like a regular function)
struct Cheat18_FunctionTemplateForwarder {
    using value_type   = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type  = Graded<ModalityKind::Absolute,
                                QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    template <int = 0>
    static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    template <int = 0>
    static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};
static constexpr bool cheat18_admits = GradedWrapper<Cheat18_FunctionTemplateForwarder>;

// ── Round-6 cheats — per-wrapper concept-gate attacks ───────────────
//
// Round-1..5 above target the GENERIC GradedWrapper concept.  Round-6
// extends the harness with adversarial cheats specific to the FOUND-G*
// wrappers (NumericalTier, DetSafe, HotPath, Vendor, ...) — each
// wrapper ships an Is<X> partial-specialization-driven DETECTION trait
// in `crucible::safety::extract` whose attack surface is DIFFERENT
// from GradedWrapper's.  Round-1..5 catch generic substrate/value-
// type/modality lies; Round-6 catches per-wrapper detection-trait
// lies (derived-class admittance, trait-spec injection on the
// per-wrapper detail trait).
//
// Per-wrapper convention (×17 wrappers × ≥2 cheats each ⇒ ≥34 probes
// when FOUND-E17 #826 fully closes):
//   - One LOCKED rejection — typically a derived-from-W cheat that
//     the partial-spec correctly refuses to match.  Catches future
//     regressions that would weaken the trait to family-match (e.g.,
//     a misguided "be permissive on subclasses" change).
//   - One DOCUMENTED architectural limit — trait-spec injection on
//     the detail::is_<x>_impl primary template.  Identical escape
//     mechanism to Cheats 11/12 against GradedWrapper; defended by
//     the same review discipline (grep guard on `is_*_impl<` outside
//     the safety/* tree).

#include <crucible/safety/IsNumericalTier.h>

// ── NumericalTier (FOUND-G01 / #826 batch 1/17) ─────────────────────

// Cheat 19: derived-from-NumericalTier (REJECTED — partial-spec
// matches the EXACT wrapper template `NumericalTier<T_at, U>`, not
// its derived classes; even though the derived type inherits the
// wrapper's typedefs and API, IsNumericalTier resolves the trait
// against the type's own identity, not its base.  If admitted, code
// that demands a NumericalTier-shaped argument would silently accept
// any subclass — defeating the wrapper's identity guarantee at
// boundaries (Substrate, dispatcher, mint factories per CLAUDE.md
// §XXI), since a subclass could carry arbitrary additional state /
// invariants the dispatcher does not anticipate).
struct Cheat19_DerivedFromNumericalTier
    : crucible::safety::NumericalTier<
          crucible::safety::Tolerance::BITEXACT, int> {};
static constexpr bool cheat19_admits =
    crucible::safety::extract::IsNumericalTier<
        Cheat19_DerivedFromNumericalTier>;

// Cheat 20 fixture — at global namespace scope so its specialization
// in crucible::safety::extract::detail can name it via `::`.
struct Cheat20_FakeViaTraitInjection {
    int payload{0};
};

// Trait-spec injection — the load-bearing escape mechanism (mirror
// of Cheats 11, 12 which inject into crucible::algebra:: traits).
// Specializing the per-wrapper detection trait in its originating
// namespace bypasses the partial-spec gate entirely.
namespace crucible::safety::extract::detail {
template <>
struct is_numerical_tier_impl<::Cheat20_FakeViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr ::crucible::safety::Tolerance tier =
        ::crucible::safety::Tolerance::BITEXACT;
    static constexpr bool has_tier = true;
};
}  // namespace crucible::safety::extract::detail

// Cheat 20: a non-NumericalTier passes IsNumericalTier because the
// user specialized the detection trait.  Same architectural-limit
// caveat as Cheats 11/12: no purely-concept-level defense exists
// against deliberate trait-spec injection in the originating
// namespace.  Defense is scripts/check-trait-injection.sh + review
// discipline: `is_numerical_tier_impl<` is rejected outside safety/*
// and this intentional fixture.
static constexpr bool cheat20_admits =
    crucible::safety::extract::IsNumericalTier<
        ::Cheat20_FakeViaTraitInjection>;

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

// ── Round-5 verdicts ────────────────────────────────────────────────
//
// Locked-in REJECTIONS (firing means a regression admitted the cheat):
static_assert(!cheat14_admits, "[CHEAT 14 ADMITTED] const-qualified return passed");
static_assert(!cheat15_admits, "[CHEAT 15 ADMITTED] deleted forwarder passed");
static_assert(!cheat16_admits, "[CHEAT 16 ADMITTED] variable not function passed");
static_assert(!cheat17_admits, "[CHEAT 17 ADMITTED] non-static member passed");

// Documented ARCHITECTURAL LIMITS (firing means concept improved —
// when this triggers, flip the assertion to !cheatN_admits to lock
// the new rejection in):
static_assert( cheat11_admits,
    "[CHEAT 11 STATUS CHANGED] trait-spec injection on is_graded_specialization "
    "+ graded_modality is now REJECTED — flip assertion to !cheat11_admits and "
    "document the new defense mechanism in the file's verdict-of-record block.");
static_assert( cheat12_admits,
    "[CHEAT 12 STATUS CHANGED] trait-spec on value_type_decoupled to escape "
    "CHEAT-1 is now REJECTED — flip assertion to !cheat12_admits.");
static_assert( cheat18_admits,
    "[CHEAT 18 STATUS CHANGED] function-template forwarder with default "
    "template arg is now REJECTED — flip assertion to !cheat18_admits if "
    "the rejection is intentional, or accept the admission as harmless.");

// ── Round-6 verdicts (per-wrapper concept-gate cheats) ─────────────
//
// NumericalTier (FOUND-G01, batch 1/17 of FOUND-E17 #826).
//
// Locked-in REJECTIONS — firing means a regression weakened
// IsNumericalTier from "exact-match partial-spec" to "family-match"
// (or worse).  The wrapper-detection trait MUST refuse derived
// classes; if a subclass is admitted, the subclass's added state
// flows undetected through every IsNumericalTier-gated boundary.
static_assert(!cheat19_admits,
    "[CHEAT 19 ADMITTED] derived-from-NumericalTier passed "
    "IsNumericalTier — partial-spec matched a subclass instead of "
    "only the exact wrapper template.  Substrate / dispatcher / mint "
    "factories now silently accept arbitrary subclasses with extra "
    "state, defeating the wrapper's identity guarantee.");

// Documented ARCHITECTURAL LIMITS — firing means the trait gained a
// defense (e.g., the impl trait moved to a private/inaccessible
// namespace, or the CI grep guard rejects this fixture too).
// When this triggers, flip the assertion to !cheatN_admits and document
// the new defense in this file's verdict-of-record block.
static_assert( cheat20_admits,
    "[CHEAT 20 STATUS CHANGED] trait-spec injection on "
    "is_numerical_tier_impl in crucible::safety::extract::detail is "
    "now REJECTED — flip assertion to !cheat20_admits and document "
    "the new defense (likely: relocate the impl trait to a "
    "review-protected namespace, OR tighten scripts/check-trait-injection.sh).");

int main() { return 0; }
