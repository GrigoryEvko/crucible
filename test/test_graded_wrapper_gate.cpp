// Each cheat below is a wrapper that satisfies the shape of the concept
// while lying about one of its projections. Rejecting all of them is what
// the build proves. They are rebuilt here rather than shared with the
// wider cheat suite, so this file stands alone and depends on nothing but
// the concept it checks.
//
// The gate specialization must be visible before any probe in this file
// instantiates it, so its header comes first.
#include <crucible/safety/diag/GradedWrapperGate.h>

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>
#include <crucible/algebra/lattices/BoolLattice.h>

#include <string_view>
#include <type_traits>

namespace cheats {

namespace algebra = ::crucible::algebra;
namespace lattices = ::crucible::algebra::lattices;

constexpr bool positive_p(int x) noexcept { return x > 0; }

struct Cheat1_ValueTypeMismatch {
    using value_type = int;
    using lattice_type = lattices::QttSemiring::At<lattices::QttGrade::One>;
    using graded_type =
        algebra::Graded<algebra::ModalityKind::Absolute, lattices::QttSemiring::At<lattices::QttGrade::One>,
                        double>;  // ← differs
    static constexpr algebra::ModalityKind modality = algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QTT::1"; }
};

struct Cheat2_LatticeMismatch {
    using value_type = int;
    using lattice_type = lattices::QttSemiring::At<lattices::QttGrade::One>;
    using graded_type = algebra::Graded<algebra::ModalityKind::Absolute, lattices::BoolLattice<decltype(positive_p)>,
                                        int>;  // ← lattice differs
    static constexpr algebra::ModalityKind modality = algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "x"; }
};

struct Cheat3_LyingForwarders {
    using value_type = int;
    using lattice_type = lattices::QttSemiring::At<lattices::QttGrade::One>;
    using graded_type =
        algebra::Graded<algebra::ModalityKind::Absolute, lattices::QttSemiring::At<lattices::QttGrade::One>, int>;
    static constexpr algebra::ModalityKind modality = algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept {
        return "TOTALLY-LYING";  // ← does not match graded_type::value_type_name()
    }
    static consteval std::string_view lattice_name() noexcept {
        return "ALSO-LYING";  // ← does not match graded_type::lattice_name()
    }
};

}  // namespace cheats

// Each probe carries a static_assert that the gate rejects its cheat, so
// the build succeeding is the whole claim.

namespace diag = ::crucible::safety::diag;

using probe_1 = diag::cheat_probe_type<cheats::Cheat1_ValueTypeMismatch, diag::Category::GradedWrapperViolation>;
using probe_2 = diag::cheat_probe_type<cheats::Cheat2_LatticeMismatch, diag::Category::GradedWrapperViolation>;
using probe_3 = diag::cheat_probe_type<cheats::Cheat3_LyingForwarders, diag::Category::GradedWrapperViolation>;

// The same facts again, stated directly. The probes above assert them
// through a layer of machinery, and these read without it.
static_assert(
    !diag::concept_gate<diag::Category::GradedWrapperViolation>::admits_type<cheats::Cheat1_ValueTypeMismatch>);
static_assert(!diag::concept_gate<diag::Category::GradedWrapperViolation>::admits_type<cheats::Cheat2_LatticeMismatch>);
static_assert(!diag::concept_gate<diag::Category::GradedWrapperViolation>::admits_type<cheats::Cheat3_LyingForwarders>);

int main() {
    // Reaching here means every cheat was rejected at compile time.
    return 0;
}
