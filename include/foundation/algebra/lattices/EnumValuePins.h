// SPDX-License-Identifier: Apache-2.0
#pragma once

// The underlying value of each enumerator below is folded into the hash that
// keys a shared cache slot, so those values are part of a persisted format
// rather than an implementation detail.  Inserting an enumerator anywhere but
// at the end silently renumbers the ones after it, which changes the hash of
// every instantiation that mentions the enum, invalidates the slots those
// hashes name, and breaks the match between builds.
//
// So a new enumerator takes the next free value, or for the enums that pack a
// group into the high nibble the next free value within its group, and extends
// the table for its enum in the same change.  A deliberate renumber is a format
// migration, and these assertions are what force that conversation before it
// ships.
//
// Each enum is one hand-written table and one call.  The table is the format:
// it is written here, by hand, and never derived from the enum, because a
// derived table would agree with any renumber and pin nothing.  The call is
// what compares the two.
//
// Only enums whose lattice was extracted to foundation are pinned here.
// The roster is the set of pinned enums itself; a new lattice enum whose
// value reaches a cache key adds its own table and call below.

#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices::detail::enum_value_pins {

// One expected enumerator: the identifier as it is written, and the value
// the persisted format pins it to.
struct enum_pin {
    std::string_view name;
    std::uint8_t value;
};

// True when the enumerators of E are exactly the entries of `expected`.
//
// The walk runs over the enum and looks each enumerator up by identifier,
// so a renamed enumerator and a moved value are both caught.  The size
// comparison catches the other direction, an entry that no enumerator
// answers to, which the walk alone cannot see.  The distinctness pass
// catches a table that names one enumerator twice, which would leave a
// second entry unread while the sizes still agreed.
//
// A failure says which enum drifted and not which enumerator, where the
// twelve hand-written assertion blocks this replaces said both.  The
// table sits next to the call, so the comparison the reader has to make
// is on one screen.
template <class E>
[[nodiscard]] consteval bool pin_enum(std::span<const enum_pin> expected) noexcept {
    static_assert(std::is_same_v<std::underlying_type_t<E>, std::uint8_t>,
                  "A pinned enum must have uint8_t as its underlying type.  The pinned values reach a "
                  "cache key one byte at a time, so a wider enum would key on a value this table cannot "
                  "express.");

    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
    if (enumerators.size() != expected.size()) return false;

    for (std::size_t i = 0; i < expected.size(); ++i) {
        for (std::size_t j = i + 1; j < expected.size(); ++j) {
            if (expected[i].name == expected[j].name) return false;
        }
    }

    bool pinned = true;
// An expansion statement unrolls into successive scopes that each
// declare the same induction variable, so -Wshadow fires once per
// iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr std::string_view name = std::meta::identifier_of(en);
        constexpr auto value = static_cast<std::uint8_t>([:en:]);
        bool matched = false;
        for (const enum_pin& pin : expected) {
            if (pin.name == name) {
                matched = pin.value == value;
                break;
            }
        }
        pinned = pinned && matched;
    }
#pragma GCC diagnostic pop
    return pinned;
}

inline constexpr std::array<enum_pin, 3> hot_path_tier_pins{{{"Cold", 0}, {"Warm", 1}, {"Hot", 2}}};
static_assert(pin_enum<HotPathTier>(hot_path_tier_pins), "HotPathTier drifted from hot_path_tier_pins.");

inline constexpr std::array<enum_pin, 7> det_safe_tier_pins{{{"NonDeterministicSyscall", 0},
                                                             {"FilesystemMtime", 1},
                                                             {"EntropyRead", 2},
                                                             {"WallClockRead", 3},
                                                             {"MonotonicClockRead", 4},
                                                             {"PhiloxRng", 5},
                                                             {"Pure", 6}}};
static_assert(pin_enum<DetSafeTier>(det_safe_tier_pins), "DetSafeTier drifted from det_safe_tier_pins.");

inline constexpr std::array<enum_pin, 7> tolerance_pins{{{"RELAXED", 0},
                                                         {"ULP_INT8", 1},
                                                         {"ULP_FP8", 2},
                                                         {"ULP_FP16", 3},
                                                         {"ULP_FP32", 4},
                                                         {"ULP_FP64", 5},
                                                         {"BITEXACT", 6}}};
static_assert(pin_enum<Tolerance>(tolerance_pins), "Tolerance drifted from tolerance_pins.");

// None is the bottom sentinel and Portable the top one, which is why the
// last value is 255 rather than 7.
inline constexpr std::array<enum_pin, 8> vendor_backend_pins{
    {{"None", 0}, {"CPU", 1}, {"NV", 2}, {"AMD", 3}, {"TPU", 4}, {"TRN", 5}, {"CER", 6}, {"Portable", 255}}};
static_assert(pin_enum<VendorBackend>(vendor_backend_pins), "VendorBackend drifted from vendor_backend_pins.");

inline constexpr std::array<enum_pin, 7> barrier_strength_pins{{{"None", 0},
                                                                {"CompilerBarrier", 1},
                                                                {"AcquireLoad", 2},
                                                                {"ReleaseStore", 3},
                                                                {"AcqRel", 4},
                                                                {"SeqCst", 5},
                                                                {"FullFence", 6}}};
static_assert(pin_enum<BarrierStrength>(barrier_strength_pins), "BarrierStrength drifted from "
                                                                "barrier_strength_pins.");

// Thread is the bottom sentinel and System the top one.  The middle
// values pack a trunk into the high nibble: 0x1n is the GPU trunk and
// 0x2n the ARM-host trunk, so a new scope takes the next free value
// inside its own trunk.
inline constexpr std::array<enum_pin, 8> memory_scope_pins{{{"Thread", 0x00},
                                                            {"Warp", 0x10},
                                                            {"Cta", 0x11},
                                                            {"Cluster", 0x12},
                                                            {"Gpu", 0x13},
                                                            {"Inner", 0x20},
                                                            {"Outer", 0x21},
                                                            {"System", 0xFF}}};
static_assert(pin_enum<MemoryScope>(memory_scope_pins), "MemoryScope drifted from memory_scope_pins.");

inline constexpr std::array<enum_pin, 3> cipher_tier_tag_pins{{{"Cold", 0}, {"Warm", 1}, {"Hot", 2}}};
static_assert(pin_enum<CipherTierTag>(cipher_tier_tag_pins), "CipherTierTag drifted from cipher_tier_tag_pins.");

inline constexpr std::array<enum_pin, 6> alloc_class_tag_pins{
    {{"HugePage", 0}, {"Mmap", 1}, {"Heap", 2}, {"Arena", 3}, {"Pool", 4}, {"Stack", 5}}};
static_assert(pin_enum<AllocClassTag>(alloc_class_tag_pins), "AllocClassTag drifted from alloc_class_tag_pins.");

inline constexpr std::array<enum_pin, 6> wait_strategy_pins{
    {{"Block", 0}, {"Park", 1}, {"AcquireWait", 2}, {"UmwaitC01", 3}, {"BoundedSpin", 4}, {"SpinPause", 5}}};
static_assert(pin_enum<WaitStrategy>(wait_strategy_pins), "WaitStrategy drifted from wait_strategy_pins.");

inline constexpr std::array<enum_pin, 3> suspend_behavior_pins{
    {{"Unknown", 0}, {"PausesOnSuspend", 1}, {"KeepsTicking", 2}}};
static_assert(pin_enum<SuspendBehavior>(suspend_behavior_pins), "SuspendBehavior drifted from "
                                                                "suspend_behavior_pins.");

inline constexpr std::array<enum_pin, 10> clock_source_pins{{{"Realtime", 0},
                                                             {"Monotonic", 1},
                                                             {"MonotonicRaw", 2},
                                                             {"Boot", 3},
                                                             {"ThreadCpu", 4},
                                                             {"ProcessCpu", 5},
                                                             {"TscRaw", 6},
                                                             {"TscSerialized", 7},
                                                             {"PmuCounter", 8},
                                                             {"PtpHwClock", 9}}};
static_assert(pin_enum<ClockSource>(clock_source_pins), "ClockSource drifted from clock_source_pins.");

// Lifetime reached foundation with the OpaqueLifetime band.  The old
// tree carried no pins for it; these pin the values it arrived with.
inline constexpr std::array<enum_pin, 3> lifetime_pins{{{"PER_REQUEST", 0}, {"PER_PROGRAM", 1}, {"PER_FLEET", 2}}};
static_assert(pin_enum<Lifetime>(lifetime_pins), "Lifetime drifted from lifetime_pins.");

// A recipe family is part of a KernelCache key: the (content_hash,
// device_capability) slot a compiled kernel lands in is derived from the
// NumericalRecipe pinned on it.  The old tree never pinned this enum;
// these pin the values it arrived with.  None is the bottom sentinel and
// Any the top one, so the last two values are 254 and 255, and a new
// family takes the next free value after BlockStable.
inline constexpr std::array<enum_pin, 6> recipe_family_pins{
    {{"Linear", 0}, {"Pairwise", 1}, {"Kahan", 2}, {"BlockStable", 3}, {"None", 254}, {"Any", 255}}};
static_assert(pin_enum<RecipeFamily>(recipe_family_pins), "RecipeFamily drifted from recipe_family_pins.");

// The walk answers no for each way a table and its enum can disagree.
// Without these, a pin_enum that answered yes for everything would leave
// all thirteen assertions above green and pin nothing.
namespace pin_enum_self_test {

enum class Probe : std::uint8_t {
    First = 0,
    Second = 1,
    Third = 2
};

inline constexpr std::array<enum_pin, 3> correct{{{"First", 0}, {"Second", 1}, {"Third", 2}}};
static_assert(pin_enum<Probe>(correct));

inline constexpr std::array<enum_pin, 3> value_moved{{{"First", 0}, {"Second", 9}, {"Third", 2}}};
static_assert(!pin_enum<Probe>(value_moved), "A moved value must be caught.");

inline constexpr std::array<enum_pin, 3> renamed{{{"First", 0}, {"Deuxieme", 1}, {"Third", 2}}};
static_assert(!pin_enum<Probe>(renamed), "A renamed enumerator must be caught.");

inline constexpr std::array<enum_pin, 2> entry_missing{{{"First", 0}, {"Second", 1}}};
static_assert(!pin_enum<Probe>(entry_missing), "An enumerator the table does not name must be caught.");

inline constexpr std::array<enum_pin, 4> entry_extra{{{"First", 0}, {"Second", 1}, {"Third", 2}, {"Fourth", 3}}};
static_assert(!pin_enum<Probe>(entry_extra), "An entry no enumerator answers to must be caught.");

inline constexpr std::array<enum_pin, 3> duplicated{{{"First", 0}, {"First", 0}, {"Third", 2}}};
static_assert(!pin_enum<Probe>(duplicated), "A table that names one enumerator twice must be caught, because "
                                            "the sizes still agree and one entry goes unread.");

}  // namespace pin_enum_self_test

}  // namespace foundation::algebra::lattices::detail::enum_value_pins
