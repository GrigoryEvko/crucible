// Sentinel TU for the band, hardware-scope and clock lattices and the
// enum value pins.  Each header carries its own static_asserts and an
// inline runtime_smoke_test; this file includes each and calls each
// once.  EnumValuePins.h is included so its pins are compiled at all.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/EnumValuePins.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/PinningRequirementLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>

#include <type_traits>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;

// Every chain lattice here is built on the shared ChainLatticeOps base,
// so its element type is the scoped enum and its At<> sub-lattice is
// empty; a band wrapper over At<> costs sizeof(T).
static_assert(std::is_base_of_v<fl::ChainLatticeOps<fl::DetSafeTier>, fl::DetSafeLattice>);
static_assert(std::is_empty_v<fl::det_safe_tier::PureTier::element_type>);
static_assert(sizeof(fa::Graded<fa::ModalityKind::Absolute, fl::det_safe_tier::PureTier, int>) == sizeof(int));

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
    fl::detail::vendor_lattice_self_test::runtime_smoke_test();
    fl::detail::recipe_family_lattice_self_test::runtime_smoke_test();
    fl::detail::barrier_strength_lattice_self_test::runtime_smoke_test();
    fl::detail::memory_scope_lattice_self_test::runtime_smoke_test();
    fl::detail::product_lattice_self_test::runtime_smoke_test();
    fl::detail::pinning_requirement_lattice_self_test::runtime_smoke_test();
    fl::detail::suspend_behavior_lattice_self_test::runtime_smoke_test();
    fl::detail::clock_source_lattice_self_test::runtime_smoke_test();
    return 0;
}
