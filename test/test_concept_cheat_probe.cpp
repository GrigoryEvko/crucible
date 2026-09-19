// Adversarial cheat-detection harness.
//
// Every fixture below is a class built to look like it satisfies a
// wrapper concept while violating exactly one structural property.  Each
// one is then inverted into an assertion that it is NOT admitted, so the
// file compiles only while every cheat is still rejected.  A change that
// weakens a concept starts admitting its cheat and the build stops.
//
// The harness grows monotonically.  A new cheat is added, never removed,
// and a rejection once locked in stays locked in.
//
// Some cheats are asserted to be ADMITTED.  Those are the documented
// limits: a user who specializes the trait machinery inside the
// namespace that declares it can bypass any concept clause built on that
// trait, and no concept-level defense against that is possible.  The
// defense is a grep guard in CI plus review, and this file is the one
// sanctioned exception to it.  When such an assertion starts failing the
// limit has been closed, so invert the assertion to lock in the new
// rejection.
//
// A cheat asserted as admitted is not automatically a hole.  Where the
// admission is judged harmless the reason is stated at the fixture.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_GradedTrait.h>
#include <crucible/algebra/lattices/_QttSemiring.h>
#include <crucible/algebra/lattices/_BoolLattice.h>
#include <crucible/safety/IsLinear.h>

#include <string_view>
#include <type_traits>

using namespace crucible::algebra;
using namespace crucible::algebra::lattices;

constexpr bool positive_p(int x) noexcept { return x > 0; }

struct Cheat1_ValueTypeMismatch {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>,
                               double>;  // ← different from value_type
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QTT::1"; }
};
static constexpr bool cheat1_admits = GradedWrapper<Cheat1_ValueTypeMismatch>;

struct Cheat2_LatticeMismatch {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;  // ← claimed
    using graded_type = Graded<ModalityKind::Absolute,
                               BoolLattice<decltype(positive_p)>,  // ← actual
                               int>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "x"; }
};
static constexpr bool cheat2_admits = GradedWrapper<Cheat2_LatticeMismatch>;

struct Cheat3_LyingForwarders {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static consteval std::string_view value_type_name() noexcept { return "TOTALLY-LYING"; }
    static consteval std::string_view lattice_name() noexcept { return "ALSO-LYING"; }
};
static constexpr bool cheat3_admits = GradedWrapper<Cheat3_LyingForwarders>;

struct Cheat4_NoSubstrateUsage {
    int data;  // ← raw storage, not using graded_type
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat4_admits = GradedWrapper<Cheat4_NoSubstrateUsage>;

struct Cheat5_ModalityMismatch {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    // A linear-shaped wrapper models Absolute, not Comonad.
    using graded_type = Graded<ModalityKind::Comonad, QttSemiring::At<QttGrade::One>, int>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat5_admits = GradedWrapper<Cheat5_ModalityMismatch>;

struct Cheat6_ReferenceReturn {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static const std::string_view& value_type_name() noexcept;
    static const std::string_view& lattice_name() noexcept;
};
static constexpr bool cheat6_admits = GradedWrapper<Cheat6_ReferenceReturn>;

struct Cheat7_ThrowingForwarder {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static std::string_view value_type_name() { return "x"; }  // not noexcept
    static std::string_view lattice_name() { return "y"; }  // not noexcept
};
static constexpr bool cheat7_admits = GradedWrapper<Cheat7_ThrowingForwarder>;

struct AlmostStringView {
    operator std::string_view() const noexcept;
};

struct Cheat8_ImplicitConversion {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static consteval AlmostStringView value_type_name() noexcept { return {}; }
    static consteval AlmostStringView lattice_name() noexcept { return {}; }
};
static constexpr bool cheat8_admits = GradedWrapper<Cheat8_ImplicitConversion>;

struct DerivedGraded : Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int> {};

struct Cheat9_DerivedGraded {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = DerivedGraded;  // ← derived, not Graded itself
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "x"; }
};
static constexpr bool cheat9_admits = GradedWrapper<Cheat9_DerivedGraded>;

template <typename T>
struct CyclicRef {
    using value_type = T;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>,
                               CyclicRef<T>>;  // ← self-reference
    static consteval std::string_view value_type_name() noexcept { return "x"; }
    static consteval std::string_view lattice_name() noexcept { return "y"; }
};
static constexpr bool cheat10_admits = GradedWrapper<CyclicRef<int>>;

// A class derived from Linear inherits its whole public surface, so a
// detector that matched on shape would admit it.  The detector matches
// the exact specialization instead, and must refuse this.
struct CheatLinear_Derived : ::crucible::safety::Linear<int> {
    using ::crucible::safety::Linear<int>::Linear;
};
static_assert(!::crucible::safety::extract::is_linear_v<CheatLinear_Derived>,
              "[IS_LINEAR DERIVED ADMITTED] is_linear_v must reject Linear-derived "
              "lookalikes; only exact Linear<T> specializations are Linear wrappers.");

// The two fixtures below sit at global namespace scope so that the
// trait specializations further down can name them through `::` from
// inside the namespace that owns the trait.
struct Cheat11_FakeSubstrate {
    using value_type = int;
    using lattice_type = crucible::algebra::lattices::QttSemiring::At<crucible::algebra::lattices::QttGrade::One>;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};

struct Cheat12_DecoupledOptOut {
    using value_type = double;  // ← mismatches the substrate's int
    using lattice_type = crucible::algebra::lattices::QttSemiring::At<crucible::algebra::lattices::QttGrade::One>;
    using graded_type = crucible::algebra::Graded<
        crucible::algebra::ModalityKind::Absolute,
        crucible::algebra::lattices::QttSemiring::At<crucible::algebra::lattices::QttGrade::One>, int>;
    static constexpr crucible::algebra::ModalityKind modality = crucible::algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

// Trait-spec injection: the escape hatch that no concept can close.
namespace crucible::algebra {
template <>
struct is_graded_specialization<::Cheat11_FakeSubstrate> : std::true_type {};
template <>
struct graded_modality<::Cheat11_FakeSubstrate> : std::integral_constant<ModalityKind, ModalityKind::Absolute> {};
template <>
struct value_type_decoupled<::Cheat12_DecoupledOptOut> : std::true_type {};
}  // namespace crucible::algebra

struct Cheat11_TraitInjection {
    using value_type = int;
    using lattice_type = crucible::algebra::lattices::QttSemiring::At<crucible::algebra::lattices::QttGrade::One>;
    using graded_type = ::Cheat11_FakeSubstrate;
    static constexpr crucible::algebra::ModalityKind modality = crucible::algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat11_admits = GradedWrapper<Cheat11_TraitInjection>;

static constexpr bool cheat12_admits = GradedWrapper<::Cheat12_DecoupledOptOut>;

struct Cheat14_ConstReturn {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    static consteval const std::string_view value_type_name() noexcept { return "int"; }
    static consteval const std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat14_admits = GradedWrapper<Cheat14_ConstReturn>;

struct Cheat15_DeletedForwarder {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept = delete("nope");
    static consteval std::string_view lattice_name() noexcept = delete("nope");
};
static constexpr bool cheat15_admits = GradedWrapper<Cheat15_DeletedForwarder>;

struct Cheat16_VariableNotFunction {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    static constexpr std::string_view value_type_name = "int";
    static constexpr std::string_view lattice_name = "QttSemiring::At<1>";
};
static constexpr bool cheat16_admits = GradedWrapper<Cheat16_VariableNotFunction>;

struct Cheat17_NonStaticMember {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
    static constexpr ModalityKind modality = ModalityKind::Absolute;
    consteval std::string_view value_type_name() const noexcept { return "int"; }
    consteval std::string_view lattice_name() const noexcept { return "QttSemiring::At<1>"; }
};
static constexpr bool cheat17_admits = GradedWrapper<Cheat17_NonStaticMember>;

// Admitted, and defensibly so: a function template with a defaulted
// parameter is called exactly like a plain function, and it forwards
// honestly.  The admission is recorded rather than treated as a hole.
struct Cheat18_FunctionTemplateForwarder {
    using value_type = int;
    using lattice_type = QttSemiring::At<QttGrade::One>;
    using graded_type = Graded<ModalityKind::Absolute, QttSemiring::At<QttGrade::One>, int>;
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

// The cheats above attack the generic wrapper concept.  Everything from
// here on attacks the per-wrapper detection traits, which have a
// different surface, and each wrapper gets the same pair:
//
//   - a class derived from the wrapper, which must be REJECTED.  The
//     detection trait matches the exact wrapper template, so a subclass
//     does not match it.  Were it to match, a boundary that demands a
//     wrapper-shaped argument would silently accept any subclass, along
//     with whatever extra state and invariants that subclass carries.
//   - an unrelated class with the detection trait specialized for it,
//     which is ADMITTED.  That is the documented limit described at the
//     top of this file.

#include <crucible/safety/_IsNumericalTier.h>

struct Cheat19_DerivedFromNumericalTier : crucible::safety::NumericalTier<crucible::safety::Tolerance::BITEXACT, int> {
};
static constexpr bool cheat19_admits = crucible::safety::extract::IsNumericalTier<Cheat19_DerivedFromNumericalTier>;

// Global namespace scope again, so that the specialization below can
// name it through `::`.
struct Cheat20_FakeViaTraitInjection {
    int payload{0};
};

namespace crucible::safety::extract::detail {
template <>
struct is_numerical_tier_impl<::Cheat20_FakeViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr ::crucible::safety::Tolerance tier = ::crucible::safety::Tolerance::BITEXACT;
    static constexpr bool has_tier = true;
};
}  // namespace crucible::safety::extract::detail

static constexpr bool cheat20_admits = crucible::safety::extract::IsNumericalTier<::Cheat20_FakeViaTraitInjection>;

template <bool B>
struct ShowAdmits {
    static constexpr bool value = B;
};

static_assert(!cheat1_admits, "[CHEAT 1 ADMITTED] value_type vs graded_type::value_type mismatch passed");
static_assert(!cheat2_admits, "[CHEAT 2 ADMITTED] lattice_type vs graded_type::lattice_type mismatch passed");
static_assert(!cheat3_admits, "[CHEAT 3 ADMITTED] lying forwarders passed");
static_assert(!cheat4_admits, "[CHEAT 4 ADMITTED] no-substrate-usage passed");
static_assert(!cheat5_admits, "[CHEAT 5 ADMITTED] modality mismatch passed");
static_assert(!cheat6_admits, "[CHEAT 6 ADMITTED] reference-return forwarder passed");
static_assert(!cheat7_admits, "[CHEAT 7 ADMITTED] throwing forwarder passed");
static_assert(!cheat8_admits, "[CHEAT 8 ADMITTED] implicit-conversion forwarder passed");
static_assert(!cheat9_admits, "[CHEAT 9 ADMITTED] derived-Graded passed");
static_assert(!cheat10_admits, "[CHEAT 10 ADMITTED] cyclic self-reference passed");

static_assert(!cheat14_admits, "[CHEAT 14 ADMITTED] const-qualified return passed");
static_assert(!cheat15_admits, "[CHEAT 15 ADMITTED] deleted forwarder passed");
static_assert(!cheat16_admits, "[CHEAT 16 ADMITTED] variable not function passed");
static_assert(!cheat17_admits, "[CHEAT 17 ADMITTED] non-static member passed");

static_assert(cheat11_admits, "[CHEAT 11 STATUS CHANGED] trait-spec injection on is_graded_specialization "
                              "+ graded_modality is now REJECTED — flip assertion to !cheat11_admits and "
                              "document the new defense mechanism at the fixture.");
static_assert(cheat12_admits, "[CHEAT 12 STATUS CHANGED] trait-spec on value_type_decoupled to escape "
                              "CHEAT-1 is now REJECTED — flip assertion to !cheat12_admits.");
static_assert(cheat18_admits, "[CHEAT 18 STATUS CHANGED] function-template forwarder with default "
                              "template arg is now REJECTED — flip assertion to !cheat18_admits if "
                              "the rejection is intentional, or accept the admission as harmless.");

static_assert(!cheat19_admits, "[CHEAT 19 ADMITTED] derived-from-NumericalTier passed "
                               "IsNumericalTier — partial-spec matched a subclass instead of "
                               "only the exact wrapper template.  Substrate / dispatcher / mint "
                               "factories now silently accept arbitrary subclasses with extra "
                               "state, defeating the wrapper's identity guarantee.");

static_assert(cheat20_admits, "[CHEAT 20 STATUS CHANGED] trait-spec injection on "
                              "is_numerical_tier_impl in crucible::safety::extract::detail is "
                              "now REJECTED — flip assertion to !cheat20_admits and document "
                              "the new defense (likely: relocate the impl trait to a "
                              "review-protected namespace, or tighten the CI grep guard).");

// Linear already has its derived-from cheat above.  Cheat 21 completes
// the pair for it, and every wrapper after that gets both.

#include <crucible/safety/_IsAllocClass.h>
#include <crucible/safety/_IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/_IsDetSafe.h>
#include <crucible/safety/_IsHotPath.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/IsProgress.h>
#include <crucible/safety/_IsRefined.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/_IsSecret.h>
#include <crucible/safety/_IsStale.h>
#include <crucible/safety/_IsTagged.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/safety/IsWait.h>

struct Cheat21_FakeLinearViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_linear_impl<::Cheat21_FakeLinearViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat21_admits = crucible::safety::extract::IsLinear<::Cheat21_FakeLinearViaTraitInjection>;

struct Cheat22_DerivedFromHotPath : crucible::safety::HotPath<crucible::safety::HotPathTier_v::Hot, int> {};
static constexpr bool cheat22_admits = crucible::safety::extract::IsHotPath<Cheat22_DerivedFromHotPath>;

struct Cheat23_FakeHotPathViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_hot_path_impl<::Cheat23_FakeHotPathViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr ::crucible::safety::HotPathTier_v tier = ::crucible::safety::HotPathTier_v::Hot;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat23_admits = crucible::safety::extract::IsHotPath<::Cheat23_FakeHotPathViaTraitInjection>;

struct Cheat24_DerivedFromDetSafe : crucible::safety::DetSafe<crucible::safety::DetSafeTier_v::Pure, int> {};
static constexpr bool cheat24_admits = crucible::safety::extract::IsDetSafe<Cheat24_DerivedFromDetSafe>;

struct Cheat25_FakeDetSafeViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_det_safe_impl<::Cheat25_FakeDetSafeViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr ::crucible::safety::DetSafeTier_v tier = ::crucible::safety::DetSafeTier_v::Pure;
    static constexpr bool has_tier = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat25_admits = crucible::safety::extract::IsDetSafe<::Cheat25_FakeDetSafeViaTraitInjection>;

struct Cheat26_DerivedFromRefined : crucible::safety::Refined<positive_p, int> {
    using crucible::safety::Refined<positive_p, int>::Refined;
};
static constexpr bool cheat26_admits = crucible::safety::extract::IsRefined<Cheat26_DerivedFromRefined>;

struct Cheat27_FakeRefinedViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_refined_impl<::Cheat27_FakeRefinedViaTraitInjection> : std::true_type {
    using value_type = int;
    using predicate_type = decltype(positive_p);
    static constexpr bool sealed = false;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat27_admits = crucible::safety::extract::IsRefined<::Cheat27_FakeRefinedViaTraitInjection>;

struct Cheat28_DerivedFromTagged : crucible::safety::Tagged<int, crucible::safety::source::FromUser> {
    using crucible::safety::Tagged<int, crucible::safety::source::FromUser>::Tagged;
};
static constexpr bool cheat28_admits = crucible::safety::extract::IsTagged<Cheat28_DerivedFromTagged>;

struct Cheat29_FakeTaggedViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_tagged_impl<::Cheat29_FakeTaggedViaTraitInjection> : std::true_type {
    using value_type = int;
    using tag_type = ::crucible::safety::source::FromUser;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat29_admits = crucible::safety::extract::IsTagged<::Cheat29_FakeTaggedViaTraitInjection>;

struct Cheat30_DerivedFromSecret : crucible::safety::Secret<int> {
    using crucible::safety::Secret<int>::Secret;
};
static constexpr bool cheat30_admits = crucible::safety::extract::IsSecret<Cheat30_DerivedFromSecret>;

struct Cheat31_FakeSecretViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_secret_impl<::Cheat31_FakeSecretViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat31_admits = crucible::safety::extract::IsSecret<::Cheat31_FakeSecretViaTraitInjection>;

struct Cheat32_DerivedFromStale : crucible::safety::Stale<int> {};
static constexpr bool cheat32_admits = crucible::safety::extract::IsStale<Cheat32_DerivedFromStale>;

struct Cheat33_FakeStaleViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_stale_impl<::Cheat33_FakeStaleViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat33_admits = crucible::safety::extract::IsStale<::Cheat33_FakeStaleViaTraitInjection>;

struct Cheat34_DerivedFromAllocClass : crucible::safety::AllocClass<crucible::safety::AllocClassTag_v::Arena, int> {};
static constexpr bool cheat34_admits = crucible::safety::extract::IsAllocClass<Cheat34_DerivedFromAllocClass>;

struct Cheat35_FakeAllocClassViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_alloc_class_impl<::Cheat35_FakeAllocClassViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat35_admits =
    crucible::safety::extract::IsAllocClass<::Cheat35_FakeAllocClassViaTraitInjection>;

struct Cheat36_DerivedFromCipherTier : crucible::safety::CipherTier<crucible::safety::CipherTierTag_v::Hot, int> {};
static constexpr bool cheat36_admits = crucible::safety::extract::IsCipherTier<Cheat36_DerivedFromCipherTier>;

struct Cheat37_FakeCipherTierViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_cipher_tier_impl<::Cheat37_FakeCipherTierViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr bool has_tag = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat37_admits =
    crucible::safety::extract::IsCipherTier<::Cheat37_FakeCipherTierViaTraitInjection>;

struct Cheat38_DerivedFromMemOrder : crucible::safety::MemOrder<crucible::safety::MemOrderTag_v::AcqRel, int> {};
static constexpr bool cheat38_admits = crucible::safety::extract::IsMemOrder<Cheat38_DerivedFromMemOrder>;

struct Cheat39_FakeMemOrderViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_mem_order_impl<::Cheat39_FakeMemOrderViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat39_admits = crucible::safety::extract::IsMemOrder<::Cheat39_FakeMemOrderViaTraitInjection>;

struct Cheat40_DerivedFromProgress : crucible::safety::Progress<crucible::safety::ProgressClass_v::Bounded, int> {};
static constexpr bool cheat40_admits = crucible::safety::extract::IsProgress<Cheat40_DerivedFromProgress>;

struct Cheat41_FakeProgressViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_progress_impl<::Cheat41_FakeProgressViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat41_admits = crucible::safety::extract::IsProgress<::Cheat41_FakeProgressViaTraitInjection>;

struct Cheat42_DerivedFromResidencyHeat
    : crucible::safety::ResidencyHeat<crucible::safety::ResidencyHeatTag_v::Hot, int> {};
static constexpr bool cheat42_admits = crucible::safety::extract::IsResidencyHeat<Cheat42_DerivedFromResidencyHeat>;

struct Cheat43_FakeResidencyHeatViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_residency_heat_impl<::Cheat43_FakeResidencyHeatViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr bool has_tag = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat43_admits =
    crucible::safety::extract::IsResidencyHeat<::Cheat43_FakeResidencyHeatViaTraitInjection>;

struct Cheat44_DerivedFromVendor : crucible::safety::Vendor<crucible::safety::VendorBackend_v::CPU, int> {};
static constexpr bool cheat44_admits = crucible::safety::extract::IsVendor<Cheat44_DerivedFromVendor>;

struct Cheat45_FakeVendorViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_vendor_impl<::Cheat45_FakeVendorViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr bool has_backend = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat45_admits = crucible::safety::extract::IsVendor<::Cheat45_FakeVendorViaTraitInjection>;

struct Cheat46_DerivedFromWait : crucible::safety::Wait<crucible::safety::WaitStrategy_v::SpinPause, int> {};
static constexpr bool cheat46_admits = crucible::safety::extract::IsWait<Cheat46_DerivedFromWait>;

struct Cheat47_FakeWaitViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_wait_impl<::Cheat47_FakeWaitViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat47_admits = crucible::safety::extract::IsWait<::Cheat47_FakeWaitViaTraitInjection>;

struct Cheat48_DerivedFromCrash : crucible::safety::Crash<crucible::safety::CrashClass_v::NoThrow, int> {};
static constexpr bool cheat48_admits = crucible::safety::extract::IsCrash<Cheat48_DerivedFromCrash>;

struct Cheat49_FakeCrashViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_crash_impl<::Cheat49_FakeCrashViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr bool has_class = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat49_admits = crucible::safety::extract::IsCrash<::Cheat49_FakeCrashViaTraitInjection>;

struct Cheat50_DerivedFromConsistency : crucible::safety::Consistency<crucible::safety::Consistency_v::STRONG, int> {};
static constexpr bool cheat50_admits = crucible::safety::extract::IsConsistency<Cheat50_DerivedFromConsistency>;

struct Cheat51_FakeConsistencyViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_consistency_impl<::Cheat51_FakeConsistencyViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr bool has_level = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat51_admits =
    crucible::safety::extract::IsConsistency<::Cheat51_FakeConsistencyViaTraitInjection>;

static_assert(!cheat22_admits, "[CHEAT 22 ADMITTED] derived-from-HotPath passed IsHotPath — "
                               "wrapper identity guarantee broken at Substrate/dispatcher/mint boundary.");
static_assert(!cheat24_admits, "[CHEAT 24 ADMITTED] derived-from-DetSafe passed IsDetSafe.");
static_assert(!cheat26_admits, "[CHEAT 26 ADMITTED] derived-from-Refined passed IsRefined.");
static_assert(!cheat28_admits, "[CHEAT 28 ADMITTED] derived-from-Tagged passed IsTagged.");
static_assert(!cheat30_admits, "[CHEAT 30 ADMITTED] derived-from-Secret passed IsSecret.");
static_assert(!cheat32_admits, "[CHEAT 32 ADMITTED] derived-from-Stale passed IsStale.");
static_assert(!cheat34_admits, "[CHEAT 34 ADMITTED] derived-from-AllocClass passed IsAllocClass.");
static_assert(!cheat36_admits, "[CHEAT 36 ADMITTED] derived-from-CipherTier passed IsCipherTier.");
static_assert(!cheat38_admits, "[CHEAT 38 ADMITTED] derived-from-MemOrder passed IsMemOrder.");
static_assert(!cheat40_admits, "[CHEAT 40 ADMITTED] derived-from-Progress passed IsProgress.");
static_assert(!cheat42_admits, "[CHEAT 42 ADMITTED] derived-from-ResidencyHeat passed IsResidencyHeat.");
static_assert(!cheat44_admits, "[CHEAT 44 ADMITTED] derived-from-Vendor passed IsVendor.");
static_assert(!cheat46_admits, "[CHEAT 46 ADMITTED] derived-from-Wait passed IsWait.");
static_assert(!cheat48_admits, "[CHEAT 48 ADMITTED] derived-from-Crash passed IsCrash.");
static_assert(!cheat50_admits, "[CHEAT 50 ADMITTED] derived-from-Consistency passed IsConsistency.");

static_assert(cheat21_admits, "[CHEAT 21 STATUS CHANGED] trait-spec injection on is_linear_impl "
                              "is now REJECTED — flip assertion to !cheat21_admits and document "
                              "the new defense.");
static_assert(cheat23_admits, "[CHEAT 23 STATUS CHANGED] trait-spec injection on is_hot_path_impl "
                              "is now REJECTED — flip assertion to !cheat23_admits.");
static_assert(cheat25_admits, "[CHEAT 25 STATUS CHANGED] trait-spec injection on is_det_safe_impl "
                              "is now REJECTED — flip assertion to !cheat25_admits.");
static_assert(cheat27_admits, "[CHEAT 27 STATUS CHANGED] trait-spec injection on is_refined_impl "
                              "is now REJECTED — flip assertion to !cheat27_admits.");
static_assert(cheat29_admits, "[CHEAT 29 STATUS CHANGED] trait-spec injection on is_tagged_impl "
                              "is now REJECTED — flip assertion to !cheat29_admits.");
static_assert(cheat31_admits, "[CHEAT 31 STATUS CHANGED] trait-spec injection on is_secret_impl "
                              "is now REJECTED — flip assertion to !cheat31_admits.");
static_assert(cheat33_admits, "[CHEAT 33 STATUS CHANGED] trait-spec injection on is_stale_impl "
                              "is now REJECTED — flip assertion to !cheat33_admits.");
static_assert(cheat35_admits, "[CHEAT 35 STATUS CHANGED] trait-spec injection on is_alloc_class_impl "
                              "is now REJECTED — flip assertion to !cheat35_admits.");
static_assert(cheat37_admits, "[CHEAT 37 STATUS CHANGED] trait-spec injection on is_cipher_tier_impl "
                              "is now REJECTED — flip assertion to !cheat37_admits.");
static_assert(cheat39_admits, "[CHEAT 39 STATUS CHANGED] trait-spec injection on is_mem_order_impl "
                              "is now REJECTED — flip assertion to !cheat39_admits.");
static_assert(cheat41_admits, "[CHEAT 41 STATUS CHANGED] trait-spec injection on is_progress_impl "
                              "is now REJECTED — flip assertion to !cheat41_admits.");
static_assert(cheat43_admits, "[CHEAT 43 STATUS CHANGED] trait-spec injection on is_residency_heat_impl "
                              "is now REJECTED — flip assertion to !cheat43_admits.");
static_assert(cheat45_admits, "[CHEAT 45 STATUS CHANGED] trait-spec injection on is_vendor_impl "
                              "is now REJECTED — flip assertion to !cheat45_admits.");
static_assert(cheat47_admits, "[CHEAT 47 STATUS CHANGED] trait-spec injection on is_wait_impl "
                              "is now REJECTED — flip assertion to !cheat47_admits.");
static_assert(cheat49_admits, "[CHEAT 49 STATUS CHANGED] trait-spec injection on is_crash_impl "
                              "is now REJECTED — flip assertion to !cheat49_admits.");
static_assert(cheat51_admits, "[CHEAT 51 STATUS CHANGED] trait-spec injection on is_consistency_impl "
                              "is now REJECTED — flip assertion to !cheat51_admits.");

// Five canonical wrappers have no cheat pair here, because they have no
// per-wrapper detection trait to attack:
//
//   - SealedRefined shares the refined detection trait, and so shares
//     the attack surface already covered above.
//   - SharedPermission likewise shares the permission trait.
//   - Monotonic, AppendOnly and TimeOrdered have no dedicated trait at
//     all, and are reached through the generic wrapper concept, which
//     the first block of cheats covers.
//
// Their absence is a consequence of the substrate, not a gap in the
// harness.  If any of them gains a detection trait, it gains a pair
// here on the same pattern as the rest.

#include <crucible/safety/IsBits.h>
#include <crucible/safety/_IsBorrowed.h>
#include <crucible/safety/IsBudgeted.h>
#include <crucible/safety/IsEpochVersioned.h>
#include <crucible/safety/IsNumaPlacement.h>
#include <crucible/safety/_IsOpaqueLifetime.h>
#include <crucible/safety/_IsOwnedRegion.h>
#include <crucible/safety/_IsRecipeSpec.h>

struct Cheat52_DerivedFromBudgeted : crucible::safety::Budgeted<int> {};
static constexpr bool cheat52_admits = crucible::safety::extract::IsBudgeted<Cheat52_DerivedFromBudgeted>;

struct Cheat53_FakeBudgetedViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_budgeted_impl<::Cheat53_FakeBudgetedViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat53_admits = crucible::safety::extract::IsBudgeted<::Cheat53_FakeBudgetedViaTraitInjection>;

struct Cheat54_DerivedFromEpochVersioned : crucible::safety::EpochVersioned<int> {};
static constexpr bool cheat54_admits = crucible::safety::extract::IsEpochVersioned<Cheat54_DerivedFromEpochVersioned>;

struct Cheat55_FakeEpochVersionedViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_epoch_versioned_impl<::Cheat55_FakeEpochVersionedViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat55_admits =
    crucible::safety::extract::IsEpochVersioned<::Cheat55_FakeEpochVersionedViaTraitInjection>;

struct Cheat56_DerivedFromNumaPlacement : crucible::safety::NumaPlacement<int> {};
static constexpr bool cheat56_admits = crucible::safety::extract::IsNumaPlacement<Cheat56_DerivedFromNumaPlacement>;

struct Cheat57_FakeNumaPlacementViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_numa_placement_impl<::Cheat57_FakeNumaPlacementViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat57_admits =
    crucible::safety::extract::IsNumaPlacement<::Cheat57_FakeNumaPlacementViaTraitInjection>;

struct Cheat58_DerivedFromOpaqueLifetime
    : crucible::safety::OpaqueLifetime<crucible::safety::Lifetime_v::PER_REQUEST, int> {};
static constexpr bool cheat58_admits = crucible::safety::extract::IsOpaqueLifetime<Cheat58_DerivedFromOpaqueLifetime>;

struct Cheat59_FakeOpaqueLifetimeViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_opaque_lifetime_impl<::Cheat59_FakeOpaqueLifetimeViaTraitInjection> : std::true_type {
    using value_type = int;
    static constexpr ::crucible::safety::Lifetime_v scope = ::crucible::safety::Lifetime_v::PER_REQUEST;
    static constexpr bool has_scope = true;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat59_admits =
    crucible::safety::extract::IsOpaqueLifetime<::Cheat59_FakeOpaqueLifetimeViaTraitInjection>;

struct Cheat60_DerivedFromRecipeSpec : crucible::safety::RecipeSpec<int> {};
static constexpr bool cheat60_admits = crucible::safety::extract::IsRecipeSpec<Cheat60_DerivedFromRecipeSpec>;

struct Cheat61_FakeRecipeSpecViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_recipe_spec_impl<::Cheat61_FakeRecipeSpecViaTraitInjection> : std::true_type {
    using value_type = int;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat61_admits =
    crucible::safety::extract::IsRecipeSpec<::Cheat61_FakeRecipeSpecViaTraitInjection>;

enum class Cheat62EnumProbe : std::uint8_t {
    A = 1 << 0,
    B = 1 << 1,
    C = 1 << 2,
};
struct Cheat62_DerivedFromBits : crucible::safety::Bits<Cheat62EnumProbe> {};
static constexpr bool cheat62_admits = crucible::safety::extract::IsBits<Cheat62_DerivedFromBits>;

struct Cheat63_FakeBitsViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_bits_impl<::Cheat63_FakeBitsViaTraitInjection> : std::true_type {
    using value_type = ::Cheat62EnumProbe;
    using underlying_type = std::underlying_type_t<::Cheat62EnumProbe>;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat63_admits = crucible::safety::extract::IsBits<::Cheat63_FakeBitsViaTraitInjection>;

struct Cheat64_BorrowSource {};
struct Cheat64_DerivedFromBorrowed : crucible::safety::Borrowed<int, Cheat64_BorrowSource> {
    using crucible::safety::Borrowed<int, Cheat64_BorrowSource>::Borrowed;
};
static constexpr bool cheat64_admits = crucible::safety::extract::IsBorrowed<Cheat64_DerivedFromBorrowed>;

struct Cheat65_FakeBorrowedViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_borrowed_impl<::Cheat65_FakeBorrowedViaTraitInjection> : std::true_type {
    using element_type = int;
    using source_type = ::Cheat64_BorrowSource;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat65_admits = crucible::safety::extract::IsBorrowed<::Cheat65_FakeBorrowedViaTraitInjection>;

struct Cheat66_OwnedTag {};
struct Cheat66_DerivedFromOwnedRegion : crucible::safety::OwnedRegion<int, Cheat66_OwnedTag> {
    using crucible::safety::OwnedRegion<int, Cheat66_OwnedTag>::OwnedRegion;
};
static constexpr bool cheat66_admits = crucible::safety::extract::IsOwnedRegion<Cheat66_DerivedFromOwnedRegion>;

struct Cheat67_FakeOwnedRegionViaTraitInjection {
    int payload{0};
};
namespace crucible::safety::extract::detail {
template <>
struct is_owned_region_impl<::Cheat67_FakeOwnedRegionViaTraitInjection> : std::true_type {
    using value_type = int;
    using tag_type = ::Cheat66_OwnedTag;
};
}  // namespace crucible::safety::extract::detail
static constexpr bool cheat67_admits =
    crucible::safety::extract::IsOwnedRegion<::Cheat67_FakeOwnedRegionViaTraitInjection>;

static_assert(!cheat52_admits, "[CHEAT 52 ADMITTED] derived-from-Budgeted passed IsBudgeted.");
static_assert(!cheat54_admits, "[CHEAT 54 ADMITTED] derived-from-EpochVersioned passed IsEpochVersioned.");
static_assert(!cheat56_admits, "[CHEAT 56 ADMITTED] derived-from-NumaPlacement passed IsNumaPlacement.");
static_assert(!cheat58_admits, "[CHEAT 58 ADMITTED] derived-from-OpaqueLifetime passed IsOpaqueLifetime.");
static_assert(!cheat60_admits, "[CHEAT 60 ADMITTED] derived-from-RecipeSpec passed IsRecipeSpec.");
static_assert(!cheat62_admits, "[CHEAT 62 ADMITTED] derived-from-Bits passed IsBits.");
static_assert(!cheat64_admits, "[CHEAT 64 ADMITTED] derived-from-Borrowed passed IsBorrowed.");
static_assert(!cheat66_admits, "[CHEAT 66 ADMITTED] derived-from-OwnedRegion passed IsOwnedRegion.");

static_assert(cheat53_admits, "[CHEAT 53 STATUS CHANGED] trait-spec injection on is_budgeted_impl "
                              "is now REJECTED — flip assertion to !cheat53_admits.");
static_assert(cheat55_admits, "[CHEAT 55 STATUS CHANGED] trait-spec injection on is_epoch_versioned_impl "
                              "is now REJECTED — flip assertion to !cheat55_admits.");
static_assert(cheat57_admits, "[CHEAT 57 STATUS CHANGED] trait-spec injection on is_numa_placement_impl "
                              "is now REJECTED — flip assertion to !cheat57_admits.");
static_assert(cheat59_admits, "[CHEAT 59 STATUS CHANGED] trait-spec injection on is_opaque_lifetime_impl "
                              "is now REJECTED — flip assertion to !cheat59_admits.");
static_assert(cheat61_admits, "[CHEAT 61 STATUS CHANGED] trait-spec injection on is_recipe_spec_impl "
                              "is now REJECTED — flip assertion to !cheat61_admits.");
static_assert(cheat63_admits, "[CHEAT 63 STATUS CHANGED] trait-spec injection on is_bits_impl "
                              "is now REJECTED — flip assertion to !cheat63_admits.");
static_assert(cheat65_admits, "[CHEAT 65 STATUS CHANGED] trait-spec injection on is_borrowed_impl "
                              "is now REJECTED — flip assertion to !cheat65_admits.");
static_assert(cheat67_admits, "[CHEAT 67 STATUS CHANGED] trait-spec injection on is_owned_region_impl "
                              "is now REJECTED — flip assertion to !cheat67_admits.");

// The wrappers below carry the derived-from cheat only, so for them the
// trait-injection surface is uncovered.

#include <crucible/safety/IsHw.h>
#include <crucible/safety/IsBarrierGuarded.h>
#include <crucible/safety/IsSimdWidthPinned.h>
#include <crucible/safety/IsScopedFence.h>
#include <crucible/safety/IsJoinPolicy.h>
#include <crucible/safety/IsClockSource.h>
#include <crucible/safety/witness/IsWitness.h>

struct Cheat68_DerivedFromHw : crucible::safety::Hw<crucible::safety::HwInstruction_v::Scalar, int> {};
static constexpr bool cheat68_admits = crucible::safety::extract::IsHw<Cheat68_DerivedFromHw>;

struct Cheat69_DerivedFromBarrierGuarded
    : crucible::safety::BarrierGuarded<crucible::safety::BarrierStrength_v::AcqRel, int> {};
static constexpr bool cheat69_admits = crucible::safety::extract::IsBarrierGuarded<Cheat69_DerivedFromBarrierGuarded>;

struct Cheat70_DerivedFromSimdWidthPinned
    : crucible::safety::SimdWidthPinned<crucible::safety::SimdIsa_v::Scalar, int> {};
static constexpr bool cheat70_admits = crucible::safety::extract::IsSimdWidthPinned<Cheat70_DerivedFromSimdWidthPinned>;

struct Cheat71_DerivedFromScopedFence : crucible::safety::ScopedFence<crucible::safety::MemoryScope_v::Thread, int> {};
static constexpr bool cheat71_admits = crucible::safety::extract::IsScopedFence<Cheat71_DerivedFromScopedFence>;

struct Cheat72_DerivedFromJoinPolicy : crucible::safety::JoinPolicy<crucible::safety::JoinPolicy_v::DETACH, int> {};
static constexpr bool cheat72_admits = crucible::safety::extract::IsJoinPolicy<Cheat72_DerivedFromJoinPolicy>;

struct Cheat73_DerivedFromClockSource
    : crucible::safety::ClockSource<crucible::safety::ClockSource_v::Monotonic, std::uint64_t> {};
static constexpr bool cheat73_admits = crucible::safety::extract::IsClockSource<Cheat73_DerivedFromClockSource>;

// The canonical witness types are declared final, so a derived-from
// cheat cannot even be written for them.  The remaining way in is a
// structural lookalike: a separate type that copies the nested
// rationale typedef without being the specialization the detector
// matches.  That is the cheat below.
struct Cheat74RationaleProbe {};
struct Cheat74_LookalikeWitness {
    using rationale_type = Cheat74RationaleProbe;
};
static constexpr bool cheat74_admits = crucible::safety::witness::IsWitness<Cheat74_LookalikeWitness>;

static_assert(!cheat68_admits, "[CHEAT 68 ADMITTED] derived-from-Hw passed IsHw.");
static_assert(!cheat69_admits, "[CHEAT 69 ADMITTED] derived-from-BarrierGuarded passed IsBarrierGuarded.");
static_assert(!cheat70_admits, "[CHEAT 70 ADMITTED] derived-from-SimdWidthPinned passed IsSimdWidthPinned.");
static_assert(!cheat71_admits, "[CHEAT 71 ADMITTED] derived-from-ScopedFence passed IsScopedFence.");
static_assert(!cheat72_admits, "[CHEAT 72 ADMITTED] derived-from-JoinPolicy passed IsJoinPolicy.");
static_assert(!cheat73_admits, "[CHEAT 73 ADMITTED] derived-from-ClockSource passed IsClockSource.");
static_assert(!cheat74_admits, "[CHEAT 74 ADMITTED] structural-lookalike passed witness::IsWitness "
                               "(rationale_type alone is not the Asserted<R> specialization).");

int main() { return 0; }
