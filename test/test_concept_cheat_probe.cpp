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

// ── Round-6 expansion (fixy-A3-024) — 15 wrappers × 2 cheats ────────
//
// Mechanical expansion of the NumericalTier (Cheats 19/20) template
// across every other FOUND-G product wrapper.  Each wrapper gets:
//   - One LOCKED REJECTION (derived-from-W → !IsW): proves the
//     partial-spec gate matches the EXACT wrapper template, not
//     subclasses.  Regression catch: if anyone weakens IsW to
//     "family match" / "has W typedefs", these fixtures admit and
//     fail to compile.
//   - One DOCUMENTED ARCHITECTURAL LIMIT (specialize is_W_impl in
//     crucible::safety::extract::detail → IsW admits the fake):
//     identical escape mechanism to Cheats 11/12.  Defense is the
//     CI grep guard (scripts/check-trait-injection.sh) plus review
//     discipline; `is_*_impl<` outside the safety/* tree is
//     rejected.  When the rejection is gained, flip the assertion
//     to !cheatN_admits and document the new defense.
//
// Linear's inheritance probe (CheatLinear_Derived) is the original
// fixture above; Cheat 21 closes the pair with the trait-injection
// counterpart.  The full §XVI 16-wrapper canonical-order surface
// therefore ships 17 wrappers × 2 cheats = 34 probes by
// construction once this block compiles.

#include <crucible/safety/IsAllocClass.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/IsDetSafe.h>
#include <crucible/safety/IsHotPath.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/IsProgress.h>
#include <crucible/safety/IsRefined.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsSecret.h>
#include <crucible/safety/IsStale.h>
#include <crucible/safety/IsTagged.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/safety/IsWait.h>

// ── Linear (#XVI canonical 14) — closes the trait-injection pair ────
struct Cheat21_FakeLinearViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_linear_impl<::Cheat21_FakeLinearViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat21_admits =
    crucible::safety::extract::IsLinear<
        ::Cheat21_FakeLinearViaTraitInjection>;

// ── HotPath (FOUND-G02) ─────────────────────────────────────────────
struct Cheat22_DerivedFromHotPath
    : crucible::safety::HotPath<
          crucible::safety::HotPathTier_v::Hot, int> {};
static constexpr bool cheat22_admits =
    crucible::safety::extract::IsHotPath<Cheat22_DerivedFromHotPath>;

struct Cheat23_FakeHotPathViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_hot_path_impl<::Cheat23_FakeHotPathViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr ::crucible::safety::HotPathTier_v tier =
        ::crucible::safety::HotPathTier_v::Hot;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat23_admits =
    crucible::safety::extract::IsHotPath<
        ::Cheat23_FakeHotPathViaTraitInjection>;

// ── DetSafe (FOUND-G03) ─────────────────────────────────────────────
struct Cheat24_DerivedFromDetSafe
    : crucible::safety::DetSafe<
          crucible::safety::DetSafeTier_v::Pure, int> {};
static constexpr bool cheat24_admits =
    crucible::safety::extract::IsDetSafe<Cheat24_DerivedFromDetSafe>;

struct Cheat25_FakeDetSafeViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_det_safe_impl<::Cheat25_FakeDetSafeViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr ::crucible::safety::DetSafeTier_v tier =
        ::crucible::safety::DetSafeTier_v::Pure;
    static constexpr bool has_tier = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat25_admits =
    crucible::safety::extract::IsDetSafe<
        ::Cheat25_FakeDetSafeViaTraitInjection>;

// ── Refined (FOUND-G04) ─────────────────────────────────────────────
struct Cheat26_DerivedFromRefined
    : crucible::safety::Refined<positive_p, int> {
    using crucible::safety::Refined<positive_p, int>::Refined;
};
static constexpr bool cheat26_admits =
    crucible::safety::extract::IsRefined<Cheat26_DerivedFromRefined>;

struct Cheat27_FakeRefinedViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_refined_impl<::Cheat27_FakeRefinedViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    using predicate_type = decltype(positive_p);
    static constexpr bool sealed = false;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat27_admits =
    crucible::safety::extract::IsRefined<
        ::Cheat27_FakeRefinedViaTraitInjection>;

// ── Tagged (FOUND-G05) ──────────────────────────────────────────────
struct Cheat28_DerivedFromTagged
    : crucible::safety::Tagged<int, crucible::safety::source::FromUser> {
    using crucible::safety::Tagged<int,
        crucible::safety::source::FromUser>::Tagged;
};
static constexpr bool cheat28_admits =
    crucible::safety::extract::IsTagged<Cheat28_DerivedFromTagged>;

struct Cheat29_FakeTaggedViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_tagged_impl<::Cheat29_FakeTaggedViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    using tag_type = ::crucible::safety::source::FromUser;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat29_admits =
    crucible::safety::extract::IsTagged<
        ::Cheat29_FakeTaggedViaTraitInjection>;

// ── Secret (FOUND-G06) ──────────────────────────────────────────────
struct Cheat30_DerivedFromSecret
    : crucible::safety::Secret<int> {
    using crucible::safety::Secret<int>::Secret;
};
static constexpr bool cheat30_admits =
    crucible::safety::extract::IsSecret<Cheat30_DerivedFromSecret>;

struct Cheat31_FakeSecretViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_secret_impl<::Cheat31_FakeSecretViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat31_admits =
    crucible::safety::extract::IsSecret<
        ::Cheat31_FakeSecretViaTraitInjection>;

// ── Stale (FOUND-G07) ───────────────────────────────────────────────
struct Cheat32_DerivedFromStale
    : crucible::safety::Stale<int> {};
static constexpr bool cheat32_admits =
    crucible::safety::extract::IsStale<Cheat32_DerivedFromStale>;

struct Cheat33_FakeStaleViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_stale_impl<::Cheat33_FakeStaleViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat33_admits =
    crucible::safety::extract::IsStale<
        ::Cheat33_FakeStaleViaTraitInjection>;

// ── AllocClass (FOUND-G08) ──────────────────────────────────────────
struct Cheat34_DerivedFromAllocClass
    : crucible::safety::AllocClass<
          crucible::safety::AllocClassTag_v::Arena, int> {};
static constexpr bool cheat34_admits =
    crucible::safety::extract::IsAllocClass<Cheat34_DerivedFromAllocClass>;

struct Cheat35_FakeAllocClassViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_alloc_class_impl<::Cheat35_FakeAllocClassViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat35_admits =
    crucible::safety::extract::IsAllocClass<
        ::Cheat35_FakeAllocClassViaTraitInjection>;

// ── CipherTier (FOUND-G09) ──────────────────────────────────────────
struct Cheat36_DerivedFromCipherTier
    : crucible::safety::CipherTier<
          crucible::safety::CipherTierTag_v::Hot, int> {};
static constexpr bool cheat36_admits =
    crucible::safety::extract::IsCipherTier<Cheat36_DerivedFromCipherTier>;

struct Cheat37_FakeCipherTierViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_cipher_tier_impl<::Cheat37_FakeCipherTierViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr bool has_tag = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat37_admits =
    crucible::safety::extract::IsCipherTier<
        ::Cheat37_FakeCipherTierViaTraitInjection>;

// ── MemOrder (FOUND-G10) ────────────────────────────────────────────
struct Cheat38_DerivedFromMemOrder
    : crucible::safety::MemOrder<
          crucible::safety::MemOrderTag_v::AcqRel, int> {};
static constexpr bool cheat38_admits =
    crucible::safety::extract::IsMemOrder<Cheat38_DerivedFromMemOrder>;

struct Cheat39_FakeMemOrderViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_mem_order_impl<::Cheat39_FakeMemOrderViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat39_admits =
    crucible::safety::extract::IsMemOrder<
        ::Cheat39_FakeMemOrderViaTraitInjection>;

// ── Progress (FOUND-G11) ────────────────────────────────────────────
struct Cheat40_DerivedFromProgress
    : crucible::safety::Progress<
          crucible::safety::ProgressClass_v::Bounded, int> {};
static constexpr bool cheat40_admits =
    crucible::safety::extract::IsProgress<Cheat40_DerivedFromProgress>;

struct Cheat41_FakeProgressViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_progress_impl<::Cheat41_FakeProgressViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat41_admits =
    crucible::safety::extract::IsProgress<
        ::Cheat41_FakeProgressViaTraitInjection>;

// ── ResidencyHeat (FOUND-G12) ───────────────────────────────────────
struct Cheat42_DerivedFromResidencyHeat
    : crucible::safety::ResidencyHeat<
          crucible::safety::ResidencyHeatTag_v::Hot, int> {};
static constexpr bool cheat42_admits =
    crucible::safety::extract::IsResidencyHeat<
        Cheat42_DerivedFromResidencyHeat>;

struct Cheat43_FakeResidencyHeatViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_residency_heat_impl<::Cheat43_FakeResidencyHeatViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr bool has_tag = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat43_admits =
    crucible::safety::extract::IsResidencyHeat<
        ::Cheat43_FakeResidencyHeatViaTraitInjection>;

// ── Vendor (FOUND-G13) ──────────────────────────────────────────────
struct Cheat44_DerivedFromVendor
    : crucible::safety::Vendor<
          crucible::safety::VendorBackend_v::CPU, int> {};
static constexpr bool cheat44_admits =
    crucible::safety::extract::IsVendor<Cheat44_DerivedFromVendor>;

struct Cheat45_FakeVendorViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_vendor_impl<::Cheat45_FakeVendorViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr bool has_backend = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat45_admits =
    crucible::safety::extract::IsVendor<
        ::Cheat45_FakeVendorViaTraitInjection>;

// ── Wait (FOUND-G14) ────────────────────────────────────────────────
struct Cheat46_DerivedFromWait
    : crucible::safety::Wait<
          crucible::safety::WaitStrategy_v::SpinPause, int> {};
static constexpr bool cheat46_admits =
    crucible::safety::extract::IsWait<Cheat46_DerivedFromWait>;

struct Cheat47_FakeWaitViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_wait_impl<::Cheat47_FakeWaitViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat47_admits =
    crucible::safety::extract::IsWait<
        ::Cheat47_FakeWaitViaTraitInjection>;

// ── Crash (FOUND-G15) ───────────────────────────────────────────────
struct Cheat48_DerivedFromCrash
    : crucible::safety::Crash<
          crucible::safety::CrashClass_v::NoThrow, int> {};
static constexpr bool cheat48_admits =
    crucible::safety::extract::IsCrash<Cheat48_DerivedFromCrash>;

struct Cheat49_FakeCrashViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_crash_impl<::Cheat49_FakeCrashViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr bool has_class = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat49_admits =
    crucible::safety::extract::IsCrash<
        ::Cheat49_FakeCrashViaTraitInjection>;

// ── Consistency (FOUND-G16) ─────────────────────────────────────────
struct Cheat50_DerivedFromConsistency
    : crucible::safety::Consistency<
          crucible::safety::Consistency_v::STRONG, int> {};
static constexpr bool cheat50_admits =
    crucible::safety::extract::IsConsistency<
        Cheat50_DerivedFromConsistency>;

struct Cheat51_FakeConsistencyViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_consistency_impl<::Cheat51_FakeConsistencyViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr bool has_level = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat51_admits =
    crucible::safety::extract::IsConsistency<
        ::Cheat51_FakeConsistencyViaTraitInjection>;

// ── Verdicts (Round-6 expansion) ────────────────────────────────────
//
// Locked-in REJECTIONS — firing means a regression weakened the
// detector's partial-spec gate from "exact match" to "family match"
// (subclass or family member admitted).  Catches future PRs that
// accidentally permissivize the wrapper-detection trait — code that
// demands a W-shaped value would otherwise silently accept arbitrary
// subclasses carrying additional state.

static_assert(!cheat22_admits,
    "[CHEAT 22 ADMITTED] derived-from-HotPath passed IsHotPath — "
    "wrapper identity guarantee broken at Substrate/dispatcher/mint boundary.");
static_assert(!cheat24_admits,
    "[CHEAT 24 ADMITTED] derived-from-DetSafe passed IsDetSafe.");
static_assert(!cheat26_admits,
    "[CHEAT 26 ADMITTED] derived-from-Refined passed IsRefined.");
static_assert(!cheat28_admits,
    "[CHEAT 28 ADMITTED] derived-from-Tagged passed IsTagged.");
static_assert(!cheat30_admits,
    "[CHEAT 30 ADMITTED] derived-from-Secret passed IsSecret.");
static_assert(!cheat32_admits,
    "[CHEAT 32 ADMITTED] derived-from-Stale passed IsStale.");
static_assert(!cheat34_admits,
    "[CHEAT 34 ADMITTED] derived-from-AllocClass passed IsAllocClass.");
static_assert(!cheat36_admits,
    "[CHEAT 36 ADMITTED] derived-from-CipherTier passed IsCipherTier.");
static_assert(!cheat38_admits,
    "[CHEAT 38 ADMITTED] derived-from-MemOrder passed IsMemOrder.");
static_assert(!cheat40_admits,
    "[CHEAT 40 ADMITTED] derived-from-Progress passed IsProgress.");
static_assert(!cheat42_admits,
    "[CHEAT 42 ADMITTED] derived-from-ResidencyHeat passed IsResidencyHeat.");
static_assert(!cheat44_admits,
    "[CHEAT 44 ADMITTED] derived-from-Vendor passed IsVendor.");
static_assert(!cheat46_admits,
    "[CHEAT 46 ADMITTED] derived-from-Wait passed IsWait.");
static_assert(!cheat48_admits,
    "[CHEAT 48 ADMITTED] derived-from-Crash passed IsCrash.");
static_assert(!cheat50_admits,
    "[CHEAT 50 ADMITTED] derived-from-Consistency passed IsConsistency.");

// Documented ARCHITECTURAL LIMITS — firing means the detector
// gained a defense against trait-spec injection (impl trait moved
// to a private/inaccessible namespace, OR the CI grep guard rejects
// this fixture too).  When this triggers, flip the assertion to
// !cheatN_admits and document the new defense in the verdict block.
//
// Same architectural-limit caveat as Cheats 11, 12, 20: no purely-
// concept-level defense exists against deliberate trait-spec
// injection in the originating namespace.  Defense is
// scripts/check-trait-injection.sh + review discipline; this fixture
// is the canonical exception that the grep guard whitelists.

static_assert( cheat21_admits,
    "[CHEAT 21 STATUS CHANGED] trait-spec injection on is_linear_impl "
    "is now REJECTED — flip assertion to !cheat21_admits and document "
    "the new defense.");
static_assert( cheat23_admits,
    "[CHEAT 23 STATUS CHANGED] trait-spec injection on is_hot_path_impl "
    "is now REJECTED — flip assertion to !cheat23_admits.");
static_assert( cheat25_admits,
    "[CHEAT 25 STATUS CHANGED] trait-spec injection on is_det_safe_impl "
    "is now REJECTED — flip assertion to !cheat25_admits.");
static_assert( cheat27_admits,
    "[CHEAT 27 STATUS CHANGED] trait-spec injection on is_refined_impl "
    "is now REJECTED — flip assertion to !cheat27_admits.");
static_assert( cheat29_admits,
    "[CHEAT 29 STATUS CHANGED] trait-spec injection on is_tagged_impl "
    "is now REJECTED — flip assertion to !cheat29_admits.");
static_assert( cheat31_admits,
    "[CHEAT 31 STATUS CHANGED] trait-spec injection on is_secret_impl "
    "is now REJECTED — flip assertion to !cheat31_admits.");
static_assert( cheat33_admits,
    "[CHEAT 33 STATUS CHANGED] trait-spec injection on is_stale_impl "
    "is now REJECTED — flip assertion to !cheat33_admits.");
static_assert( cheat35_admits,
    "[CHEAT 35 STATUS CHANGED] trait-spec injection on is_alloc_class_impl "
    "is now REJECTED — flip assertion to !cheat35_admits.");
static_assert( cheat37_admits,
    "[CHEAT 37 STATUS CHANGED] trait-spec injection on is_cipher_tier_impl "
    "is now REJECTED — flip assertion to !cheat37_admits.");
static_assert( cheat39_admits,
    "[CHEAT 39 STATUS CHANGED] trait-spec injection on is_mem_order_impl "
    "is now REJECTED — flip assertion to !cheat39_admits.");
static_assert( cheat41_admits,
    "[CHEAT 41 STATUS CHANGED] trait-spec injection on is_progress_impl "
    "is now REJECTED — flip assertion to !cheat41_admits.");
static_assert( cheat43_admits,
    "[CHEAT 43 STATUS CHANGED] trait-spec injection on is_residency_heat_impl "
    "is now REJECTED — flip assertion to !cheat43_admits.");
static_assert( cheat45_admits,
    "[CHEAT 45 STATUS CHANGED] trait-spec injection on is_vendor_impl "
    "is now REJECTED — flip assertion to !cheat45_admits.");
static_assert( cheat47_admits,
    "[CHEAT 47 STATUS CHANGED] trait-spec injection on is_wait_impl "
    "is now REJECTED — flip assertion to !cheat47_admits.");
static_assert( cheat49_admits,
    "[CHEAT 49 STATUS CHANGED] trait-spec injection on is_crash_impl "
    "is now REJECTED — flip assertion to !cheat49_admits.");
static_assert( cheat51_admits,
    "[CHEAT 51 STATUS CHANGED] trait-spec injection on is_consistency_impl "
    "is now REJECTED — flip assertion to !cheat51_admits.");

// ── Round-7 expansion (FIXY-U-063) — 8 wrappers × 2 cheats ──────────
//
// Closes the per-wrapper trait-spec-injection probe coverage for the
// 8 remaining Graded-backed wrappers with dedicated IsX.h headers that
// Round-6 did not reach: Budgeted, EpochVersioned, NumaPlacement,
// OpaqueLifetime, RecipeSpec, Bits, Borrowed, OwnedRegion.  Identical
// shape to Round-6 (derived-from-W → !IsW locked rejection; trait-spec
// injection → IsW admitted as documented architectural limit).
//
// ── §XVI canonical wrappers WITHOUT per-wrapper trait-injection ─────
//
// Five §XVI canonical wrappers do NOT fit the Round-6/7 pattern
// because they have no dedicated IsX.h header / `is_<w>_impl` partial
// specialization to inject into:
//
//   - SealedRefined<Pred, T>     — detected via `is_refined_impl`
//                                  with `sealed = true`; shares
//                                  attack surface with Refined
//                                  (already covered by Cheats 26/27).
//   - Monotonic<T, Cmp>          — no dedicated trait; substrate
//                                  detection via GradedWrapper concept
//                                  (covered by Cheats 1-12 generically).
//   - AppendOnly<T, Storage>     — same as Monotonic.
//   - TimeOrdered<T, N, Tag>     — same as Monotonic.
//   - SharedPermission<Tag>      — façade detected via
//                                  `is_permission_impl` shared variant;
//                                  attack surface shared with Permission.
//
// These five wrappers are NOT abandoned — their attack surface is
// covered by the generic GradedWrapper-substrate cheats (Round-1..5,
// Cheats 1-12).  Adding per-wrapper Is traits for them would be a
// substrate-side prerequisite (5 new IsX.h headers); when those land,
// Round-8 mechanically extends Round-7's pattern.  Until then, the
// 25 wrappers with IsX.h (17 Round-6 + 8 Round-7) span the full
// per-wrapper trait-injection attack surface.
//
// Each pair is structurally distinct in its concept-gate path:
//   - The derived-from cheat exercises the partial-spec EXACT-match
//     refusal (subclass not matched).  Regression catch: weakening
//     IsW to "any class with W's typedefs" or "family match".
//   - The trait-injection cheat is the originating-namespace escape
//     hatch (same as Cheats 11/12/20-51).  Defense lives in
//     scripts/check-trait-injection.sh + review discipline.

#include <crucible/safety/IsBits.h>
#include <crucible/safety/IsBorrowed.h>
#include <crucible/safety/IsBudgeted.h>
#include <crucible/safety/IsEpochVersioned.h>
#include <crucible/safety/IsNumaPlacement.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsRecipeSpec.h>

// ── Budgeted (Tier-Space, off-tree) ────────────────────────────────
struct Cheat52_DerivedFromBudgeted
    : crucible::safety::Budgeted<int> {};
static constexpr bool cheat52_admits =
    crucible::safety::extract::IsBudgeted<Cheat52_DerivedFromBudgeted>;

struct Cheat53_FakeBudgetedViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_budgeted_impl<::Cheat53_FakeBudgetedViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat53_admits =
    crucible::safety::extract::IsBudgeted<
        ::Cheat53_FakeBudgetedViaTraitInjection>;

// ── EpochVersioned (Tier-V Version, off-tree) ───────────────────────
struct Cheat54_DerivedFromEpochVersioned
    : crucible::safety::EpochVersioned<int> {};
static constexpr bool cheat54_admits =
    crucible::safety::extract::IsEpochVersioned<
        Cheat54_DerivedFromEpochVersioned>;

struct Cheat55_FakeEpochVersionedViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_epoch_versioned_impl<
    ::Cheat55_FakeEpochVersionedViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat55_admits =
    crucible::safety::extract::IsEpochVersioned<
        ::Cheat55_FakeEpochVersionedViaTraitInjection>;

// ── NumaPlacement (Tier-L Representation, off-tree) ─────────────────
struct Cheat56_DerivedFromNumaPlacement
    : crucible::safety::NumaPlacement<int> {};
static constexpr bool cheat56_admits =
    crucible::safety::extract::IsNumaPlacement<
        Cheat56_DerivedFromNumaPlacement>;

struct Cheat57_FakeNumaPlacementViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_numa_placement_impl<
    ::Cheat57_FakeNumaPlacementViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat57_admits =
    crucible::safety::extract::IsNumaPlacement<
        ::Cheat57_FakeNumaPlacementViaTraitInjection>;

// ── OpaqueLifetime (Lifetime axis, off-tree) ────────────────────────
struct Cheat58_DerivedFromOpaqueLifetime
    : crucible::safety::OpaqueLifetime<
          crucible::safety::Lifetime_v::PER_REQUEST, int> {};
static constexpr bool cheat58_admits =
    crucible::safety::extract::IsOpaqueLifetime<
        Cheat58_DerivedFromOpaqueLifetime>;

struct Cheat59_FakeOpaqueLifetimeViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_opaque_lifetime_impl<
    ::Cheat59_FakeOpaqueLifetimeViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    static constexpr ::crucible::safety::Lifetime_v scope =
        ::crucible::safety::Lifetime_v::PER_REQUEST;
    static constexpr bool has_scope = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat59_admits =
    crucible::safety::extract::IsOpaqueLifetime<
        ::Cheat59_FakeOpaqueLifetimeViaTraitInjection>;

// ── RecipeSpec (Tier-Precision, off-tree) ───────────────────────────
struct Cheat60_DerivedFromRecipeSpec
    : crucible::safety::RecipeSpec<int> {};
static constexpr bool cheat60_admits =
    crucible::safety::extract::IsRecipeSpec<
        Cheat60_DerivedFromRecipeSpec>;

struct Cheat61_FakeRecipeSpecViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_recipe_spec_impl<::Cheat61_FakeRecipeSpecViaTraitInjection>
    : std::true_type
{
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat61_admits =
    crucible::safety::extract::IsRecipeSpec<
        ::Cheat61_FakeRecipeSpecViaTraitInjection>;

// ── Bits (typed bitset, scoped-enum carrier) ────────────────────────
enum class Cheat62EnumProbe : std::uint8_t {
    A = 1 << 0, B = 1 << 1, C = 1 << 2,
};
struct Cheat62_DerivedFromBits
    : crucible::safety::Bits<Cheat62EnumProbe> {};
static constexpr bool cheat62_admits =
    crucible::safety::extract::IsBits<Cheat62_DerivedFromBits>;

struct Cheat63_FakeBitsViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_bits_impl<::Cheat63_FakeBitsViaTraitInjection> : std::true_type {
    using value_type      = ::Cheat62EnumProbe;
    using underlying_type = std::underlying_type_t<::Cheat62EnumProbe>;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat63_admits =
    crucible::safety::extract::IsBits<
        ::Cheat63_FakeBitsViaTraitInjection>;

// ── Borrowed (lifetime-bound, two-template-arg wrapper) ─────────────
struct Cheat64_BorrowSource {};
struct Cheat64_DerivedFromBorrowed
    : crucible::safety::Borrowed<int, Cheat64_BorrowSource> {
    using crucible::safety::Borrowed<int, Cheat64_BorrowSource>::Borrowed;
};
static constexpr bool cheat64_admits =
    crucible::safety::extract::IsBorrowed<Cheat64_DerivedFromBorrowed>;

struct Cheat65_FakeBorrowedViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_borrowed_impl<::Cheat65_FakeBorrowedViaTraitInjection>
    : std::true_type
{
    using element_type = int;
    using source_type  = ::Cheat64_BorrowSource;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat65_admits =
    crucible::safety::extract::IsBorrowed<
        ::Cheat65_FakeBorrowedViaTraitInjection>;

// ── OwnedRegion (RAII region, two-template-arg wrapper) ─────────────
struct Cheat66_OwnedTag {};
struct Cheat66_DerivedFromOwnedRegion
    : crucible::safety::OwnedRegion<int, Cheat66_OwnedTag> {
    using crucible::safety::OwnedRegion<int, Cheat66_OwnedTag>::OwnedRegion;
};
static constexpr bool cheat66_admits =
    crucible::safety::extract::IsOwnedRegion<
        Cheat66_DerivedFromOwnedRegion>;

struct Cheat67_FakeOwnedRegionViaTraitInjection { int payload{0}; };
namespace crucible::safety::extract::detail {
template <>
struct is_owned_region_impl<
    ::Cheat67_FakeOwnedRegionViaTraitInjection>
    : std::true_type
{
    using value_type = int;
    using tag_type   = ::Cheat66_OwnedTag;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat67_admits =
    crucible::safety::extract::IsOwnedRegion<
        ::Cheat67_FakeOwnedRegionViaTraitInjection>;

// ── Round-7 verdicts ────────────────────────────────────────────────
//
// Locked-in REJECTIONS — same wrapper-identity rationale as Round-6.

static_assert(!cheat52_admits,
    "[CHEAT 52 ADMITTED] derived-from-Budgeted passed IsBudgeted.");
static_assert(!cheat54_admits,
    "[CHEAT 54 ADMITTED] derived-from-EpochVersioned passed IsEpochVersioned.");
static_assert(!cheat56_admits,
    "[CHEAT 56 ADMITTED] derived-from-NumaPlacement passed IsNumaPlacement.");
static_assert(!cheat58_admits,
    "[CHEAT 58 ADMITTED] derived-from-OpaqueLifetime passed IsOpaqueLifetime.");
static_assert(!cheat60_admits,
    "[CHEAT 60 ADMITTED] derived-from-RecipeSpec passed IsRecipeSpec.");
static_assert(!cheat62_admits,
    "[CHEAT 62 ADMITTED] derived-from-Bits passed IsBits.");
static_assert(!cheat64_admits,
    "[CHEAT 64 ADMITTED] derived-from-Borrowed passed IsBorrowed.");
static_assert(!cheat66_admits,
    "[CHEAT 66 ADMITTED] derived-from-OwnedRegion passed IsOwnedRegion.");

// Documented ARCHITECTURAL LIMITS — same trait-injection escape as
// Round-6.  When the rejection is gained (e.g., impl trait moved to
// inaccessible namespace, OR scripts/check-trait-injection.sh tightened),
// flip the assertion to !cheatN_admits and document the new defense.

static_assert( cheat53_admits,
    "[CHEAT 53 STATUS CHANGED] trait-spec injection on is_budgeted_impl "
    "is now REJECTED — flip assertion to !cheat53_admits.");
static_assert( cheat55_admits,
    "[CHEAT 55 STATUS CHANGED] trait-spec injection on is_epoch_versioned_impl "
    "is now REJECTED — flip assertion to !cheat55_admits.");
static_assert( cheat57_admits,
    "[CHEAT 57 STATUS CHANGED] trait-spec injection on is_numa_placement_impl "
    "is now REJECTED — flip assertion to !cheat57_admits.");
static_assert( cheat59_admits,
    "[CHEAT 59 STATUS CHANGED] trait-spec injection on is_opaque_lifetime_impl "
    "is now REJECTED — flip assertion to !cheat59_admits.");
static_assert( cheat61_admits,
    "[CHEAT 61 STATUS CHANGED] trait-spec injection on is_recipe_spec_impl "
    "is now REJECTED — flip assertion to !cheat61_admits.");
static_assert( cheat63_admits,
    "[CHEAT 63 STATUS CHANGED] trait-spec injection on is_bits_impl "
    "is now REJECTED — flip assertion to !cheat63_admits.");
static_assert( cheat65_admits,
    "[CHEAT 65 STATUS CHANGED] trait-spec injection on is_borrowed_impl "
    "is now REJECTED — flip assertion to !cheat65_admits.");
static_assert( cheat67_admits,
    "[CHEAT 67 STATUS CHANGED] trait-spec injection on is_owned_region_impl "
    "is now REJECTED — flip assertion to !cheat67_admits.");

int main() { return 0; }
