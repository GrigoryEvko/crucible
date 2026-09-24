#pragma once

// A build that overrides these constants must supply the lowest cache budget
// across the fleet the binary will run on. Cost-model decisions derived from
// them stay sound only for the worst-case host.
//
// When the build host differs from the deploy host, the override has to be
// stated explicitly. Reading the build host's own cache topology would bake
// the wrong silicon's numbers into the binary.

#include <crucible/concurrent/SubstrateCtxFit.h>

#include <cstddef>

namespace crucible::concurrent::topology_constexpr {

#ifdef CRUCIBLE_L1D_PER_CORE_BYTES
inline constexpr std::size_t l1d_per_core_bytes_v = static_cast<std::size_t>(CRUCIBLE_L1D_PER_CORE_BYTES);
#else
inline constexpr std::size_t l1d_per_core_bytes_v = conservative_l1d_per_core;
#endif

#ifdef CRUCIBLE_L2_PER_CORE_BYTES
inline constexpr std::size_t l2_per_core_bytes_v = static_cast<std::size_t>(CRUCIBLE_L2_PER_CORE_BYTES);
#else
inline constexpr std::size_t l2_per_core_bytes_v = conservative_l2_per_core;
#endif

#ifdef CRUCIBLE_L3_TOTAL_BYTES
inline constexpr std::size_t l3_total_bytes_v = static_cast<std::size_t>(CRUCIBLE_L3_TOTAL_BYTES);
#else
inline constexpr std::size_t l3_total_bytes_v = conservative_l3_total;
#endif

static_assert(l1d_per_core_bytes_v > 0, "l1d_per_core_bytes_v must be greater than zero — check the "
                                        "CRUCIBLE_L1D_PER_CORE_BYTES override value.");
static_assert(l2_per_core_bytes_v > 0, "l2_per_core_bytes_v must be greater than zero — check the "
                                       "CRUCIBLE_L2_PER_CORE_BYTES override value.");
static_assert(l3_total_bytes_v > 0, "l3_total_bytes_v must be greater than zero — check the "
                                    "CRUCIBLE_L3_TOTAL_BYTES override value.");
static_assert(l1d_per_core_bytes_v < l2_per_core_bytes_v, "l1d_per_core_bytes_v must be less than "
                                                          "l2_per_core_bytes_v — an override that fails this has "
                                                          "most likely confused KB with MB.");
static_assert(l2_per_core_bytes_v < l3_total_bytes_v, "l2_per_core_bytes_v must be less than l3_total_bytes_v — an "
                                                      "override that fails this has most likely confused KB, MB "
                                                      "and GB.");

#ifdef CRUCIBLE_L1D_PER_CORE_BYTES
inline constexpr bool is_l1d_overridden_v = true;
#else
inline constexpr bool is_l1d_overridden_v = false;
#endif

#ifdef CRUCIBLE_L2_PER_CORE_BYTES
inline constexpr bool is_l2_overridden_v = true;
#else
inline constexpr bool is_l2_overridden_v = false;
#endif

#ifdef CRUCIBLE_L3_TOTAL_BYTES
inline constexpr bool is_l3_overridden_v = true;
#else
inline constexpr bool is_l3_overridden_v = false;
#endif

static_assert(is_l1d_overridden_v || l1d_per_core_bytes_v == conservative_l1d_per_core,
              "with no override in force, l1d_per_core_bytes_v must equal the conservative "
              "substrate default it is derived from.");
static_assert(is_l2_overridden_v || l2_per_core_bytes_v == conservative_l2_per_core,
              "with no override in force, l2_per_core_bytes_v must equal the conservative "
              "substrate default it is derived from.");
static_assert(is_l3_overridden_v || l3_total_bytes_v == conservative_l3_total,
              "with no override in force, l3_total_bytes_v must equal the conservative "
              "substrate default it is derived from.");

}  // namespace crucible::concurrent::topology_constexpr
