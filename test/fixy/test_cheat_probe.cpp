// Adversarial cheat-detection harness for the fixy wrappers.
//
// Every fixture below is a class built to look like it satisfies a
// wrapper concept while violating exactly one structural property.
// Each one is then inverted into an assertion that it is NOT admitted,
// so the file compiles only while every cheat is still rejected.  A
// change that weakens a concept starts admitting its cheat and the
// build stops.
//
// The harness grows monotonically.  A new cheat is added, never
// removed, and a rejection once locked in stays locked in.
//
// Old spelling: test/test_concept_cheat_probe.cpp, over the old safety
// wrappers.  That file carried twenty-seven cheats it asserted as
// ADMITTED: each specialized a detection trait from a foreign
// namespace, and a trait is a class template any translation unit can
// specialize, so no concept built on one could tell the two apart.
// The new tree's detection is a concept over a reflection query, and
// its opt-ins are members of the wrapper's own class body, so a
// specialization written here reaches no gate.  Every one of those
// cheats that has a target in the new tree is asserted REJECTED below,
// and each is paired with an assertion that the specialization did take
// on the variable it named, so the rejection is shown to be the
// concept's and not a typo's.  The one cheat that cannot be written at
// all, a specialization of graded_modality for a type that is not
// Graded, is the negative-compile fixture
// neg/neg_cheat_graded_modality_injection.cpp: the primary is
// constrained, so the specialization is a constraint failure.
//
// Two cheats stay admitted, and neither reopens a namespace:
//
//   - Cheat 4, raw storage beside a correct diagnostic surface.  The
//     concept checks names, types and forwarders and says nothing about
//     storage on purpose (foundation/algebra/GradedTrait.h).  Storage is
//     what CRUCIBLE_GRADED_LAYOUT_INVARIANT and the per-wrapper size
//     cells in test_wrapper_verification.cpp check, per regime.
//   - Cheat 18, a function-template forwarder with a defaulted template
//     parameter.  It is called exactly like a plain function and it
//     forwards honestly; the admission is recorded rather than treated
//     as a hole.
//
// Ten of the old pairs have no target here, because the new tree did
// not carry the wrapper: MemOrder, Progress, ResidencyHeat, Vendor,
// Crash, Consistency, Budgeted, EpochVersioned, NumaPlacement, and the
// witness lookalike.  Bits was carried without a detection surface, so
// it has no pair either.

#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <fixy/Machine.h>
#include <fixy/Mutation.h>
#include <fixy/OwnedRegion.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/reflect/Instance.h>

#include <string_view>
#include <type_traits>

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fr = ::foundation::reflect;

using One = fl::QttSemiring::At<fl::QttGrade::One>;
using SubstrateInt = fa::Graded<fa::ModalityKind::Absolute, One, int>;

constexpr bool positive_p(int x) noexcept { return x > 0; }

// ── The generic wrapper concept ─────────────────────────────────────
//
// Each fixture states the six members GradedWrapper reads and breaks
// exactly one of them.  The old file left `modality` off the first ten,
// so each of those was refused for a missing member on top of its own
// defect; here every fixture declares it, and is refused by its defect
// alone.

struct Cheat1_ValueTypeMismatch {
    using value_type = int;
    using lattice_type = One;
    using graded_type = fa::Graded<fa::ModalityKind::Absolute, One, double>;  // different from value_type
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(!fa::GradedWrapper<Cheat1_ValueTypeMismatch>,
              "[CHEAT 1 ADMITTED] value_type vs graded_type::value_type mismatch passed");

struct Cheat2_LatticeMismatch {
    using value_type = int;
    using lattice_type = One;  // claimed
    using graded_type = fa::Graded<fa::ModalityKind::Absolute, fl::BoolLattice<decltype(positive_p)>, int>;  // actual
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(!fa::GradedWrapper<Cheat2_LatticeMismatch>,
              "[CHEAT 2 ADMITTED] lattice_type vs graded_type::lattice_type mismatch passed");

struct Cheat3_LyingForwarders {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "TOTALLY-LYING"; }
    static consteval std::string_view lattice_name() noexcept { return "ALSO-LYING"; }
};
static_assert(!fa::GradedWrapper<Cheat3_LyingForwarders>, "[CHEAT 3 ADMITTED] lying forwarders passed");

// Admitted, and stated so at the top of the file: the concept does not
// see storage.
struct Cheat4_NoSubstrateUsage {
    int data;  // raw storage, not a graded_type member
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(fa::GradedWrapper<Cheat4_NoSubstrateUsage>,
              "[CHEAT 4 STATUS CHANGED] raw storage beside a correct surface is now REJECTED: the concept "
              "learned to see storage. Flip this assertion and delete the admission from the header comment.");

struct Cheat5_ModalityMismatch {
    using value_type = int;
    using lattice_type = One;
    // A linear-shaped wrapper models Absolute, not Comonad.
    using graded_type = fa::Graded<fa::ModalityKind::Comonad, One, int>;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(!fa::GradedWrapper<Cheat5_ModalityMismatch>, "[CHEAT 5 ADMITTED] modality mismatch passed");

struct Cheat6_ReferenceReturn {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static const std::string_view& value_type_name() noexcept;
    static const std::string_view& lattice_name() noexcept;
};
static_assert(!fa::GradedWrapper<Cheat6_ReferenceReturn>, "[CHEAT 6 ADMITTED] reference-return forwarder passed");

struct Cheat7_ThrowingForwarder {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static std::string_view value_type_name() { return graded_type::value_type_name(); }  // not noexcept
    static std::string_view lattice_name() { return graded_type::lattice_name(); }  // not noexcept
};
static_assert(!fa::GradedWrapper<Cheat7_ThrowingForwarder>, "[CHEAT 7 ADMITTED] throwing forwarder passed");

struct AlmostStringView {
    operator std::string_view() const noexcept;
};

struct Cheat8_ImplicitConversion {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval AlmostStringView value_type_name() noexcept { return {}; }
    static consteval AlmostStringView lattice_name() noexcept { return {}; }
};
static_assert(!fa::GradedWrapper<Cheat8_ImplicitConversion>, "[CHEAT 8 ADMITTED] implicit-conversion forwarder passed");

struct DerivedGraded : SubstrateInt {};

struct Cheat9_DerivedGraded {
    using value_type = int;
    using lattice_type = One;
    using graded_type = DerivedGraded;  // derived, not Graded itself
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(!fa::GradedWrapper<Cheat9_DerivedGraded>, "[CHEAT 9 ADMITTED] derived-Graded passed");

template <typename T>
struct CyclicRef {
    using value_type = T;
    using lattice_type = One;
    using graded_type = fa::Graded<fa::ModalityKind::Absolute, One, CyclicRef<T>>;  // self-reference
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "x"; }
    static consteval std::string_view lattice_name() noexcept { return "y"; }
};
static_assert(!fa::GradedWrapper<CyclicRef<int>>, "[CHEAT 10 ADMITTED] cyclic self-reference passed");

// Cheat 11: trait-spec injection on is_graded_specialization.  The
// specialization takes on the struct and the concept does not read it.
// Its twin, a specialization of graded_modality, no longer compiles at
// all: the primary is constrained to Graded, and the negative-compile
// fixture beside this file stands on that.
struct Cheat11_FakeSubstrate {
    using value_type = int;
    using lattice_type = One;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};
namespace foundation::algebra {
template <>
struct is_graded_specialization<::Cheat11_FakeSubstrate> : std::true_type {};
}  // namespace foundation::algebra
struct Cheat11_TraitInjection {
    using value_type = int;
    using lattice_type = One;
    using graded_type = ::Cheat11_FakeSubstrate;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};
static_assert(fa::is_graded_specialization<::Cheat11_FakeSubstrate>::value, "the injection did not take");
static_assert(!fa::IsGraded<::Cheat11_FakeSubstrate>);
static_assert(!fa::GradedWrapper<Cheat11_TraitInjection>,
              "[CHEAT 11 ADMITTED] trait-spec injection on is_graded_specialization reached GradedWrapper");

// Cheat 12: trait-spec injection on value_type_decoupled, to escape the
// value_type check of Cheat 1.  The opt-in is a member now.
struct Cheat12_DecoupledOptOut {
    using value_type = double;  // mismatches the substrate's int
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
namespace foundation::algebra {
template <>
struct value_type_decoupled<::Cheat12_DecoupledOptOut> : std::true_type {};
}  // namespace foundation::algebra
static_assert(fa::value_type_decoupled<::Cheat12_DecoupledOptOut>::value, "the injection did not take");
static_assert(!fa::DeclaresValueTypeDecoupled<::Cheat12_DecoupledOptOut>);
static_assert(!fa::GradedWrapper<::Cheat12_DecoupledOptOut>,
              "[CHEAT 12 ADMITTED] trait-spec injection on value_type_decoupled reached GradedWrapper");

// Cheat 13: the same escape through the member, declared false.  A
// declaration that says no is no opt-in.
struct Cheat13_DecoupledDeclaredFalse {
    using value_type = double;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static constexpr bool value_type_decoupled = false;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(!fa::GradedWrapper<Cheat13_DecoupledDeclaredFalse>,
              "[CHEAT 13 ADMITTED] value_type_decoupled = false was read as an opt-in");

struct Cheat14_ConstReturn {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval const std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval const std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(!fa::GradedWrapper<Cheat14_ConstReturn>, "[CHEAT 14 ADMITTED] const-qualified return passed");

struct Cheat15_DeletedForwarder {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept = delete("nope");
    static consteval std::string_view lattice_name() noexcept = delete("nope");
};
static_assert(!fa::GradedWrapper<Cheat15_DeletedForwarder>, "[CHEAT 15 ADMITTED] deleted forwarder passed");

struct Cheat16_VariableNotFunction {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static constexpr std::string_view value_type_name = "int";
    static constexpr std::string_view lattice_name = "QttSemiring::At<1>";
};
static_assert(!fa::GradedWrapper<Cheat16_VariableNotFunction>, "[CHEAT 16 ADMITTED] variable not function passed");

struct Cheat17_NonStaticMember {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    consteval std::string_view value_type_name() const noexcept { return graded_type::value_type_name(); }
    consteval std::string_view lattice_name() const noexcept { return graded_type::lattice_name(); }
};
static_assert(!fa::GradedWrapper<Cheat17_NonStaticMember>, "[CHEAT 17 ADMITTED] non-static member passed");

// Admitted, and stated so at the top of the file.
struct Cheat18_FunctionTemplateForwarder {
    using value_type = int;
    using lattice_type = One;
    using graded_type = SubstrateInt;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    template <int = 0>
    static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    template <int = 0>
    static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};
static_assert(fa::GradedWrapper<Cheat18_FunctionTemplateForwarder>,
              "[CHEAT 18 STATUS CHANGED] function-template forwarder with default template arg is now REJECTED. "
              "Flip this assertion if the rejection is intentional, and delete the admission from the "
              "header comment.");

// Cheat 19: the derived spelling of the wrapper predicate is a
// variable template, and a variable template can be specialized.  The
// specialization takes, and the concept reads the reflection query.
struct Cheat19_FakeWrapper {};
namespace foundation::algebra {
template <>
inline constexpr bool is_graded_wrapper_v<::Cheat19_FakeWrapper> = true;
}  // namespace foundation::algebra
static_assert(fa::is_graded_wrapper_v<::Cheat19_FakeWrapper>, "the injection did not take");
static_assert(!fa::GradedWrapper<::Cheat19_FakeWrapper>,
              "[CHEAT 19 ADMITTED] is_graded_wrapper_v injection reached GradedWrapper");

// ── The substrate's own identity ────────────────────────────────────
//
// Every wrapper concept stands on IsGraded or on IsInstanceOf, so these
// two are attacked first, from each side.

struct Cheat20_FakeGraded {
    using value_type = int;
    using lattice_type = fl::DetSafeLattice::At<fl::DetSafeTier::Pure>;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
};
namespace foundation::algebra {
template <>
inline constexpr bool is_graded_v<::Cheat20_FakeGraded> = true;
}  // namespace foundation::algebra
static_assert(fa::is_graded_v<::Cheat20_FakeGraded>, "the injection did not take");
static_assert(!fa::IsGraded<::Cheat20_FakeGraded>, "[CHEAT 20 ADMITTED] is_graded_v injection reached IsGraded");
static_assert(!fixy::IsBand<::Cheat20_FakeGraded>, "[CHEAT 20 ADMITTED] is_graded_v injection reached IsBand");
static_assert(!fixy::IsBandOf<fixy::DetSafeLattice, ::Cheat20_FakeGraded>,
              "[CHEAT 20 ADMITTED] is_graded_v injection reached IsBandOf");

struct Cheat21_FakeInstance {};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat21_FakeInstance, ^^::foundation::algebra::Graded> = true;
}  // namespace foundation::reflect
static_assert(fr::is_instance_of_v<::Cheat21_FakeInstance, ^^fa::Graded>, "the injection did not take");
static_assert(!fr::IsInstanceOf<::Cheat21_FakeInstance, ^^fa::Graded>,
              "[CHEAT 21 ADMITTED] is_instance_of_v injection reached IsInstanceOf");
static_assert(!fa::IsGraded<::Cheat21_FakeInstance>, "[CHEAT 21 ADMITTED] is_instance_of_v injection reached IsGraded");

struct Cheat22_DerivedFromGraded : SubstrateInt {};
static_assert(!fa::IsGraded<Cheat22_DerivedFromGraded>, "[CHEAT 22 ADMITTED] derived-from-Graded passed IsGraded");

struct Cheat23_FakeBand {
    using value_type = int;
    using lattice_type = fl::DetSafeLattice::At<fl::DetSafeTier::Pure>;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
};
namespace fixy {
template <>
inline constexpr bool is_band_v<::Cheat23_FakeBand> = true;
}  // namespace fixy
static_assert(fixy::is_band_v<::Cheat23_FakeBand>, "the injection did not take");
static_assert(!fixy::IsBand<::Cheat23_FakeBand>, "[CHEAT 23 ADMITTED] is_band_v injection reached IsBand");

// ── Per-wrapper detection ───────────────────────────────────────────
//
// The cheats above attack the generic concept and the substrate.
// Everything from here on attacks the per-wrapper detection concepts,
// and each wrapper gets the same three:
//
//   - a class derived from the wrapper, which must be REJECTED: the
//     concept matches the exact specialization, so a subclass does not
//     match it.  Were it to match, a boundary that demands a wrapper
//     would silently accept any subclass, along with whatever extra
//     state and invariants the subclass carries.
//   - an unrelated class with the wrapper's is_*_v specialized for it,
//     which must be REJECTED: the value is derived from the concept and
//     read by no gate.  The old tree admitted this.
//   - the same class with is_instance_of_v specialized for it against
//     the wrapper's template, which must be REJECTED for the same
//     reason.

struct FakeSource {};
struct FakeOwnedTag {};

// Linear and Affine: one class template, two grades.
struct Cheat24_DerivedFromLinear : fixy::Linear<int> {};
static_assert(!fr::IsInstanceOf<Cheat24_DerivedFromLinear, ^^fixy::Qtt>,
              "[CHEAT 24 ADMITTED] derived-from-Linear passed the Qtt query");

struct Cheat25_FakeLinear {
    int payload{0};
};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat25_FakeLinear, ^^::fixy::Qtt> = true;
}  // namespace foundation::reflect
static_assert(fr::is_instance_of_v<::Cheat25_FakeLinear, ^^fixy::Qtt>, "the injection did not take");
static_assert(!fr::IsInstanceOf<::Cheat25_FakeLinear, ^^fixy::Qtt>,
              "[CHEAT 25 ADMITTED] is_instance_of_v injection passed the Qtt query");

// Refined and SealedRefined share Refinement.
struct Cheat26_DerivedFromRefined : fixy::Refined<positive_p, int> {};
static_assert(!fixy::IsRefined<Cheat26_DerivedFromRefined>, "[CHEAT 26 ADMITTED] derived-from-Refined passed IsRefined");

struct Cheat27_FakeRefined {
    int payload{0};
};
namespace fixy {
template <>
inline constexpr bool is_refined_v<::Cheat27_FakeRefined> = true;
}  // namespace fixy
static_assert(fixy::is_refined_v<::Cheat27_FakeRefined>, "the injection did not take");
static_assert(!fixy::IsRefined<::Cheat27_FakeRefined>, "[CHEAT 27 ADMITTED] is_refined_v injection reached IsRefined");

struct Cheat28_FakeRefinedInstance {
    int payload{0};
};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat28_FakeRefinedInstance, ^^::fixy::Refinement> = true;
}  // namespace foundation::reflect
static_assert(fr::is_instance_of_v<::Cheat28_FakeRefinedInstance, ^^fixy::Refinement>, "the injection did not take");
static_assert(!fixy::IsRefined<::Cheat28_FakeRefinedInstance>,
              "[CHEAT 28 ADMITTED] is_instance_of_v injection reached IsRefined");

struct Cheat29_DerivedFromTagged : fixy::Tagged<int, fixy::tags::source::FromUser> {};
static_assert(!fixy::IsTagged<Cheat29_DerivedFromTagged>, "[CHEAT 29 ADMITTED] derived-from-Tagged passed IsTagged");

struct Cheat30_FakeTagged {
    int payload{0};
};
namespace fixy {
template <>
inline constexpr bool is_tagged_v<::Cheat30_FakeTagged> = true;
}  // namespace fixy
static_assert(fixy::is_tagged_v<::Cheat30_FakeTagged>, "the injection did not take");
static_assert(!fixy::IsTagged<::Cheat30_FakeTagged>, "[CHEAT 30 ADMITTED] is_tagged_v injection reached IsTagged");

struct Cheat31_FakeTaggedInstance {
    int payload{0};
};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat31_FakeTaggedInstance, ^^::fixy::Tagged> = true;
}  // namespace foundation::reflect
static_assert(!fixy::IsTagged<::Cheat31_FakeTaggedInstance>,
              "[CHEAT 31 ADMITTED] is_instance_of_v injection reached IsTagged");

struct Cheat32_DerivedFromSecret : fixy::Secret<int> {};
static_assert(!fixy::IsSecret<Cheat32_DerivedFromSecret>, "[CHEAT 32 ADMITTED] derived-from-Secret passed IsSecret");

struct Cheat33_FakeSecret {
    int payload{0};
};
namespace fixy {
template <>
inline constexpr bool is_secret_v<::Cheat33_FakeSecret> = true;
}  // namespace fixy
static_assert(fixy::is_secret_v<::Cheat33_FakeSecret>, "the injection did not take");
static_assert(!fixy::IsSecret<::Cheat33_FakeSecret>, "[CHEAT 33 ADMITTED] is_secret_v injection reached IsSecret");

struct Cheat34_FakeSecretInstance {
    int payload{0};
};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat34_FakeSecretInstance, ^^::fixy::Secret> = true;
}  // namespace foundation::reflect
static_assert(!fixy::IsSecret<::Cheat34_FakeSecretInstance>,
              "[CHEAT 34 ADMITTED] is_instance_of_v injection reached IsSecret");

struct Cheat35_DerivedFromStale : fixy::Stale<int> {};
static_assert(!fixy::IsStale<Cheat35_DerivedFromStale>, "[CHEAT 35 ADMITTED] derived-from-Stale passed IsStale");

struct Cheat36_FakeStale {
    int payload{0};
};
namespace fixy {
template <>
inline constexpr bool is_stale_v<::Cheat36_FakeStale> = true;
}  // namespace fixy
static_assert(fixy::is_stale_v<::Cheat36_FakeStale>, "the injection did not take");
static_assert(!fixy::IsStale<::Cheat36_FakeStale>, "[CHEAT 36 ADMITTED] is_stale_v injection reached IsStale");

struct Cheat37_FakeStaleInstance {
    int payload{0};
};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat37_FakeStaleInstance, ^^::fixy::Stale> = true;
}  // namespace foundation::reflect
static_assert(!fixy::IsStale<::Cheat37_FakeStaleInstance>,
              "[CHEAT 37 ADMITTED] is_instance_of_v injection reached IsStale");

struct Cheat38_DerivedFromBorrowed : fixy::Borrowed<int, FakeSource> {};
static_assert(!fixy::IsBorrowed<Cheat38_DerivedFromBorrowed>,
              "[CHEAT 38 ADMITTED] derived-from-Borrowed passed IsBorrowed");

struct Cheat39_FakeBorrowed {
    int payload{0};
};
namespace fixy {
template <>
inline constexpr bool is_borrowed_v<::Cheat39_FakeBorrowed> = true;
}  // namespace fixy
static_assert(fixy::is_borrowed_v<::Cheat39_FakeBorrowed>, "the injection did not take");
static_assert(!fixy::IsBorrowed<::Cheat39_FakeBorrowed>,
              "[CHEAT 39 ADMITTED] is_borrowed_v injection reached IsBorrowed");

struct Cheat40_FakeBorrowedInstance {
    int payload{0};
};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat40_FakeBorrowedInstance, ^^::fixy::Borrowed> = true;
}  // namespace foundation::reflect
static_assert(!fixy::IsBorrowed<::Cheat40_FakeBorrowedInstance>,
              "[CHEAT 40 ADMITTED] is_instance_of_v injection reached IsBorrowed");

struct Cheat41_DerivedFromOwnedRegion : fixy::OwnedRegion<int, FakeOwnedTag> {};
static_assert(!fixy::IsOwnedRegion<Cheat41_DerivedFromOwnedRegion>,
              "[CHEAT 41 ADMITTED] derived-from-OwnedRegion passed IsOwnedRegion");

struct Cheat42_FakeOwnedRegion {
    int payload{0};
};
namespace fixy {
template <>
inline constexpr bool is_owned_region_v<::Cheat42_FakeOwnedRegion> = true;
}  // namespace fixy
static_assert(fixy::is_owned_region_v<::Cheat42_FakeOwnedRegion>, "the injection did not take");
static_assert(!fixy::IsOwnedRegion<::Cheat42_FakeOwnedRegion>,
              "[CHEAT 42 ADMITTED] is_owned_region_v injection reached IsOwnedRegion");

struct Cheat43_FakeOwnedRegionInstance {
    int payload{0};
};
namespace foundation::reflect {
template <>
inline constexpr bool is_instance_of_v<::Cheat43_FakeOwnedRegionInstance, ^^::fixy::OwnedRegion> = true;
}  // namespace foundation::reflect
static_assert(!fixy::IsOwnedRegion<::Cheat43_FakeOwnedRegionInstance>,
              "[CHEAT 43 ADMITTED] is_instance_of_v injection reached IsOwnedRegion");

// WriteOnce and Machine publish a value and a struct, both derived from
// the query; a specialization of either changes only its own readers.
struct Cheat44_DerivedFromWriteOnce : fixy::WriteOnce<int> {};
static_assert(!fixy::is_writeonce_v<Cheat44_DerivedFromWriteOnce>,
              "[CHEAT 44 ADMITTED] derived-from-WriteOnce passed is_writeonce_v");

struct Cheat45_FakeWriteOnce {
    int payload{0};
};
namespace fixy {
template <>
struct is_writeonce<::Cheat45_FakeWriteOnce> : std::true_type {};
}  // namespace fixy
static_assert(fixy::is_writeonce<::Cheat45_FakeWriteOnce>::value, "the injection did not take");
static_assert(!fixy::is_writeonce_v<::Cheat45_FakeWriteOnce>,
              "[CHEAT 45 ADMITTED] a specialization of the is_writeonce struct reached is_writeonce_v");
static_assert(!fr::IsInstanceOf<::Cheat45_FakeWriteOnce, ^^fixy::WriteOnce>);

struct Cheat46_DerivedFromMachine : fixy::Machine<int> {};
static_assert(!fixy::mach::is_machine_v<Cheat46_DerivedFromMachine>,
              "[CHEAT 46 ADMITTED] derived-from-Machine passed is_machine_v");

// ── The bands ───────────────────────────────────────────────────────
//
// A band is Graded itself, so a class derived from one fails IsGraded
// and with it IsBand, IsBandOf and the tier query.  One derived cheat
// per band, and one for RecipeSpec.

struct Cheat47_DerivedFromDetSafe : fixy::det_safe::Pure<int> {};
static_assert(!fixy::IsBand<Cheat47_DerivedFromDetSafe>, "[CHEAT 47 ADMITTED] derived-from-DetSafe passed IsBand");
static_assert(!fixy::IsBandOf<fixy::DetSafeLattice, Cheat47_DerivedFromDetSafe>,
              "[CHEAT 47 ADMITTED] derived-from-DetSafe passed IsBandOf");

struct Cheat48_DerivedFromAllocClass : fixy::alloc_class::Arena<int> {};
static_assert(!fixy::IsBand<Cheat48_DerivedFromAllocClass>,
              "[CHEAT 48 ADMITTED] derived-from-AllocClass passed IsBand");

struct Cheat49_DerivedFromHotPath : fixy::hot_path::Hot<int> {};
static_assert(!fixy::IsBand<Cheat49_DerivedFromHotPath>, "[CHEAT 49 ADMITTED] derived-from-HotPath passed IsBand");

struct Cheat50_DerivedFromCipherTier : fixy::cipher_tier::Hot<int> {};
static_assert(!fixy::IsBand<Cheat50_DerivedFromCipherTier>,
              "[CHEAT 50 ADMITTED] derived-from-CipherTier passed IsBand");

struct Cheat51_DerivedFromWait : fixy::wait::SpinPause<int> {};
static_assert(!fixy::IsBand<Cheat51_DerivedFromWait>, "[CHEAT 51 ADMITTED] derived-from-Wait passed IsBand");

struct Cheat52_DerivedFromNumericalTier : fixy::numerical_tier::Bitexact<int> {};
static_assert(!fixy::IsBand<Cheat52_DerivedFromNumericalTier>,
              "[CHEAT 52 ADMITTED] derived-from-NumericalTier passed IsBand");

struct Cheat53_DerivedFromOpaqueLifetime : fixy::opaque_lifetime::PerFleet<int> {};
static_assert(!fixy::IsBand<Cheat53_DerivedFromOpaqueLifetime>,
              "[CHEAT 53 ADMITTED] derived-from-OpaqueLifetime passed IsBand");

struct Cheat54_DerivedFromRecipeSpec : fixy::RecipeSpec<int> {};
static_assert(!fixy::IsRecipeSpec<Cheat54_DerivedFromRecipeSpec>,
              "[CHEAT 54 ADMITTED] derived-from-RecipeSpec passed IsRecipeSpec");

struct Cheat55_FakeRecipeSpec {
    int payload{0};
};
namespace fixy {
template <>
inline constexpr bool is_recipe_spec_v<::Cheat55_FakeRecipeSpec> = true;
}  // namespace fixy
static_assert(fixy::is_recipe_spec_v<::Cheat55_FakeRecipeSpec>, "the injection did not take");
static_assert(!fixy::IsRecipeSpec<::Cheat55_FakeRecipeSpec>,
              "[CHEAT 55 ADMITTED] is_recipe_spec_v injection reached IsRecipeSpec");

// A structural lookalike of a band: the four members IsBand reads,
// without being the substrate.  IsGraded is what refuses it.
struct Cheat56_LookalikeBand {
    using value_type = int;
    using lattice_type = fl::DetSafeLattice::At<fl::DetSafeTier::Pure>;
    using graded_type = fixy::det_safe::Pure<int>;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return graded_type::value_type_name(); }
    static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};
static_assert(!fixy::IsBand<Cheat56_LookalikeBand>, "[CHEAT 56 ADMITTED] a structural lookalike passed IsBand");
static_assert(fa::GradedWrapper<Cheat56_LookalikeBand>,
              "A lookalike that forwards honestly IS a graded wrapper; it is not a band, and the two "
              "questions must stay distinct.");

int main() { return 0; }
