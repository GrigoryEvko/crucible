#pragma once

// GAPS-147. Typed SR-IOV / virtual-function management substrate.
//
// This header owns admission for physical-NIC -> VF partitioning intent. It
// does not perform privileged kernel mutation or invent live vendor behavior.
// Privileged Linux backends consume DeclaredSrIovPlan values and currently
// report explicit deferral/unavailability.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/cntp/Pacing.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cog::sriov {

enum class SrIovError : std::uint8_t {
    None = 0,
    ZeroCog = 1,
    NonNicCog = 2,
    MissingSrIovCapability = 3,
    InvalidInterfaceName = 4,
    InvalidVfCount = 5,
    InvalidVfIndex = 6,
    InvalidMac = 7,
    InvalidVlan = 8,
    InvalidRateLimit = 9,
    InvalidResourceLimit = 10,
    VfIndexOutOfRange = 11,
    InsufficientHandleCapacity = 12,
    PrivilegedApplyDeferred = 13,
    PrivilegedBackendUnavailable = 14,
    QueryDeferred = 15,
};

[[nodiscard]] std::string_view sriov_error_name(SrIovError error) noexcept;

using VfCount =
    safety::Bounded<std::uint16_t{1}, std::uint16_t{4096}, std::uint16_t>;
using VfIndex =
    safety::Bounded<std::uint16_t{0}, std::uint16_t{4095}, std::uint16_t>;
using VfVlanId =
    safety::Bounded<std::uint16_t{0}, std::uint16_t{4094}, std::uint16_t>;
using VfRateLimitMbps =
    safety::Bounded<std::uint64_t{0}, std::uint64_t{1'000'000'000ull},
                    std::uint64_t>;
using VfResourceLimit =
    safety::Bounded<std::uint32_t{0}, std::uint32_t{1'000'000},
                    std::uint32_t>;

struct MacAddress {
    std::array<std::uint8_t, 6> bytes{};

    [[nodiscard]] static constexpr MacAddress
    locally_administered(std::uint8_t suffix) noexcept {
        return MacAddress{{0x02u, 0x00u, 0x00u, 0x00u, 0x00u, suffix}};
    }

    [[nodiscard]] constexpr bool is_zero() const noexcept {
        for (const std::uint8_t b : bytes) {
            if (b != 0u) return false;
        }
        return true;
    }

    [[nodiscard]] constexpr bool is_multicast() const noexcept {
        return (bytes[0] & 0x01u) != 0u;
    }

    constexpr auto operator<=>(MacAddress const&) const noexcept = default;
};

inline constexpr auto vf_mac_valid = [](MacAddress mac) constexpr noexcept {
    return !mac.is_zero() && !mac.is_multicast();
};

using VfMacAddress = safety::Refined<vf_mac_valid, MacAddress>;

struct VfConfig {
    VfMacAddress mac{
        MacAddress::locally_administered(1), typename VfMacAddress::Trusted{}};
    VfVlanId vlan{std::uint16_t{0}, typename VfVlanId::Trusted{}};
    VfRateLimitMbps rate_limit_mbps{
        std::uint64_t{0}, typename VfRateLimitMbps::Trusted{}};
    VfResourceLimit max_qps{
        std::uint32_t{0}, typename VfResourceLimit::Trusted{}};
    VfResourceLimit max_mrs{
        std::uint32_t{0}, typename VfResourceLimit::Trusted{}};
    bool spoofchk = true;
};

struct SrIovPlan {
    CogIdentity physical{};
    cntp::NicInterfaceName interface{};
    VfCount num_vfs{std::uint16_t{1}};
    VfConfig default_vf{};
    bool allow_privileged_apply = false;
};

struct VfHandle {
    VfIndex index{std::uint16_t{0}, typename VfIndex::Trusted{}};
    Uuid parent_uuid{};
    CogIdentity identity{};
};

using DeclaredVfConfig =
    safety::Tagged<VfConfig, safety::source::SrIov>;
using DeclaredSrIovPlan =
    safety::Tagged<SrIovPlan, safety::source::SrIov>;

template <class Ctx>
concept CtxFitsSrIovMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

[[nodiscard]] constexpr std::expected<VfCount, SrIovError>
admit_vf_count(std::uint16_t count) noexcept {
    if (count == 0u || count > 4096u) {
        return std::unexpected(SrIovError::InvalidVfCount);
    }
    return VfCount{count, typename VfCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<VfIndex, SrIovError>
admit_vf_index(std::uint16_t index) noexcept {
    if (index > 4095u) {
        return std::unexpected(SrIovError::InvalidVfIndex);
    }
    return VfIndex{index, typename VfIndex::Trusted{}};
}

[[nodiscard]] constexpr std::expected<VfVlanId, SrIovError>
admit_vlan(std::uint16_t vlan) noexcept {
    if (vlan > 4094u) {
        return std::unexpected(SrIovError::InvalidVlan);
    }
    return VfVlanId{vlan, typename VfVlanId::Trusted{}};
}

[[nodiscard]] constexpr std::expected<VfRateLimitMbps, SrIovError>
admit_rate_limit_mbps(std::uint64_t rate) noexcept {
    if (rate > 1'000'000'000ull) {
        return std::unexpected(SrIovError::InvalidRateLimit);
    }
    return VfRateLimitMbps{rate, typename VfRateLimitMbps::Trusted{}};
}

[[nodiscard]] constexpr std::expected<VfResourceLimit, SrIovError>
admit_resource_limit(std::uint32_t limit) noexcept {
    if (limit > 1'000'000u) {
        return std::unexpected(SrIovError::InvalidResourceLimit);
    }
    return VfResourceLimit{limit, typename VfResourceLimit::Trusted{}};
}

[[nodiscard]] constexpr std::expected<VfMacAddress, SrIovError>
admit_mac(MacAddress mac) noexcept {
    if (!vf_mac_valid(mac)) {
        return std::unexpected(SrIovError::InvalidMac);
    }
    return VfMacAddress{mac, typename VfMacAddress::Trusted{}};
}

[[nodiscard]] constexpr bool
interface_name_present(cntp::NicInterfaceName interface) noexcept {
    return !interface.view().empty();
}

[[nodiscard]] constexpr std::expected<void, SrIovError>
validate_physical(CogIdentity physical, NicPortTargetCaps const& caps) noexcept {
    if (physical.uuid.is_zero()) {
        return std::unexpected(SrIovError::ZeroCog);
    }
    if (physical.kind != CogKind::NicPort) {
        return std::unexpected(SrIovError::NonNicCog);
    }
    if (!caps.features.test(NicFeature::SrIov)) {
        return std::unexpected(SrIovError::MissingSrIovCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, SrIovError>
validate_vf_config(VfConfig config) noexcept {
    if (!vf_mac_valid(config.mac.value())) {
        return std::unexpected(SrIovError::InvalidMac);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, SrIovError>
validate_plan(SrIovPlan const& plan, NicPortTargetCaps const& caps) noexcept {
    auto physical = validate_physical(plan.physical, caps);
    if (!physical.has_value()) {
        return std::unexpected(physical.error());
    }
    if (!interface_name_present(plan.interface)) {
        return std::unexpected(SrIovError::InvalidInterfaceName);
    }
    return validate_vf_config(plan.default_vf);
}

[[nodiscard]] constexpr std::uint64_t
mix_vf_uuid(std::uint64_t x) noexcept {
    x ^= x >> 33u;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33u;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33u;
    return x;
}

[[nodiscard]] constexpr CogIdentity
derive_vf_identity(CogIdentity physical, VfIndex index) noexcept {
    CogIdentity vf{};
    vf.uuid = Uuid{
        mix_vf_uuid(physical.uuid.hi ^ (0x5352494fULL << 16u) ^ index.value()),
        mix_vf_uuid(physical.uuid.lo ^ 0x56465f434f47ULL ^ index.value())};
    vf.level = CogLevel::L0_Atomic;
    vf.kind = CogKind::NicPort;
    vf.vendor = physical.vendor;
    vf.model = physical.model;
    vf.firmware_revision = physical.firmware_revision;
    vf.bios_revision = physical.bios_revision;
    return vf;
}

[[nodiscard]] constexpr VfHandle
make_vf_handle(CogIdentity physical, VfIndex index) noexcept {
    return VfHandle{
        .index = index,
        .parent_uuid = physical.uuid,
        .identity = derive_vf_identity(physical, index),
    };
}

template <class Ctx>
    requires CtxFitsSrIovMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredSrIovPlan, SrIovError>
mint_sriov_plan(Ctx const&,
                CogIdentity physical,
                NicPortTargetCaps caps,
                cntp::NicInterfaceName interface,
                VfCount num_vfs,
                VfConfig default_vf = {},
                bool allow_privileged_apply = false) noexcept {
    SrIovPlan plan{
        .physical = physical,
        .interface = interface,
        .num_vfs = num_vfs,
        .default_vf = default_vf,
        .allow_privileged_apply = allow_privileged_apply,
    };
    auto valid = validate_plan(plan, caps);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return DeclaredSrIovPlan{plan};
}

[[nodiscard]] constexpr DeclaredVfConfig
declare_vf_config(VfConfig config) noexcept {
    return DeclaredVfConfig{config};
}

[[nodiscard]] constexpr std::expected<std::span<VfHandle>, SrIovError>
materialize_vf_handles(DeclaredSrIovPlan plan,
                       std::span<VfHandle> out) noexcept {
    const std::uint16_t count = plan.value().num_vfs.value();
    if (out.size() < count) {
        return std::unexpected(SrIovError::InsufficientHandleCapacity);
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        out[i] = make_vf_handle(
            plan.value().physical, VfIndex{i, typename VfIndex::Trusted{}});
    }
    return out.first(count);
}

[[nodiscard]] constexpr std::expected<VfHandle, SrIovError>
vf_handle_at(DeclaredSrIovPlan plan, VfIndex index) noexcept {
    if (index.value() >= plan.value().num_vfs.value()) {
        return std::unexpected(SrIovError::VfIndexOutOfRange);
    }
    return make_vf_handle(plan.value().physical, index);
}

class SrIovManager : public safety::Pinned<SrIovManager> {
public:
    SrIovManager() = default;

    [[nodiscard]] std::expected<std::span<VfHandle>, SrIovError>
    enable(DeclaredSrIovPlan plan, std::span<VfHandle> out) noexcept;

    [[nodiscard]] std::expected<void, SrIovError>
    configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept;

    [[nodiscard]] std::expected<void, SrIovError>
    disable(DeclaredSrIovPlan plan) noexcept;
};

[[nodiscard]] std::expected<std::span<VfHandle>, SrIovError>
enable(DeclaredSrIovPlan plan, std::span<VfHandle> out) noexcept;
[[nodiscard]] std::expected<void, SrIovError>
configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept;
[[nodiscard]] std::expected<void, SrIovError>
disable(DeclaredSrIovPlan plan) noexcept;
[[nodiscard]] std::expected<DeclaredSrIovPlan, SrIovError>
query_current(CogIdentity physical, cntp::NicInterfaceName interface) noexcept;

static_assert(sizeof(VfCount) == sizeof(std::uint16_t));
static_assert(sizeof(VfIndex) == sizeof(std::uint16_t));
static_assert(sizeof(VfVlanId) == sizeof(std::uint16_t));
static_assert(sizeof(VfRateLimitMbps) == sizeof(std::uint64_t));
static_assert(sizeof(VfResourceLimit) == sizeof(std::uint32_t));
static_assert(sizeof(VfMacAddress) == sizeof(MacAddress));
static_assert(sizeof(DeclaredSrIovPlan) == sizeof(SrIovPlan));
static_assert(CtxFitsSrIovMint<effects::ColdInitCtx>);
static_assert(!CtxFitsSrIovMint<effects::BgDrainCtx>);
static_assert(std::is_trivially_copyable_v<MacAddress>);
static_assert(std::is_trivially_copyable_v<VfConfig>);
static_assert(std::is_trivially_copyable_v<VfHandle>);
static_assert(std::is_trivially_copyable_v<SrIovPlan>);

}  // namespace crucible::cog::sriov
