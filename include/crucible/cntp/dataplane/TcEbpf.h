#pragma once

// GAPS-193 substrate slice. CNT-P dataplane TC direct-action eBPF plans.
//
// TC programs are the skb-context complement to cntp/dataplane/Xdp.h. This header keeps
// live qdisc mutation, verifier loading, fd ownership, and clsact attach out
// of scope; it pins the zero-runtime userspace contract first: source-tagged
// program specs, bounded DSCP/classid fields, Init-row minting, NIC capability
// admission, and deterministic in-process policy maps for tests and future
// loader wiring.

#include <crucible/cntp/dataplane/Xdp.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp::dataplane {

enum class TcAction : std::uint8_t {
    Ok         = 0,
    Shot       = 2,
    Stolen     = 4,
    Pipe       = 3,
    Reclassify = 1,
    Trap       = 8,
};

enum class TcAttachPoint : std::uint8_t {
    Ingress = 0,
    Egress  = 1,
};

enum class TcProgramKind : std::uint8_t {
    EgressMark       = 0,
    PacingGate       = 1,
    QuarantineDrop   = 2,
    FlowTelemetry    = 3,
    IngressClassify  = 4,
};

enum class TcError : std::uint8_t {
    InvalidIfIndex,
    InvalidDscp,
    InvalidClassId,
    InvalidFlowPriority,
    WrongCogKind,
    MissingTcEbpf,
    PrivilegedAttachDeferred,
};

[[nodiscard]] std::string_view tc_action_name(TcAction action) noexcept;
[[nodiscard]] std::string_view tc_attach_point_name(TcAttachPoint point) noexcept;
[[nodiscard]] std::string_view tc_program_kind_name(TcProgramKind kind) noexcept;
[[nodiscard]] std::string_view tc_error_name(TcError error) noexcept;

using TcIfIndex = XdpIfIndex;
using TcDscp = safety::Bounded<std::uint8_t{0}, std::uint8_t{63}, std::uint8_t>;
using TcClassId = safety::Positive<std::uint32_t>;
using TcFlowPriority =
    safety::Bounded<std::uint8_t{0}, std::uint8_t{7}, std::uint8_t>;

struct TcProgramSpec {
    cntp::NicInterfaceName interface{};
    TcIfIndex ifindex{std::uint32_t{1}};
    TcAttachPoint attach_point = TcAttachPoint::Egress;
    TcProgramKind kind = TcProgramKind::EgressMark;
    TcAction default_action = TcAction::Ok;
    bool direct_action = true;
    safety::Bits<cog::NicFeature> required_features{
        cog::NicFeature::TcEbpf};
};

struct TcFlowClass {
    TcDscp dscp{std::uint8_t{0}, typename TcDscp::Trusted{}};
    TcClassId classid{std::uint32_t{1}};
    TcFlowPriority priority{std::uint8_t{0}, typename TcFlowPriority::Trusted{}};
    TcAction action = TcAction::Ok;
};

using DeclaredTcProgram =
    safety::Tagged<TcProgramSpec, safety::source::TcEbpf>;
using DeclaredTcFlowClass =
    safety::Tagged<TcFlowClass, safety::source::TcEbpf>;

template <class Ctx>
concept CtxFitsTcMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

[[nodiscard]] constexpr std::expected<TcDscp, TcError>
admit_tc_dscp(std::uint8_t dscp) noexcept {
    if (dscp > 63u) {
        return std::unexpected(TcError::InvalidDscp);
    }
    return TcDscp{dscp, typename TcDscp::Trusted{}};
}

[[nodiscard]] constexpr std::expected<TcClassId, TcError>
admit_tc_classid(std::uint32_t classid) noexcept {
    if (classid == 0u) {
        return std::unexpected(TcError::InvalidClassId);
    }
    return TcClassId{classid, typename TcClassId::Trusted{}};
}

[[nodiscard]] constexpr std::expected<TcFlowPriority, TcError>
admit_tc_flow_priority(std::uint8_t priority) noexcept {
    if (priority > 7u) {
        return std::unexpected(TcError::InvalidFlowPriority);
    }
    return TcFlowPriority{priority, typename TcFlowPriority::Trusted{}};
}

template <class Ctx>
    requires CtxFitsTcMint<Ctx>
[[nodiscard]] constexpr DeclaredTcProgram
mint_tc_program(Ctx const&,
                cntp::NicInterfaceName iface,
                TcIfIndex ifindex,
                TcAttachPoint attach_point,
                TcProgramKind kind,
                TcAction default_action = TcAction::Ok) noexcept {
    return DeclaredTcProgram{TcProgramSpec{
        .interface = iface,
        .ifindex = ifindex,
        .attach_point = attach_point,
        .kind = kind,
        .default_action = default_action,
        .direct_action = true,
        .required_features = safety::Bits<cog::NicFeature>{
            cog::NicFeature::TcEbpf},
    }};
}

[[nodiscard]] constexpr DeclaredTcFlowClass
mint_tc_flow_class(TcDscp dscp,
                   TcClassId classid,
                   TcFlowPriority priority,
                   TcAction action = TcAction::Ok) noexcept {
    return DeclaredTcFlowClass{TcFlowClass{
        .dscp = dscp,
        .classid = classid,
        .priority = priority,
        .action = action,
    }};
}

[[nodiscard]] constexpr std::expected<void, TcError>
tc_admit_nic(cog::CogIdentity const& identity,
             cog::NicPortTargetCaps const& caps,
             DeclaredTcProgram program) noexcept {
    if (program.value().ifindex.value() == 0u) {
        return std::unexpected(TcError::InvalidIfIndex);
    }
    if (identity.kind != cog::CogKind::NicPort) {
        return std::unexpected(TcError::WrongCogKind);
    }
    if (!caps.features.test(cog::NicFeature::TcEbpf)) {
        return std::unexpected(TcError::MissingTcEbpf);
    }
    return {};
}

[[nodiscard]] std::expected<void, TcError>
attach_tc_program(DeclaredTcProgram program) noexcept;

struct TcFlowKey {
    std::int32_t fd = 0;

    [[nodiscard]] friend constexpr bool
    operator==(TcFlowKey, TcFlowKey) noexcept = default;
};

[[nodiscard]] constexpr TcFlowKey tc_flow_key(cntp::SocketFd fd) noexcept {
    return TcFlowKey{.fd = fd.value()};
}

template <std::uint32_t MaxFlows>
    requires (MaxFlows > 0)
class TcFlowClassMap
    : public safety::Pinned<TcFlowClassMap<MaxFlows>> {
    BpfMapImage<TcFlowKey, TcFlowClass, MaxFlows, BpfMapKind::LruHash> map_{};

public:
    constexpr TcFlowClassMap() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t size() const noexcept {
        return map_.size();
    }

    [[nodiscard]] constexpr std::expected<void, XdpError>
    update(TcFlowKey key,
           DeclaredTcFlowClass value,
           BpfMapUpdate mode = BpfMapUpdate::Any) noexcept {
        return map_.update(key, value.value(), mode);
    }

    [[nodiscard]] constexpr std::optional<DeclaredTcFlowClass>
    lookup(TcFlowKey const& key) const noexcept {
        auto raw = map_.lookup(key);
        if (!raw.has_value()) {
            return std::nullopt;
        }
        return DeclaredTcFlowClass{*raw};
    }
};

static_assert(static_cast<std::uint8_t>(TcAction::Ok) == 0);
static_assert(static_cast<std::uint8_t>(TcAction::Shot) == 2);
static_assert(sizeof(TcIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(TcDscp) == sizeof(std::uint8_t));
static_assert(sizeof(TcClassId) == sizeof(std::uint32_t));
static_assert(sizeof(TcFlowPriority) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredTcProgram) == sizeof(TcProgramSpec));
static_assert(sizeof(DeclaredTcFlowClass) == sizeof(TcFlowClass));
static_assert(sizeof(TcFlowKey) == sizeof(std::int32_t));
static_assert(std::is_trivially_copyable_v<TcProgramSpec>);
static_assert(std::is_trivially_copyable_v<TcFlowClass>);
static_assert(std::has_unique_object_representations_v<TcFlowKey>);
static_assert(BpfKey<TcFlowKey>);
static_assert(BpfScalar<DeclaredTcFlowClass>);

}  // namespace crucible::cntp::dataplane
