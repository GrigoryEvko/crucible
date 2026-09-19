// Sentinel TU for the algebra core.  Each header carries its own
// static_asserts and an inline runtime_smoke_test; a header no
// translation unit includes is never compiled under the project flags
// and its smoke test never runs.  This file includes each of the four
// and calls each smoke test once.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/Modality.h>

#include <type_traits>

namespace {

namespace fa = ::foundation::algebra;

// The five-kind set is what the extraction decided; the left side is
// derived by reflection, so this fires if an enumerator is added or
// removed.
static_assert(fa::modality_kind_count == 5);
static_assert(fa::SteppingModality<fa::ModalityKind::Stepping>);
static_assert(fa::has_grade_only_v<fa::ModalityKind::Stepping>);

// Row is a refinement of BoundedLattice, and the self-test row is its
// first witness.
static_assert(fa::Row<fa::detail::lattice_self_test::TrivialRow>);
static_assert(!fa::Row<fa::detail::lattice_self_test::TrivialBoolLattice>);

// The primary template over an empty grade still collapses to sizeof(T).
using EmptyGraded = fa::Graded<fa::ModalityKind::Absolute, fa::detail::graded_self_test::TrivialEmptyLattice, int>;
static_assert(sizeof(EmptyGraded) == sizeof(int));
static_assert(fa::IsGraded<EmptyGraded>);
static_assert(fa::is_graded_specialization_v<EmptyGraded const&>);

}  // namespace

int main() {
    fa::detail::lattice_self_test::runtime_smoke_test();
    fa::detail::modality_self_test::runtime_smoke_test();
    fa::detail::graded_self_test::runtime_smoke_test();
    fa::detail::is_graded_specialization_self_test::runtime_smoke_test();
    return 0;
}
