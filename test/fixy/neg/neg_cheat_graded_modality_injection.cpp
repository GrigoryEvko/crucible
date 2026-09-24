// The old cheat probe's Cheat 11 specialized graded_modality for a fake
// substrate, and the specialization was admitted: the primary was an
// unconstrained class template.  The primary is constrained to Graded
// now, so the specialization for a type that is not Graded is not
// merely inert, it does not compile.  This fixture stands on that.
//
// The second required diagnostic is the compiler's note naming the
// constraint and the type, `IsGraded<T>' [with T = FakeSubstrate]`,
// which the source never spells.

#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/QttSemiring.h>

#include <string_view>
#include <type_traits>

struct FakeSubstrate {
    using value_type = int;
    using lattice_type = ::foundation::algebra::lattices::QttSemiring::At<::foundation::algebra::lattices::QttGrade::One>;
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};

namespace foundation::algebra {
template <>
struct graded_modality<::FakeSubstrate> : std::integral_constant<ModalityKind, ModalityKind::Absolute> {};
}  // namespace foundation::algebra

int main() { return 0; }
