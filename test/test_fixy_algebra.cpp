// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags.

#include <crucible/fixy/Algebra.h>

#include <string_view>
#include <type_traits>

namespace fa = crucible::fixy::algebra;
namespace fal = crucible::fixy::algebra::lattices;
namespace al = crucible::algebra;
namespace all = crucible::algebra::lattices;

struct AlgebraSentinel_Int {};

using GradedLinearInt = fa::Graded<fa::ModalityKind::Absolute, fal::QttSemiring::At<fal::QttGrade::One>, int>;

static_assert(std::is_same_v<GradedLinearInt,
                             al::Graded<al::ModalityKind::Absolute, all::QttSemiring::At<all::QttGrade::One>, int>>,
              "fixy::algebra::Graded must alias algebra::Graded — same template, "
              "same instantiation address.");

static_assert(sizeof(GradedLinearInt) == sizeof(int), "Graded<Absolute, QttSemiring::At<One>, int> must collapse to "
                                                      "sizeof(int) under [[no_unique_address]] EBO.");

// A representative sample, not the full catalog. Each lattice header
// carries its own exhaustive self-test.
static_assert(fa::Lattice<fal::QttSemiring>);
static_assert(fa::Lattice<fal::FractionalLattice>);
static_assert(fa::Lattice<fal::ConfLattice>);
static_assert(fa::Lattice<fal::CipherTierLattice>);
static_assert(fa::Lattice<fal::CrashLattice>);
static_assert(fa::Lattice<fal::DetSafeLattice>);
static_assert(fa::Lattice<fal::HotPathLattice>);
static_assert(fa::Lattice<fal::MemOrderLattice>);
static_assert(fa::Lattice<fal::ProgressLattice>);
static_assert(fa::Lattice<fal::ResidencyHeatLattice>);
static_assert(fa::Lattice<fal::VendorLattice>);
static_assert(fa::Lattice<fal::WaitLattice>);
static_assert(fa::Lattice<fal::AllocClassLattice>);
static_assert(fa::Lattice<fal::ConsistencyLattice>);
static_assert(fa::Lattice<fal::LifetimeLattice>);
static_assert(fa::Lattice<fal::ToleranceLattice>);
static_assert(fa::Lattice<fal::AffinityLattice>);

struct AlgebraSentinel_Pred {
    [[nodiscard]] static constexpr bool check(int x) noexcept { return x >= 0; }
};
static_assert(fa::Lattice<fal::BoolLattice<AlgebraSentinel_Pred>>);

struct AlgebraSentinel_TrustSrc {};
static_assert(fa::Lattice<fal::TrustLattice<AlgebraSentinel_TrustSrc>>);

static_assert(fa::Lattice<fal::MonotoneLattice<int>>);
static_assert(fa::Lattice<fal::SeqPrefixLattice<int>>);
static_assert(fa::Lattice<fal::HappensBeforeLattice<3>>);

static_assert(fa::Semiring<fal::QttSemiring>);
static_assert(fa::Semiring<fal::StalenessSemiring>);

static_assert(fa::ModalityKind::Comonad == al::ModalityKind::Comonad);
static_assert(fa::ModalityKind::RelativeMonad == al::ModalityKind::RelativeMonad);
static_assert(fa::ModalityKind::Absolute == al::ModalityKind::Absolute);
static_assert(fa::ModalityKind::Relative == al::ModalityKind::Relative);
static_assert(fa::ModalityKind::Quotient == al::ModalityKind::Quotient);
static_assert(fa::ModalityKind::Coeffect == al::ModalityKind::Coeffect);

// A floor, not an exact count. The exact count is pinned beside the
// enumerator definition, so this witness catches only the removal of an
// arm, which that pin cannot see.
static_assert(fa::modality_kind_count >= 6, "floor: fixy::algebra::modality_kind_count regressed below 6. A "
                                            "ModalityKind enumerator was removed without updating the exact pin "
                                            "beside the enumerator definition and this floor witness.");

static_assert(std::is_same_v<fa::modality::Comonad_t, al::modality::Comonad_t>);
static_assert(std::is_same_v<fa::modality::RelativeMonad_t, al::modality::RelativeMonad_t>);
static_assert(std::is_same_v<fa::modality::Absolute_t, al::modality::Absolute_t>);

static_assert(fa::has_counit_v<fa::ModalityKind::Comonad>);
static_assert(fa::has_unit_v<fa::ModalityKind::RelativeMonad>);
static_assert(fa::has_grade_only_v<fa::ModalityKind::Absolute>);
static_assert(!fa::has_counit_v<fa::ModalityKind::Absolute>);

static_assert(fa::modality_name(fa::ModalityKind::Absolute) == std::string_view{"Absolute"});
static_assert(fa::modality_name(fa::ModalityKind::Comonad) == std::string_view{"Comonad"});
static_assert(fa::modality_name(fa::ModalityKind::RelativeMonad) == std::string_view{"RelativeMonad"});

static_assert(std::is_same_v<fa::LatticeElement<fal::QttSemiring>, al::LatticeElement<all::QttSemiring>>,
              "LatticeElement must project identically through the alias.");

static_assert(fa::verify_semiring_axioms_at<fal::QttSemiring>(fal::QttGrade::Zero, fal::QttGrade::One,
                                                              fal::QttGrade::Omega));

static_assert(fa::subsumes<fal::QttSemiring>(fal::QttGrade::Zero, fal::QttGrade::Omega));

int main() {
    using LinearInt = fa::Graded<fa::ModalityKind::Absolute, fal::QttSemiring::At<fal::QttGrade::One>, int>;
    LinearInt g{42, fal::QttSemiring::At<fal::QttGrade::One>::element_type{}};
    int observed = g.peek();
    (void)observed;
    return 0;
}
