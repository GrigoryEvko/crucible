// Sentinel TU for the band, hardware-scope and clock lattices and the
// enum value pins.  Each header carries its own static_asserts and an
// inline runtime_smoke_test; this file includes each and calls each
// once.  EnumValuePins.h is included so its pins are compiled at all.
//
// The tiers are walked by reflection rather than listed by hand, so a
// new enumerator is covered the moment it is declared.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/EnumValuePins.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/PinningRequirementLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdio>
#include <meta>
#include <string_view>
#include <type_traits>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fr = ::foundation::reflect;

struct TwoWords {
    unsigned long long lo{0};
    unsigned long long hi{0};
};

// Every chain lattice here is built on the shared ChainLatticeOps base,
// so its element type is the scoped enum and its At<> sub-lattice is
// empty; a band wrapper over At<> costs sizeof(T) at every tier, not
// only at the one a hand-written cell happened to name.
template <typename L, typename E = typename L::element_type>
[[nodiscard]] consteval bool every_tier_collapses() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using At = typename L::template At<([:en:])>;
        if (!std::is_empty_v<typename At::element_type>) return false;
        if (sizeof(fa::Graded<fa::ModalityKind::Absolute, At, int>) != sizeof(int)) return false;
        if (sizeof(fa::Graded<fa::ModalityKind::Absolute, At, TwoWords>) != sizeof(TwoWords)) return false;
        if (alignof(fa::Graded<fa::ModalityKind::Absolute, At, TwoWords>) != alignof(TwoWords)) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(std::is_base_of_v<fl::ChainLatticeOps<fl::DetSafeTier>, fl::DetSafeLattice>);
static_assert(every_tier_collapses<fl::DetSafeLattice>());
static_assert(every_tier_collapses<fl::AllocClassLattice>());
static_assert(every_tier_collapses<fl::HotPathLattice>());
static_assert(every_tier_collapses<fl::CipherTierLattice>());
static_assert(every_tier_collapses<fl::ToleranceLattice>());
static_assert(every_tier_collapses<fl::WaitLattice>());
static_assert(every_tier_collapses<fl::LifetimeLattice>());
static_assert(every_tier_collapses<fl::BarrierStrengthLattice>());
static_assert(every_tier_collapses<fl::SuspendBehaviorLattice>());
static_assert(every_tier_collapses<fl::PinningRequirementLattice>());
static_assert(every_tier_collapses<fl::VendorLattice>());
static_assert(every_tier_collapses<fl::MemoryScopeLattice>());
static_assert(every_tier_collapses<fl::ClockSourceLattice, fl::ClockSource>());

// The reflected At name is the lattice name, "::At<", the enumerator
// identifier and ">", at every tier.
template <typename L, typename E = typename L::element_type>
[[nodiscard]] consteval bool every_at_name_is_reflected() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using At = typename L::template At<([:en:])>;
        const std::string_view name = At::name();
        if (!name.starts_with(L::name())) return false;
        if (!name.ends_with(">")) return false;
        const std::string_view middle = name.substr(L::name().size());
        if (!middle.starts_with("::At<")) return false;
        if (middle.substr(5, middle.size() - 6) != fr::enum_name([:en:])) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_at_name_is_reflected<fl::DetSafeLattice>());
static_assert(every_at_name_is_reflected<fl::ToleranceLattice>());
static_assert(every_at_name_is_reflected<fl::LifetimeLattice>());
static_assert(every_at_name_is_reflected<fl::VendorLattice>());
static_assert(every_at_name_is_reflected<fl::ClockSourceLattice, fl::ClockSource>());

// The clock lattice is a product of three axes and its At<> pins the
// source only; the projected point is a free function.
static_assert(fl::ClockSourceLattice::arity == 3);
static_assert(fa::BoundedLattice<fl::ClockSourceLattice>);
static_assert(fl::ClockSourceLattice::get<0>(fl::clock_source_project(fl::ClockSource::TscSerialized))
              == fl::DetSafeTier::MonotonicClockRead);

}  // namespace

int main() {
    fl::detail::chain_lattice_self_test::runtime_smoke_test();
    fl::detail::det_safe_lattice_self_test::runtime_smoke_test();
    fl::detail::alloc_class_lattice_self_test::runtime_smoke_test();
    fl::detail::hot_path_lattice_self_test::runtime_smoke_test();
    fl::detail::cipher_tier_lattice_self_test::runtime_smoke_test();
    fl::detail::tolerance_lattice_self_test::runtime_smoke_test();
    fl::detail::wait_lattice_self_test::runtime_smoke_test();
    fl::detail::lifetime_lattice_self_test::runtime_smoke_test();
    fl::detail::vendor_lattice_self_test::runtime_smoke_test();
    fl::detail::recipe_family_lattice_self_test::runtime_smoke_test();
    fl::detail::barrier_strength_lattice_self_test::runtime_smoke_test();
    fl::detail::memory_scope_lattice_self_test::runtime_smoke_test();
    fl::detail::product_lattice_self_test::runtime_smoke_test();
    fl::detail::pinning_requirement_lattice_self_test::runtime_smoke_test();
    fl::detail::suspend_behavior_lattice_self_test::runtime_smoke_test();
    fl::detail::clock_source_lattice_self_test::runtime_smoke_test();

    // The reflected names reach a runtime context too: the views point
    // at static storage, so a diagnostic can print them.
    volatile int raw_tier = 6;
    const auto tier = static_cast<fl::DetSafeTier>(raw_tier);
    if (fr::enum_name(tier) != "Pure") {
        std::fprintf(stderr, "test_lattices_bands: enum_name(DetSafeTier{6}) is not Pure\n");
        return 1;
    }
    volatile int raw_bad = 200;
    if (fr::enum_name(static_cast<fl::DetSafeTier>(raw_bad)) != "<unknown DetSafeTier>") {
        std::fprintf(stderr, "test_lattices_bands: enum_name(DetSafeTier{200}) is not the sentinel\n");
        return 1;
    }
    return 0;
}
