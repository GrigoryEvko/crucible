#pragma once

// Nothing here mutates a qdisc, loads a verifier program, owns a program file
// descriptor or attaches to clsact.  TcFlowClassMap is a process-local image
// standing in for a kernel map.

#include <crucible/cntp/dataplane/Xdp.h>
#include <fixy/Bits.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>

namespace crucible::cntp::dataplane {

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

[[nodiscard]] std::string_view tc_action_name(TcAction action) noexcept;
[[nodiscard]] std::string_view tc_attach_point_name(TcAttachPoint point) noexcept;
[[nodiscard]] std::string_view tc_program_kind_name(TcProgramKind kind) noexcept;
[[nodiscard]] std::string_view tc_error_name(TcError error) noexcept;

inline constexpr std::uint8_t kMaxTcDscp = 63;
inline constexpr std::uint8_t kMaxTcFlowPriority = 7;

using TcIfIndex = XdpIfIndex;
using TcDscp = ::fixy::Bounded<std::uint8_t{0}, kMaxTcDscp, std::uint8_t>;
using TcClassId = ::fixy::Positive<std::uint32_t>;
using TcFlowPriority = ::fixy::Bounded<std::uint8_t{0}, kMaxTcFlowPriority, std::uint8_t>;

struct TcProgramSpec {
    cntp::NicInterfaceName interface{};
    TcIfIndex ifindex = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
    TcAttachPoint attach_point = TcAttachPoint::Egress;
    TcProgramKind kind = TcProgramKind::EgressMark;
    TcAction default_action = TcAction::Ok;
    bool direct_action = true;
    ::fixy::Bits<cog::NicFeature> required_features{cog::NicFeature::TcEbpf};
};

// A flow class as the admission functions checked it.  Each bounded field
// is refined, so a class holds only values the kernel program accepts.
struct TcFlowClass {
    TcDscp dscp = ::fixy::mint_refined<::fixy::in_range<std::uint8_t{0}, kMaxTcDscp>>(std::uint8_t{0});
    TcClassId classid = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
    TcFlowPriority priority =
        ::fixy::mint_refined<::fixy::in_range<std::uint8_t{0}, kMaxTcFlowPriority>>(std::uint8_t{0});
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

[[nodiscard]] constexpr std::expected<TcDscp, TcError> admit_tc_dscp(std::uint8_t dscp) noexcept {
    return ::fixy::admit_refined<::fixy::in_range<std::uint8_t{0}, kMaxTcDscp>>(dscp, TcError::InvalidDscp);
}

[[nodiscard]] constexpr std::expected<TcClassId, TcError> admit_tc_classid(std::uint32_t classid) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(classid, TcError::InvalidClassId);
}

[[nodiscard]] constexpr std::expected<TcFlowPriority, TcError> admit_tc_flow_priority(std::uint8_t priority) noexcept {
    return ::fixy::admit_refined<::fixy::in_range<std::uint8_t{0}, kMaxTcFlowPriority>>(priority,
                                                                                        TcError::InvalidFlowPriority);
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
        .direct_action = true,
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

[[nodiscard]] constexpr std::expected<void, TcError>
tc_admit_nic(cog::CogIdentity const& identity, cog::NicPortTargetCaps const& caps, DeclaredTcProgram program) noexcept {
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

[[nodiscard]] std::expected<void, TcError> attach_tc_program(DeclaredTcProgram program) noexcept;

struct TcFlowKey {
    std::int32_t fd = 0;

    [[nodiscard]] friend constexpr bool operator==(TcFlowKey, TcFlowKey) noexcept = default;
};

[[nodiscard]] constexpr TcFlowKey tc_flow_key(cntp::SocketFd fd) noexcept { return TcFlowKey{.fd = fd.value()}; }

template <std::uint32_t MaxFlows>
    requires(MaxFlows > 0)
class TcFlowClassMap : public ::foundation::Pinned<TcFlowClassMap<MaxFlows>> {
    BpfMapImage<TcFlowKey, TcFlowClassRecord, MaxFlows, BpfMapKind::LruHash> map_{};

public:
    constexpr TcFlowClassMap() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return map_.size(); }

    [[nodiscard]] constexpr std::expected<void, XdpError> update(TcFlowKey key, DeclaredTcFlowClass value,
                                                                 BpfMapUpdate mode = BpfMapUpdate::Any) noexcept {
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

    // update is the one writer of the image, and it stores the fields of
    // an admitted class, so each checked mint below holds.
    [[nodiscard]] constexpr std::optional<DeclaredTcFlowClass> lookup(TcFlowKey const& key) const noexcept {
        auto record = map_.lookup(key);
        if (!record.has_value()) {
            return std::nullopt;
        }
        return ::fixy::mint_tagged<::fixy::tags::source::TcEbpf>(TcFlowClass{
            .dscp = ::fixy::mint_refined<::fixy::in_range<std::uint8_t{0}, kMaxTcDscp>>(record->dscp),
            .classid = ::fixy::mint_refined<::fixy::positive>(record->classid),
            .priority = ::fixy::mint_refined<::fixy::in_range<std::uint8_t{0}, kMaxTcFlowPriority>>(record->priority),
            .action = record->action,
        });
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
static_assert(std::is_trivially_copy_constructible_v<TcProgramSpec>);
static_assert(std::is_trivially_destructible_v<TcProgramSpec>);
// A refined field keeps a flow class from being trivially copyable, so no
// byte copy builds one.  The record is the byte form the map holds.
static_assert(!std::is_trivially_copyable_v<TcFlowClass>);
static_assert(std::is_trivially_copy_constructible_v<TcFlowClass>);
static_assert(std::has_unique_object_representations_v<TcFlowKey>);
static_assert(BpfKey<TcFlowKey>);
static_assert(BpfMapElement<TcFlowClassRecord>);
static_assert(!BpfScalar<TcFlowClass>, "a kernel map value holds bytes, not a refined class");
static_assert(sizeof(TcFlowClassRecord) == 8);
static_assert(offsetof(TcFlowClassRecord, classid) == 0 && offsetof(TcFlowClassRecord, dscp) == 4
              && offsetof(TcFlowClassRecord, priority) == 5 && offsetof(TcFlowClassRecord, action) == 6);
static_assert(CtxFitsTcMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsTcMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsTcMint<::fixy::TestRunnerCtx>);

}  // namespace crucible::cntp::dataplane
