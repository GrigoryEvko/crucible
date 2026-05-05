#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>

struct BadTierWrapper {
    using value_type = int;
    using lattice_type = ::crucible::algebra::lattices::QttSemiring::At<
        ::crucible::algebra::lattices::QttGrade::One>;
    using graded_type = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        int>;

    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }

    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

namespace crucible::safety {

template <>
struct wrapper_dimension<::BadTierWrapper>
    : std::integral_constant<DimensionAxis, DimensionAxis::Protocol> {};

}  // namespace crucible::safety

static_assert(::crucible::safety::verify_quadruple<::BadTierWrapper>(),
    "QUADRUPLE_TIER_MISMATCH");

int main() { return 0; }
