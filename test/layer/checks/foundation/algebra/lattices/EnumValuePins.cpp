// The compile-time checks of foundation/algebra/lattices/EnumValuePins.h.

#include <foundation/algebra/lattices/EnumValuePins.h>

namespace foundation::algebra::lattices::detail::enum_value_pins {

static_assert(pin_lattice_enum(hot_path_tier_pins), "HotPathTier drifted from hot_path_tier_pins.");

static_assert(pin_lattice_enum(det_safe_tier_pins), "DetSafeTier drifted from det_safe_tier_pins.");

static_assert(pin_lattice_enum(tolerance_pins), "Tolerance drifted from tolerance_pins.");

static_assert(pin_lattice_enum(vendor_backend_pins), "VendorBackend drifted from vendor_backend_pins.");

static_assert(pin_lattice_enum(barrier_strength_pins), "BarrierStrength drifted from barrier_strength_pins.");

static_assert(pin_lattice_enum(memory_scope_pins), "MemoryScope drifted from memory_scope_pins.");

static_assert(pin_lattice_enum(cipher_tier_tag_pins), "CipherTierTag drifted from cipher_tier_tag_pins.");

static_assert(pin_lattice_enum(residency_heat_tag_pins), "ResidencyHeatTag drifted from residency_heat_tag_pins.");

static_assert(pin_lattice_enum(alloc_class_tag_pins), "AllocClassTag drifted from alloc_class_tag_pins.");

static_assert(pin_lattice_enum(wait_strategy_pins), "WaitStrategy drifted from wait_strategy_pins.");

static_assert(pin_lattice_enum(suspend_behavior_pins), "SuspendBehavior drifted from suspend_behavior_pins.");

static_assert(pin_lattice_enum(clock_source_pins), "ClockSource drifted from clock_source_pins.");

static_assert(pin_lattice_enum(lifetime_pins), "Lifetime drifted from lifetime_pins.");

static_assert(pin_lattice_enum(recipe_family_pins), "RecipeFamily drifted from recipe_family_pins.");

}  // namespace foundation::algebra::lattices::detail::enum_value_pins
