// Each cheat below is a wrapper that satisfies the shape of the concept
// while lying about one of its projections. Rejecting all of them is what
// the build proves. They are rebuilt here rather than shared with the
// wider cheat suite, so this file stands alone and depends on nothing but
// the concept it checks.

#include <crucible/algebra/_GradedTrait.h>

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_QttSemiring.h>
#include <crucible/algebra/lattices/_BoolLattice.h>

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

// Each assertion states that the concept rejects its cheat, so the build
// succeeding is the whole claim.

static_assert(!::crucible::algebra::GradedWrapper<cheats::Cheat1_ValueTypeMismatch>);
static_assert(!::crucible::algebra::GradedWrapper<cheats::Cheat2_LatticeMismatch>);
static_assert(!::crucible::algebra::GradedWrapper<cheats::Cheat3_LyingForwarders>);

int main() {
    // Reaching here means every cheat was rejected at compile time.
    return 0;
}
