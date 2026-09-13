#pragma once

#include <crucible/safety/diag/TestRegistry.h>

#include <cstdint>
#include <string_view>

namespace crucible::safety::diag {

// An unregistered id is Active by default. A witness that pins no
// particular run must stay valid. Only a registered id can be revoked or
// carry an expiry.

template <auto Id>
struct CiRunEntry final {
    static constexpr auto id_v = Id;
    static constexpr std::string_view name{"<unregistered>"};
    static constexpr std::string_view ci_run_url{""};
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t expiry_epoch = 0;
};

template <auto Id>
inline constexpr bool is_valid_ci_run_v = CiRunEntry<Id>::status == WitnessStatus::Active;

inline constexpr std::uint64_t UnnamedCiRunId = 0;

namespace ci_id {

inline constexpr std::uint64_t fixy_cross_vendor_smoke = 0xCFE055AA5750C001ULL;
inline constexpr std::uint64_t fixy_aarch64_x86_pairwise = 0xA41AACEA5750A887ULL;

inline constexpr std::uint64_t fixy_revoked_ci_demo = 0xBAD0C1BAD0C1BADULL;

}  // namespace ci_id

template <>
struct CiRunEntry<ci_id::fixy_cross_vendor_smoke> final {
    static constexpr auto id_v = ci_id::fixy_cross_vendor_smoke;
    static constexpr std::string_view name = "fixy_cross_vendor_smoke";
    static constexpr std::string_view ci_run_url = "internal://ci/fixy_cross_vendor_smoke/latest";
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t expiry_epoch = 0;
};

template <>
struct CiRunEntry<ci_id::fixy_aarch64_x86_pairwise> final {
    static constexpr auto id_v = ci_id::fixy_aarch64_x86_pairwise;
    static constexpr std::string_view name = "fixy_aarch64_x86_pairwise";
    static constexpr std::string_view ci_run_url = "internal://ci/fixy_aarch64_x86_pairwise/latest";
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t expiry_epoch = 0;
};

template <>
struct CiRunEntry<ci_id::fixy_revoked_ci_demo> final {
    static constexpr auto id_v = ci_id::fixy_revoked_ci_demo;
    static constexpr std::string_view name = "fixy_revoked_ci_demo (synthetic)";
    static constexpr std::string_view ci_run_url = "<synthetic>";
    static constexpr WitnessStatus status = WitnessStatus::Revoked;
    static constexpr std::uint64_t expiry_epoch = 0;
};

namespace ci_run_registry_self_test {

static_assert(CiRunEntry<UnnamedCiRunId>::status == WitnessStatus::Active);
static_assert(is_valid_ci_run_v<UnnamedCiRunId>);

static_assert(is_valid_ci_run_v<ci_id::fixy_cross_vendor_smoke>);
static_assert(is_valid_ci_run_v<ci_id::fixy_aarch64_x86_pairwise>);

static_assert(CiRunEntry<ci_id::fixy_revoked_ci_demo>::status == WitnessStatus::Revoked);
static_assert(!is_valid_ci_run_v<ci_id::fixy_revoked_ci_demo>);

}  // namespace ci_run_registry_self_test

}  // namespace crucible::safety::diag
