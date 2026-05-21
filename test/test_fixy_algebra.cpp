// ── test_fixy_algebra — sentinel TU for fixy/Algebra.h ─────────────
//
// Pulls fixy/Algebra.h into a TU compiled under project warning flags
// so the header's static_asserts execute under enforcement.  Witnesses:
//
//   1. fixy::algebra::Graded<M, L, T> aliases algebra::Graded<M, L, T>.
//   2. Every shipped lattice (~30 entries) is reachable via
//      fixy::algebra::lattices::*.
//   3. Lattice / Semiring concepts pass through unchanged.
//   4. ModalityKind enum + modality::* tags are reachable.
//   5. Law-verifier helpers compile when invoked via the alias.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/
//       neg_fixy_algebra_*.cpp.

#include <crucible/fixy/Algebra.h>

#include <string_view>
#include <type_traits>

namespace fa  = crucible::fixy::algebra;
namespace fal = crucible::fixy::algebra::lattices;
namespace al  = crucible::algebra;
namespace all = crucible::algebra::lattices;

// ─── 1. Graded substrate identity ─────────────────────────────────

struct AlgebraSentinel_Int {};

using GradedLinearInt = fa::Graded<
    fa::ModalityKind::Absolute,
    fal::QttSemiring::At<fal::QttGrade::One>,
    int>;

static_assert(std::is_same_v<GradedLinearInt,
    al::Graded<al::ModalityKind::Absolute,
               all::QttSemiring::At<all::QttGrade::One>,
               int>>,
    "fixy::algebra::Graded must alias algebra::Graded — same template, "
    "same instantiation address.");

// EBO collapse: linear-graded int is bit-exact sizeof(int).
static_assert(sizeof(GradedLinearInt) == sizeof(int),
    "Graded<Absolute, QttSemiring::At<One>, int> must collapse to "
    "sizeof(int) under [[no_unique_address]] EBO.");

// ─── 2. Every shipped lattice is reachable ────────────────────────

// Each lattice satisfies the Lattice concept.  Spot-check a
// representative subset; the full lattice catalog enforces self-tests
// in its own header.
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

// Templated lattices need a parameter.
struct AlgebraSentinel_Pred {
    [[nodiscard]] static constexpr bool check(int x) noexcept { return x >= 0; }
};
static_assert(fa::Lattice<fal::BoolLattice<AlgebraSentinel_Pred>>);

struct AlgebraSentinel_TrustSrc {};
static_assert(fa::Lattice<fal::TrustLattice<AlgebraSentinel_TrustSrc>>);

static_assert(fa::Lattice<fal::MonotoneLattice<int>>);
static_assert(fa::Lattice<fal::SeqPrefixLattice<int>>);
static_assert(fa::Lattice<fal::HappensBeforeLattice<3>>);

// Semirings.
static_assert(fa::Semiring<fal::QttSemiring>);
static_assert(fa::Semiring<fal::StalenessSemiring>);

// ─── 3. ModalityKind + modality::* tag identity ───────────────────

static_assert(fa::ModalityKind::Comonad       == al::ModalityKind::Comonad);
static_assert(fa::ModalityKind::RelativeMonad == al::ModalityKind::RelativeMonad);
static_assert(fa::ModalityKind::Absolute      == al::ModalityKind::Absolute);
static_assert(fa::ModalityKind::Relative      == al::ModalityKind::Relative);
static_assert(fa::ModalityKind::Quotient      == al::ModalityKind::Quotient);
static_assert(fa::ModalityKind::Coeffect      == al::ModalityKind::Coeffect);

// FIXY-U-128 / U-129 floor-vs-ceiling split: the EXACT ceiling pin
// (`== 6`) lives in fixy/Modality.h colocated with the source-of-truth
// constant; THIS TU only holds the FLOOR pin (`>= 6`) catching the
// inverse direction — an accidental REMOVAL of a ModalityKind arm.
static_assert(fa::modality_kind_count >= 6,
    "floor: fixy::algebra::modality_kind_count regressed below 6 — "
    "a ModalityKind enumerator was removed without updating both "
    "Modality.h's colocated ceiling pin AND this floor witness.");

static_assert(std::is_same_v<fa::modality::Comonad_t,       al::modality::Comonad_t>);
static_assert(std::is_same_v<fa::modality::RelativeMonad_t, al::modality::RelativeMonad_t>);
static_assert(std::is_same_v<fa::modality::Absolute_t,      al::modality::Absolute_t>);

// Modality predicates pass through.
static_assert( fa::has_counit_v<fa::ModalityKind::Comonad>);
static_assert( fa::has_unit_v<fa::ModalityKind::RelativeMonad>);
static_assert( fa::has_grade_only_v<fa::ModalityKind::Absolute>);
static_assert(!fa::has_counit_v<fa::ModalityKind::Absolute>);

// Modality name emission.
static_assert(fa::modality_name(fa::ModalityKind::Absolute)      == std::string_view{"Absolute"});
static_assert(fa::modality_name(fa::ModalityKind::Comonad)       == std::string_view{"Comonad"});
static_assert(fa::modality_name(fa::ModalityKind::RelativeMonad) == std::string_view{"RelativeMonad"});

// ─── 4. GradedWrapper concept + LatticeElement projection ─────────

static_assert(std::is_same_v<
    fa::LatticeElement<fal::QttSemiring>,
    al::LatticeElement<all::QttSemiring>>,
    "LatticeElement must project identically through the alias.");

// ─── 5. Law verifiers invokable via the alias ─────────────────────

// QttSemiring is a Semiring; pass the canonical three grades through
// the semiring-axiom verifier.  Compile-time-only check.
static_assert(fa::verify_semiring_axioms_at<fal::QttSemiring>(
    fal::QttGrade::Zero, fal::QttGrade::One, fal::QttGrade::Omega));

// Subsumption helpers.
static_assert( fa::subsumes<fal::QttSemiring>(fal::QttGrade::Zero, fal::QttGrade::Omega));

// ─── 6. Runtime sanity — instantiate a Graded value ───────────────

int main() {
    using LinearInt = fa::Graded<fa::ModalityKind::Absolute,
                                  fal::QttSemiring::At<fal::QttGrade::One>,
                                  int>;
    LinearInt g{42, fal::QttSemiring::At<fal::QttGrade::One>::element_type{}};
    int observed = g.peek();
    (void)observed;
    return 0;
}
