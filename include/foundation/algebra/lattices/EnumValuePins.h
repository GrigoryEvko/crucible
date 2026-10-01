// SPDX-License-Identifier: Apache-2.0
#pragma once

// The underlying values of the enums below are part of a format, not an
// implementation detail.  For most of them the value is the order of the
// lattice: ChainLatticeOps compares underlying values,
// BarrierStrengthLattice derives a height from them, and MemoryScope reads
// its chain from the high nibble.  A grade that a program stores at run
// time, such as the tier and the family of a RecipeSpec, holds the value
// too.  An enumerator that goes in before the end renumbers the ones after
// it, and the order and each stored grade then change meaning with no
// error.  A row hash cannot see such a change, because a pinned grade
// folds the name of its enumerator and not its value.  These tables are
// the witness.
//
// So a new enumerator takes the next free value, or for the enums that pack a
// group into the high nibble the next free value within its group, and extends
// the table for its enum in the same change.  A deliberate renumber is a format
// migration, and these assertions are what force that conversation before it
// ships.
//
// Each enum is one hand-written table and one call.  The table is the format,
// and foundation::reflect::pin_enum compares it with the enum.  The calls are
// in the check file of this header,
// test/layer/checks/foundation/algebra/lattices/EnumValuePins.cpp, so one
// translation unit evaluates them.
//
// The roster is the set of pinned enums itself.  A new lattice enum whose
// value reaches a cache key adds its own table below and its call in the
// check file.

#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/ResidencyHeatLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/reflect/EnumPins.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::algebra::lattices::detail::enum_value_pins {

using ::foundation::reflect::enum_pin;

template <class E, std::size_t N>
[[nodiscard]] consteval bool pin_lattice_enum(std::array<enum_pin<E>, N> const& expected) noexcept {
    static_assert(std::is_same_v<std::underlying_type_t<E>, std::uint8_t>,
                  "A pinned lattice enum must have uint8_t as its underlying type.  The pinned values reach "
                  "a cache key one byte at a time, so a wider enum would key on a value this table cannot "
                  "express.");
    return ::foundation::reflect::pin_enum(expected);
}

inline constexpr std::array<enum_pin<HotPathTier>, 3> hot_path_tier_pins{{{"Cold", 0}, {"Warm", 1}, {"Hot", 2}}};

inline constexpr std::array<enum_pin<DetSafeTier>, 7> det_safe_tier_pins{{{"NonDeterministicSyscall", 0},
                                                                          {"FilesystemMtime", 1},
                                                                          {"EntropyRead", 2},
                                                                          {"WallClockRead", 3},
                                                                          {"MonotonicClockRead", 4},
                                                                          {"PhiloxRng", 5},
                                                                          {"Pure", 6}}};

inline constexpr std::array<enum_pin<Tolerance>, 7> tolerance_pins{{{"RELAXED", 0},
                                                                    {"ULP_INT8", 1},
                                                                    {"ULP_FP8", 2},
                                                                    {"ULP_FP16", 3},
                                                                    {"ULP_FP32", 4},
                                                                    {"ULP_FP64", 5},
                                                                    {"BITEXACT", 6}}};

// None is the bottom sentinel and Portable the top one, which is why the
// last value is 255 rather than 7.
inline constexpr std::array<enum_pin<VendorBackend>, 8> vendor_backend_pins{
    {{"None", 0}, {"CPU", 1}, {"NV", 2}, {"AMD", 3}, {"TPU", 4}, {"TRN", 5}, {"CER", 6}, {"Portable", 255}}};

inline constexpr std::array<enum_pin<BarrierStrength>, 7> barrier_strength_pins{{{"None", 0},
                                                                                 {"CompilerBarrier", 1},
                                                                                 {"AcquireLoad", 2},
                                                                                 {"ReleaseStore", 3},
                                                                                 {"AcqRel", 4},
                                                                                 {"SeqCst", 5},
                                                                                 {"FullFence", 6}}};

// Thread is the bottom sentinel and System the top one.  The middle
// values pack a trunk into the high nibble: 0x1n is the GPU trunk and
// 0x2n the ARM-host trunk, so a new scope takes the next free value
// inside its own trunk.
inline constexpr std::array<enum_pin<MemoryScope>, 8> memory_scope_pins{{{"Thread", 0x00},
                                                                         {"Warp", 0x10},
                                                                         {"Cta", 0x11},
                                                                         {"Cluster", 0x12},
                                                                         {"Gpu", 0x13},
                                                                         {"Inner", 0x20},
                                                                         {"Outer", 0x21},
                                                                         {"System", 0xFF}}};

inline constexpr std::array<enum_pin<CipherTierTag>, 3> cipher_tier_tag_pins{{{"Cold", 0}, {"Warm", 1}, {"Hot", 2}}};

inline constexpr std::array<enum_pin<ResidencyHeatTag>, 3> residency_heat_tag_pins{
    {{"Cold", 0}, {"Warm", 1}, {"Hot", 2}}};

inline constexpr std::array<enum_pin<AllocClassTag>, 6> alloc_class_tag_pins{
    {{"HugePage", 0}, {"Mmap", 1}, {"Heap", 2}, {"Arena", 3}, {"Pool", 4}, {"Stack", 5}}};

inline constexpr std::array<enum_pin<WaitStrategy>, 6> wait_strategy_pins{
    {{"Block", 0}, {"Park", 1}, {"AcquireWait", 2}, {"UmwaitC01", 3}, {"BoundedSpin", 4}, {"SpinPause", 5}}};

inline constexpr std::array<enum_pin<SuspendBehavior>, 3> suspend_behavior_pins{
    {{"Unknown", 0}, {"PausesOnSuspend", 1}, {"KeepsTicking", 2}}};

inline constexpr std::array<enum_pin<ClockSource>, 10> clock_source_pins{{{"Realtime", 0},
                                                                          {"Monotonic", 1},
                                                                          {"MonotonicRaw", 2},
                                                                          {"Boot", 3},
                                                                          {"ThreadCpu", 4},
                                                                          {"ProcessCpu", 5},
                                                                          {"TscRaw", 6},
                                                                          {"TscSerialized", 7},
                                                                          {"PmuCounter", 8},
                                                                          {"PtpHwClock", 9}}};

inline constexpr std::array<enum_pin<Lifetime>, 3> lifetime_pins{
    {{"PER_REQUEST", 0}, {"PER_PROGRAM", 1}, {"PER_FLEET", 2}}};

// A recipe family is part of a KernelCache key: the (content_hash,
// device_capability) slot a compiled kernel lands in is derived from the
// NumericalRecipe pinned on it.  None is the bottom sentinel and Any the
// top one, so the last two values are 254 and 255, and a new family
// takes the next free value after BlockStable.
inline constexpr std::array<enum_pin<RecipeFamily>, 6> recipe_family_pins{
    {{"Linear", 0}, {"Pairwise", 1}, {"Kahan", 2}, {"BlockStable", 3}, {"None", 254}, {"Any", 255}}};

}  // namespace foundation::algebra::lattices::detail::enum_value_pins
