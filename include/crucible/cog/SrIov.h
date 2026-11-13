#pragma once

// Admission for partitioning a physical NIC into virtual functions.
// Nothing here mutates kernel state or guesses at vendor behaviour. A
// privileged backend consumes the declared plan this header mints.
//
// foundation::reflect::enum_name gives the name of each enumerator.

#include <crucible/cntp/Pacing.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <cstdint>
#include <type_traits>

namespace crucible::cog::sriov {

// Admission and validation are live. Every privileged apply and query
// function is a stub that reports PrivilegedApplyDeferred,
// PrivilegedBackendUnavailable or QueryDeferred. Setting this true
// means a backend exists that drives the real kernel interfaces, and
// the tests that assert the stubbed behaviour change with it.
inline constexpr bool privileged_apply_implemented = false;

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

// Zero virtual functions is the disabled state, which disable() reaches.
// An enable plan asks for at least one.
inline constexpr auto vf_count_bound = ::fixy::in_range<std::uint16_t{1}, std::uint16_t{4096}>;
inline constexpr auto vf_index_bound = ::fixy::in_range<std::uint16_t{0}, std::uint16_t{4095}>;
inline constexpr auto vf_vlan_bound = ::fixy::in_range<std::uint16_t{0}, std::uint16_t{4094}>;
inline constexpr auto vf_rate_limit_bound = ::fixy::in_range<std::uint64_t{0}, std::uint64_t{1000000000ull}>;
inline constexpr auto vf_resource_limit_bound = ::fixy::in_range<std::uint32_t{0}, std::uint32_t{1000000}>;

using VfCount = ::fixy::Refined<vf_count_bound, std::uint16_t>;
using VfIndex = ::fixy::Refined<vf_index_bound, std::uint16_t>;
using VfVlanId = ::fixy::Refined<vf_vlan_bound, std::uint16_t>;
using VfRateLimitMbps = ::fixy::Refined<vf_rate_limit_bound, std::uint64_t>;
using VfResourceLimit = ::fixy::Refined<vf_resource_limit_bound, std::uint32_t>;

inline constexpr VfIndex first_vf_index = ::fixy::mint_refined<vf_index_bound>(std::uint16_t{0});
inline constexpr VfResourceLimit no_resource_limit = ::fixy::mint_refined<vf_resource_limit_bound>(std::uint32_t{0});

struct MacAddress {
    ::foundation::core::FixedArray<std::uint8_t, 6> bytes{};

    [[nodiscard]] static constexpr MacAddress locally_administered(std::uint8_t suffix) noexcept {
        return MacAddress{{0x02u, 0x00u, 0x00u, 0x00u, 0x00u, suffix}};
    }

    [[nodiscard]] constexpr bool is_zero() const noexcept {
        for (const std::uint8_t octet : bytes) {
            if (octet != 0u) return false;
        }
        return true;
    }

    // The low bit of the first octet marks a group address.
    [[nodiscard]] constexpr bool is_multicast() const noexcept { return (bytes.at(::fixy::index<0>()) & 0x01u) != 0u; }

    [[nodiscard]] constexpr bool operator==(MacAddress const&) const noexcept = default;
};

// A virtual function takes a unicast identity. The zero address names no
// station, and a multicast address names a group rather than one port.
struct IsAssignableVfMac {
    [[nodiscard]] constexpr bool operator()(MacAddress const& mac) const noexcept {
        return !mac.is_zero() && !mac.is_multicast();
    }
};

inline constexpr IsAssignableVfMac vf_mac_valid{};

using VfMacAddress = ::fixy::Refined<vf_mac_valid, MacAddress>;

struct VfConfig {
    VfMacAddress mac = ::fixy::mint_refined<vf_mac_valid>(MacAddress::locally_administered(1));
    VfVlanId vlan = ::fixy::mint_refined<vf_vlan_bound>(std::uint16_t{0});
    VfRateLimitMbps rate_limit_mbps = ::fixy::mint_refined<vf_rate_limit_bound>(std::uint64_t{0});
    VfResourceLimit max_qps = no_resource_limit;
    VfResourceLimit max_mrs = no_resource_limit;
    bool spoofchk = true;
};

struct SrIovPlan {
    CogIdentity physical{};
    cntp::NicInterfaceName interface{};
    VfCount num_vfs = ::fixy::mint_refined<vf_count_bound>(std::uint16_t{1});
    VfConfig default_vf{};
    bool allow_privileged_apply = false;
};

using DeclaredVfConfig = ::fixy::Tagged<VfConfig, ::fixy::tags::source::SrIov>;
using DeclaredSrIovPlan = ::fixy::Tagged<SrIovPlan, ::fixy::tags::source::SrIov>;

// The two salts keep the upper and the lower half of a derived identity
// apart, so a virtual function never shares a half with its parent.
[[nodiscard]] constexpr CogIdentity derive_vf_identity(CogIdentity physical, VfIndex index) noexcept {
    CogIdentity vf{};
    vf.uuid = Uuid{::foundation::reflect::fmix64(physical.uuid.hi ^ (0x5352494fULL << 16u) ^ index.value()),
                   ::foundation::reflect::fmix64(physical.uuid.lo ^ 0x56465f434f47ULL ^ index.value())};
    vf.level = CogLevel::L0_Atomic;
    vf.kind = CogKind::NicPort;
    vf.vendor = physical.vendor;
    vf.model = physical.model;
    vf.firmware_revision = physical.firmware_revision;
    vf.bios_revision = physical.bios_revision;
    return vf;
}

class VfHandleDoor;

// A handle names one virtual function of a declared plan. Only the two
// functions materialize_vf_handles and vf_handle_at build one, from the
// plan, so a handle cannot name a function under a parent that no plan
// admitted.
class VfHandle {
public:
    // The empty slot of a handle buffer. It names no function, and
    // configure_vf refuses it.
    constexpr VfHandle() noexcept = default;

    [[nodiscard]] constexpr VfIndex index() const noexcept { return index_; }
    [[nodiscard]] constexpr Uuid parent_uuid() const noexcept { return parent_uuid_; }
    [[nodiscard]] constexpr CogIdentity identity() const noexcept { return identity_; }

private:
    constexpr VfHandle(CogIdentity physical, VfIndex index) noexcept
        : index_{index}, parent_uuid_{physical.uuid}, identity_{derive_vf_identity(physical, index)} {}

    friend class VfHandleDoor;

    VfIndex index_ = first_vf_index;
    Uuid parent_uuid_{};
    CogIdentity identity_{};
};

[[nodiscard]] constexpr ::fixy::Result<::fixy::View<VfHandle>, SrIovError>
materialize_vf_handles(DeclaredSrIovPlan plan, ::fixy::View<VfHandle> out) noexcept;

[[nodiscard]] constexpr ::fixy::Result<VfHandle, SrIovError> vf_handle_at(DeclaredSrIovPlan plan,
                                                                          VfIndex index) noexcept;

// The door to the private constructor of a handle.  A Result of a handle
// needs a complete VfHandle, so the class of the handle cannot name the
// two functions as friends.  This class names them, and only they build a
// handle through it.
class VfHandleDoor {
    [[nodiscard]] static constexpr VfHandle build(CogIdentity physical, VfIndex index) noexcept {
        return VfHandle{physical, index};
    }

    friend constexpr ::fixy::Result<::fixy::View<VfHandle>, SrIovError>
    materialize_vf_handles(DeclaredSrIovPlan plan, ::fixy::View<VfHandle> out) noexcept;
    friend constexpr ::fixy::Result<VfHandle, SrIovError> vf_handle_at(DeclaredSrIovPlan plan, VfIndex index) noexcept;
};

template <class Ctx>
concept CtxFitsSrIovMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

[[nodiscard]] constexpr ::fixy::Result<VfCount, SrIovError> admit_vf_count(std::uint16_t count) noexcept {
    return ::fixy::admit_refined<vf_count_bound>(count, SrIovError::InvalidVfCount);
}

[[nodiscard]] constexpr ::fixy::Result<VfIndex, SrIovError> admit_vf_index(std::uint16_t index) noexcept {
    return ::fixy::admit_refined<vf_index_bound>(index, SrIovError::InvalidVfIndex);
}

[[nodiscard]] constexpr ::fixy::Result<VfVlanId, SrIovError> admit_vlan(std::uint16_t vlan) noexcept {
    return ::fixy::admit_refined<vf_vlan_bound>(vlan, SrIovError::InvalidVlan);
}

[[nodiscard]] constexpr ::fixy::Result<VfRateLimitMbps, SrIovError> admit_rate_limit_mbps(std::uint64_t rate) noexcept {
    return ::fixy::admit_refined<vf_rate_limit_bound>(rate, SrIovError::InvalidRateLimit);
}

[[nodiscard]] constexpr ::fixy::Result<VfResourceLimit, SrIovError> admit_resource_limit(std::uint32_t limit) noexcept {
    return ::fixy::admit_refined<vf_resource_limit_bound>(limit, SrIovError::InvalidResourceLimit);
}

[[nodiscard]] constexpr ::fixy::Result<VfMacAddress, SrIovError> admit_mac(MacAddress mac) noexcept {
    return ::fixy::admit_refined<vf_mac_valid>(mac, SrIovError::InvalidMac);
}

[[nodiscard]] constexpr bool interface_name_present(cntp::NicInterfaceName interface) noexcept {
    return interface.view().size() != 0;
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, SrIovError>
validate_physical(CogIdentity physical, NicPortTargetCaps const& caps) noexcept {
    if (physical.uuid.is_zero()) {
        return ::fixy::err(SrIovError::ZeroCog);
    }
    if (physical.kind != CogKind::NicPort) {
        return ::fixy::err(SrIovError::NonNicCog);
    }
    if (!caps.features.test(NicFeature::SrIov)) {
        return ::fixy::err(SrIovError::MissingSrIovCapability);
    }
    return ::fixy::Unit{};
}

// Every field of a VfConfig is refined, so a plan needs no check of its
// default function beyond the checks of the physical port and the name.
[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, SrIovError> validate_plan(SrIovPlan const& plan,
                                                                               NicPortTargetCaps const& caps) noexcept {
    CRUCIBLE_TRY(_, validate_physical(plan.physical, caps));
    if (!interface_name_present(plan.interface)) {
        return ::fixy::err(SrIovError::InvalidInterfaceName);
    }
    return ::fixy::Unit{};
}

template <class Ctx>
    requires CtxFitsSrIovMint<Ctx>
[[nodiscard]] constexpr ::fixy::Result<DeclaredSrIovPlan, SrIovError>
mint_sriov_plan(Ctx const&, CogIdentity physical, NicPortTargetCaps caps, cntp::NicInterfaceName interface,
                VfCount num_vfs, VfConfig default_vf = {}, bool allow_privileged_apply = false) noexcept {
    SrIovPlan plan{
        .physical = physical,
        .interface = interface,
        .num_vfs = num_vfs,
        .default_vf = default_vf,
        .allow_privileged_apply = allow_privileged_apply,
    };
    CRUCIBLE_TRY(_, validate_plan(plan, caps));
    return ::fixy::mint_tagged<::fixy::tags::source::SrIov>(plan);
}

// Every field of a VfConfig is refined, so the refinements are the whole
// validation and the declaration only records the provenance.
[[nodiscard]] constexpr DeclaredVfConfig declare_vf_config(VfConfig const& config) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::SrIov>(config);
}

// The first num_vfs slots of out take the handles, and the call gives the
// View of those slots.  A View of fewer slots has no such window, so the
// call refuses it.  An index below num_vfs, which is at most 4096, is at
// most 4095, so the checked mint of each index passes.
[[nodiscard]] constexpr ::fixy::Result<::fixy::View<VfHandle>, SrIovError>
materialize_vf_handles(DeclaredSrIovPlan plan, ::fixy::View<VfHandle> out) noexcept {
    const std::uint16_t count = plan.value().num_vfs.value();
    CRUCIBLE_TRY(handles, out.window(0, count).ok_or(SrIovError::InsufficientHandleCapacity));
    std::uint16_t next_index = 0;
    for (VfHandle& handle : handles) {
        handle = VfHandleDoor::build(plan.value().physical, ::fixy::mint_refined<vf_index_bound>(next_index));
        ++next_index;
    }
    return handles;
}

[[nodiscard]] constexpr ::fixy::Result<VfHandle, SrIovError> vf_handle_at(DeclaredSrIovPlan plan,
                                                                          VfIndex index) noexcept {
    if (index.value() >= plan.value().num_vfs.value()) {
        return ::fixy::err(SrIovError::VfIndexOutOfRange);
    }
    return VfHandleDoor::build(plan.value().physical, index);
}

// Every method below and every free function that mirrors one is a
// stub. The deprecation attribute is what makes a caller see that at
// compile time rather than only through the returned sentinel. A
// caller that means to touch a stub suppresses the warning around the
// call.
class SrIovManager : public ::foundation::Pinned<SrIovManager> {
public:
    SrIovManager() = default;

    [[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend creates virtual "
                            "functions; returns PrivilegedApplyDeferred or "
                            "PrivilegedBackendUnavailable")]]
    ::fixy::Result<::fixy::View<VfHandle>, SrIovError> enable(DeclaredSrIovPlan plan,
                                                              ::fixy::View<VfHandle> out) noexcept;

    [[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend configures a "
                            "virtual function; returns PrivilegedApplyDeferred")]]
    ::fixy::Result<::fixy::Unit, SrIovError> configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept;

    [[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend removes virtual "
                            "functions; returns PrivilegedApplyDeferred or "
                            "PrivilegedBackendUnavailable")]]
    ::fixy::Result<::fixy::Unit, SrIovError> disable(DeclaredSrIovPlan plan) noexcept;
};

[[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend creates virtual "
                        "functions")]]
::fixy::Result<::fixy::View<VfHandle>, SrIovError> enable(DeclaredSrIovPlan plan, ::fixy::View<VfHandle> out) noexcept;
[[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend configures a "
                        "virtual function")]]
::fixy::Result<::fixy::Unit, SrIovError> configure_vf(VfHandle handle, DeclaredVfConfig config) noexcept;
[[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend removes virtual "
                        "functions")]]
::fixy::Result<::fixy::Unit, SrIovError> disable(DeclaredSrIovPlan plan) noexcept;
[[nodiscard, deprecated("CRUCIBLE_STUB: no backend reads the live virtual-function "
                        "layout; returns SrIovError::QueryDeferred")]]
::fixy::Result<DeclaredSrIovPlan, SrIovError> query_current(CogIdentity physical,
                                                            cntp::NicInterfaceName interface) noexcept;

}  // namespace crucible::cog::sriov
