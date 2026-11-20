#pragma once

// The traffic-control direct-action eBPF programs of the dataplane, as plans
// that the admission functions check.  Nothing here changes a qdisc, loads a
// verifier program, owns a program file descriptor or attaches to clsact.
// TcFlowClassMap is a map image of this process that stands in for the
// kernel map dp_tc_flow_classes of bpf/dp_tc_egress_classify.bpf.c.

#include <crucible/cntp/dataplane/NetTimeline.h>
#include <crucible/cntp/dataplane/Xdp.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstdint>

namespace crucible::cntp::dataplane {

// The return codes of a TC program, with the values of TC_ACT_* of the
// kernel.
enum class TcAction : std::uint8_t {
    Ok = 0,
    Shot = 2,
    Stolen = 4,
    Pipe = 3,
    Reclassify = 1,
    Trap = 8,
};

enum class TcAttachPoint : std::uint8_t {
    Ingress = 0,
    Egress = 1,
};

enum class TcProgramKind : std::uint8_t {
    EgressMark = 0,
    PacingGate = 1,
    QuarantineDrop = 2,
    FlowTelemetry = 3,
    IngressClassify = 4,
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

// The log spelling of each enum comes from reflection.  A value that names
// no enumerator reads as "<unknown E>".
[[nodiscard]] constexpr ::fixy::TextView tc_action_name(TcAction action) noexcept {
    return ::foundation::core::enum_name_text(action);
}
[[nodiscard]] constexpr ::fixy::TextView tc_attach_point_name(TcAttachPoint point) noexcept {
    return ::foundation::core::enum_name_text(point);
}
[[nodiscard]] constexpr ::fixy::TextView tc_program_kind_name(TcProgramKind kind) noexcept {
    return ::foundation::core::enum_name_text(kind);
}
[[nodiscard]] constexpr ::fixy::TextView tc_error_name(TcError error) noexcept {
    return ::foundation::core::enum_name_text(error);
}

inline constexpr std::uint8_t kMaxTcDscp = 63;
inline constexpr std::uint8_t kMaxTcFlowPriority = 7;

// One predicate for each bounded value.  The alias and each mint read the
// same constant, so the two cannot drift apart.
inline constexpr auto tc_dscp_range = ::fixy::in_range<std::uint8_t{0}, kMaxTcDscp>;
inline constexpr auto tc_flow_priority_range = ::fixy::in_range<std::uint8_t{0}, kMaxTcFlowPriority>;

using TcIfIndex = XdpIfIndex;
using TcDscp = ::fixy::Refined<tc_dscp_range, std::uint8_t>;
using TcClassId = ::fixy::Positive<std::uint32_t>;
using TcFlowPriority = ::fixy::Refined<tc_flow_priority_range, std::uint8_t>;

struct TcProgramSpec {
    cntp::NicInterfaceName interface{};
    TcIfIndex ifindex = ::fixy::mint_refined_constant<::fixy::positive, std::uint32_t{1}>();
    TcAttachPoint attach_point = TcAttachPoint::Egress;
    TcProgramKind kind = TcProgramKind::EgressMark;
    TcAction default_action = TcAction::Ok;
    bool is_direct_action = true;
    ::fixy::Bits<cog::NicFeature> required_features{cog::NicFeature::TcEbpf};
};

// A flow class as the admission functions checked it.  Each bounded field
// is refined, so a class holds only values that the kernel program accepts.
struct TcFlowClass {
    TcDscp dscp = ::fixy::mint_refined_constant<tc_dscp_range, std::uint8_t{0}>();
    TcClassId classid = ::fixy::mint_refined_constant<::fixy::positive, std::uint32_t{1}>();
    TcFlowPriority priority = ::fixy::mint_refined_constant<tc_flow_priority_range, std::uint8_t{0}>();
    TcAction action = TcAction::Ok;
};

// The bytes of one flow class as the kernel map stores them.  The layout is
// struct crucible_tc_flow_class of bpf/dp_tc_egress_classify.bpf.c.  A
// kernel map copies bytes, so the record holds no refined field.
struct TcFlowClassRecord {
    std::uint32_t classid = 0;
    std::uint8_t dscp = 0;
    std::uint8_t priority = 0;
    TcAction action = TcAction::Ok;
    std::uint8_t pad = 0;
};

using DeclaredTcProgram = ::fixy::Tagged<TcProgramSpec, ::fixy::tags::source::TcEbpf>;
using DeclaredTcFlowClass = ::fixy::Tagged<TcFlowClass, ::fixy::tags::source::TcEbpf>;

// An attach plan is start-up work, so its mint takes a context that admits
// the initialization row.
template <class Ctx>
concept CtxFitsTcMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

[[nodiscard]] constexpr ::fixy::Result<TcDscp, TcError> admit_tc_dscp(std::uint8_t dscp) noexcept {
    return ::fixy::admit_refined<tc_dscp_range>(dscp, TcError::InvalidDscp);
}

[[nodiscard]] constexpr ::fixy::Result<TcClassId, TcError> admit_tc_classid(std::uint32_t classid) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(classid, TcError::InvalidClassId);
}

[[nodiscard]] constexpr ::fixy::Result<TcFlowPriority, TcError> admit_tc_flow_priority(std::uint8_t priority) noexcept {
    return ::fixy::admit_refined<tc_flow_priority_range>(priority, TcError::InvalidFlowPriority);
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsTcMint<Ctx>
[[nodiscard]] constexpr DeclaredTcProgram mint_tc_program(Ctx const&, cntp::NicInterfaceName iface, TcIfIndex ifindex,
                                                          TcAttachPoint attach_point, TcProgramKind kind,
                                                          TcAction default_action = TcAction::Ok) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::TcEbpf>(TcProgramSpec{
        .interface = iface,
        .ifindex = ifindex,
        .attach_point = attach_point,
        .kind = kind,
        .default_action = default_action,
        .is_direct_action = true,
        .required_features = ::fixy::Bits<cog::NicFeature>{cog::NicFeature::TcEbpf},
    });
}

// Each field comes refined, so the class needs no check of its own.
[[nodiscard]] constexpr DeclaredTcFlowClass mint_tc_flow_class(TcDscp dscp, TcClassId classid, TcFlowPriority priority,
                                                               TcAction action = TcAction::Ok) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::TcEbpf>(TcFlowClass{
        .dscp = dscp,
        .classid = classid,
        .priority = priority,
        .action = action,
    });
}

// The interface index of a program is refined, so the checks left are the
// kind of the target and its TC eBPF feature.
[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcError> tc_admit_nic(cog::CogIdentity const& identity,
                                                                           cog::NicPortTargetCaps const& caps,
                                                                           DeclaredTcProgram const& program) noexcept {
    if (identity.kind != cog::CogKind::NicPort) {
        return ::fixy::err(TcError::WrongCogKind);
    }
    if (program.value().required_features.test(cog::NicFeature::TcEbpf)
        && !caps.features.test(cog::NicFeature::TcEbpf)) {
        return ::fixy::err(TcError::MissingTcEbpf);
    }
    return ::fixy::Unit{};
}

// No privileged attach path exists, so each call gives
// PrivilegedAttachDeferred.
[[nodiscard]] ::fixy::Result<::fixy::Unit, TcError> attach_tc_program(DeclaredTcProgram const& program) noexcept;

// The image of the flow map of the egress classifier: the key is the flow
// key of bpf/net_common.h, and the value is the record of a flow class.
template <std::uint32_t MaxFlows>
    requires(MaxFlows > 0)
class TcFlowClassMap : public ::foundation::Pinned<TcFlowClassMap<MaxFlows>> {
    BpfMapImage<NetFlowKey, TcFlowClassRecord, MaxFlows, BpfMapKind::LruHash> map_{};

public:
    constexpr TcFlowClassMap() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return map_.size(); }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, XdpError>
    update(NetFlowKey const& key, DeclaredTcFlowClass const& value, BpfMapUpdate mode = BpfMapUpdate::Any) noexcept {
        TcFlowClass const& cls = value.value();
        return map_.update(key,
                           TcFlowClassRecord{
                               .classid = cls.classid.value(),
                               .dscp = cls.dscp.value(),
                               .priority = cls.priority.value(),
                               .action = cls.action,
                           },
                           mode);
    }

    // update is the one writer of the image, and it stores the fields of an
    // admitted class, so each checked mint below holds.
    [[nodiscard]] constexpr ::fixy::Option<DeclaredTcFlowClass> lookup(NetFlowKey const& key) const noexcept {
        return map_.lookup(key).match(
            [](TcFlowClassRecord record) noexcept {
                return ::fixy::Option<DeclaredTcFlowClass>::some(
                    ::fixy::mint_tagged<::fixy::tags::source::TcEbpf>(TcFlowClass{
                        .dscp = ::fixy::mint_refined<tc_dscp_range>(record.dscp),
                        .classid = ::fixy::mint_refined<::fixy::positive>(record.classid),
                        .priority = ::fixy::mint_refined<tc_flow_priority_range>(record.priority),
                        .action = record.action,
                    }));
            },
            []() noexcept { return ::fixy::Option<DeclaredTcFlowClass>{::fixy::none}; });
    }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, XdpError> erase(NetFlowKey const& key) noexcept {
        return map_.erase(key);
    }
};

}  // namespace crucible::cntp::dataplane
