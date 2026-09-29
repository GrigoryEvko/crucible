#pragma once

// Nothing here links NVIDIA DOCA, a Pensando SDK, Nitro tooling or any vendor
// userspace daemon.  The deploy and channel paths prove the request shape and
// then report deferral or unavailability.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OffloadTarget.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp::_wip::doca {

namespace wip_source {
struct DocaOffload {};
}  // namespace wip_source

enum class DocaError : std::uint8_t {
    None = 0,
    ZeroDpuCog,
    NonDpuCog,
    MissingDocaCapability,
    InvalidProgramId,
    InvalidProgramImageBytes,
    InvalidQueueDepth,
    InvalidPayloadBytes,
    RuntimeUnavailable,
    DeployDeferred,
    VendorBackendUnavailable,
    CommChannelUnavailable,
    PayloadTooLarge,
    OutputBufferTooSmall,
    ProgramMismatch,
};

enum class DocaOffloadKind : std::uint8_t {
    SwimGossip = 0,
    Scuttlebutt,
    Ktls,
    Compression,
    Crypto,
    StorageEmulation,
    FlowSteering,
};

[[nodiscard]] std::string_view doca_error_name(DocaError error) noexcept;
[[nodiscard]] std::string_view doca_offload_kind_name(DocaOffloadKind kind) noexcept;

using DocaProgramId = ::fixy::Refined<::fixy::non_zero, std::uint64_t>;
using DocaImageBytes = ::fixy::Positive<std::uint64_t>;
using DocaQueueDepth = ::fixy::Positive<std::uint16_t>;
using DocaPayloadBytes = ::fixy::Positive<std::uint32_t>;

// The refined fields take no default.  A spec names each value that its
// caller admitted, so no spec exists before those values do.
struct DocaOffloadSpec {
    DocaProgramId program_id;
    DocaOffloadKind kind = DocaOffloadKind::SwimGossip;
    DocaImageBytes image_bytes;
    DocaQueueDepth queue_depth;
    bool runtime_loaded = false;
    bool allow_backend_deploy = false;
};

struct DocaDeployPlan {
    cog::CogIdentity dpu{};
    DocaOffloadSpec spec;
};

// mint_doca_deploy_plan is the one function that returns a declared plan.
using DeclaredDocaDeployPlan = ::fixy::Tagged<DocaDeployPlan, wip_source::DocaOffload>;

class DocaOffloadHandle;
using OwnedDocaOffload = ::fixy::Linear<DocaOffloadHandle>;

// The handle of a program that a DPU runs.  The constructor is private and
// deploy_doca_offload is its one door, so no caller holds an offload that no
// deploy started.  The door opens for no request while no backend exists.
// A handle names one program, so it moves and does not copy: a copy would
// let a second owner be minted from the first.
class DocaOffloadHandle {
public:
    DocaOffloadHandle(DocaOffloadHandle const&) = delete("an offload handle names one running program");
    DocaOffloadHandle& operator=(DocaOffloadHandle const&) = delete("an offload handle names one running program");
    DocaOffloadHandle(DocaOffloadHandle&&) noexcept = default;
    DocaOffloadHandle& operator=(DocaOffloadHandle&&) noexcept = default;
    ~DocaOffloadHandle() = default;

    [[nodiscard]] constexpr cog::Uuid dpu_uuid() const noexcept { return dpu_uuid_; }
    [[nodiscard]] constexpr DocaProgramId program_id() const noexcept { return program_id_; }
    [[nodiscard]] constexpr DocaOffloadKind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr DocaQueueDepth queue_depth() const noexcept { return queue_depth_; }

private:
    constexpr DocaOffloadHandle(cog::Uuid dpu_uuid, DocaProgramId program_id, DocaOffloadKind kind,
                                DocaQueueDepth queue_depth) noexcept
        : dpu_uuid_{dpu_uuid}, program_id_{program_id}, kind_{kind}, queue_depth_{queue_depth} {}

    friend constexpr std::expected<OwnedDocaOffload, DocaError>
    deploy_doca_offload(DeclaredDocaDeployPlan plan) noexcept;

    cog::Uuid dpu_uuid_{};
    DocaProgramId program_id_;
    DocaOffloadKind kind_ = DocaOffloadKind::SwimGossip;
    DocaQueueDepth queue_depth_;
};

struct DocaChannelConfig {
    DocaPayloadBytes max_payload_bytes;
    bool comm_channel_ready = false;
};

template <class Ctx>
concept CtxFitsDocaMint = ::foundation::effects::IsExecCtx<Ctx>
                       && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

template <class Ctx>
concept CtxFitsDocaComm = ::foundation::effects::IsExecCtx<Ctx>
                       && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

[[nodiscard]] constexpr std::expected<DocaProgramId, DocaError> admit_doca_program_id(std::uint64_t id) noexcept {
    return ::fixy::admit_refined<::fixy::non_zero>(id, DocaError::InvalidProgramId);
}

[[nodiscard]] constexpr std::expected<DocaImageBytes, DocaError> admit_doca_image_bytes(std::uint64_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, DocaError::InvalidProgramImageBytes);
}

[[nodiscard]] constexpr std::expected<DocaQueueDepth, DocaError> admit_doca_queue_depth(std::uint16_t depth) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(depth, DocaError::InvalidQueueDepth);
}

[[nodiscard]] constexpr std::expected<DocaPayloadBytes, DocaError>
admit_doca_payload_bytes(std::uint32_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, DocaError::InvalidPayloadBytes);
}

// A DPU sits on a NIC card or in a switch, and its capabilities advertise
// DOCA.
inline constexpr cog::OffloadTargetRefusals<DocaError> doca_target_refusals{
    .undiscovered = DocaError::ZeroDpuCog,
    .wrong_kind = DocaError::NonDpuCog,
    .missing_feature = DocaError::MissingDocaCapability,
};

[[nodiscard]] constexpr std::expected<void, DocaError> validate_doca_dpu(cog::CogIdentity const& dpu,
                                                                         cog::NvSwitchTargetCaps const& caps) noexcept {
    return cog::validate_offload_target<cog::SwitchFeature::Doca, cog::CogKind::NicCard, cog::CogKind::NvSwitch>(
        dpu, caps, doca_target_refusals);
}

// The refined fields already hold these bounds.  The check reads them
// again, so a value that entered through mint_refined_trusted is still
// refused here.
[[nodiscard]] constexpr std::expected<void, DocaError> validate_doca_spec(DocaOffloadSpec const& spec) noexcept {
    if (spec.program_id.value() == 0u) {
        return std::unexpected(DocaError::InvalidProgramId);
    }
    if (spec.image_bytes.value() == 0u) {
        return std::unexpected(DocaError::InvalidProgramImageBytes);
    }
    if (spec.queue_depth.value() == 0u) {
        return std::unexpected(DocaError::InvalidQueueDepth);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, DocaError>
validate_doca_deploy_plan(DocaDeployPlan const& plan, cog::NvSwitchTargetCaps const& caps) noexcept {
    auto dpu_valid = validate_doca_dpu(plan.dpu, caps);
    if (!dpu_valid.has_value()) {
        return dpu_valid;
    }
    return validate_doca_spec(plan.spec);
}

template <class Ctx>
    requires CtxFitsDocaMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredDocaDeployPlan, DocaError>
mint_doca_deploy_plan(Ctx const&, cog::CogIdentity dpu, cog::NvSwitchTargetCaps const& caps,
                      DocaOffloadSpec spec) noexcept {
    const DocaDeployPlan plan{
        .dpu = dpu,
        .spec = spec,
    };
    auto valid = validate_doca_deploy_plan(plan, caps);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return ::fixy::mint_tagged<wip_source::DocaOffload>(plan);
}

[[nodiscard]] constexpr std::expected<OwnedDocaOffload, DocaError>
deploy_doca_offload(DeclaredDocaDeployPlan plan) noexcept {
    auto const& raw = plan.value();
    if (!raw.spec.runtime_loaded) {
        return std::unexpected(DocaError::RuntimeUnavailable);
    }
    if (!raw.spec.allow_backend_deploy) {
        return std::unexpected(DocaError::DeployDeferred);
    }
    return std::unexpected(DocaError::VendorBackendUnavailable);
}

// The checks that a send runs before it reaches the channel.  A payload
// larger than the channel maximum is refused, and so is a channel that is
// not ready.
[[nodiscard]] constexpr std::expected<void, DocaError> validate_doca_send(DocaChannelConfig const& config,
                                                                          std::span<const std::byte> payload) noexcept {
    if (payload.size() > config.max_payload_bytes.value()) {
        return std::unexpected(DocaError::PayloadTooLarge);
    }
    if (!config.comm_channel_ready) {
        return std::unexpected(DocaError::CommChannelUnavailable);
    }
    return {};
}

// The checks that a receive runs before it reaches the channel.  The
// output must hold the largest payload that the channel carries.
[[nodiscard]] constexpr std::expected<void, DocaError> validate_doca_recv(DocaChannelConfig const& config,
                                                                          std::span<std::byte> output) noexcept {
    if (output.size() < config.max_payload_bytes.value()) {
        return std::unexpected(DocaError::OutputBufferTooSmall);
    }
    if (!config.comm_channel_ready) {
        return std::unexpected(DocaError::CommChannelUnavailable);
    }
    return {};
}

// A channel takes an owned offload, and only deploy_doca_offload makes one,
// so a channel exists only for an offload that a DPU runs.
class DpuCommChannel : public ::foundation::Pinned<DpuCommChannel> {
    OwnedDocaOffload offload_;
    DocaChannelConfig config_;

public:
    DpuCommChannel(OwnedDocaOffload offload, DocaChannelConfig config) noexcept
        : offload_{std::move(offload)}, config_{config} {}

    template <class Ctx>
        requires CtxFitsDocaComm<Ctx>
    [[nodiscard]] std::expected<void, DocaError> send_to_dpu(Ctx const&, std::span<const std::byte> payload) noexcept {
        auto valid = validate_doca_send(config_, payload);
        if (!valid.has_value()) {
            return valid;
        }
        return std::unexpected(DocaError::VendorBackendUnavailable);
    }

    template <class Ctx>
        requires CtxFitsDocaComm<Ctx>
    [[nodiscard]] std::expected<std::size_t, DocaError> recv_from_dpu(Ctx const&,
                                                                      std::span<std::byte> output) noexcept {
        auto valid = validate_doca_recv(config_, output);
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        return std::unexpected(DocaError::VendorBackendUnavailable);
    }

    [[nodiscard]] constexpr DocaOffloadHandle const& handle() const noexcept { return offload_.peek(); }
};

[[nodiscard]] std::expected<OwnedDocaOffload, DocaError>
force_doca_backend_boundary(DeclaredDocaDeployPlan plan) noexcept;

static_assert(sizeof(DocaProgramId) == sizeof(std::uint64_t));
static_assert(sizeof(DocaImageBytes) == sizeof(std::uint64_t));
static_assert(sizeof(DocaQueueDepth) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredDocaDeployPlan) == sizeof(DocaDeployPlan));
static_assert(::fixy::qtt_consume_tracked || sizeof(OwnedDocaOffload) == sizeof(DocaOffloadHandle));
static_assert(CtxFitsDocaMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsDocaMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsDocaComm<::fixy::BgDrainCtx>);
static_assert(!CtxFitsDocaComm<::fixy::ColdInitCtx>);
// A refined member makes a spec or a plan not trivially copyable, because no
// byte route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<DocaOffloadSpec>
              && std::is_trivially_destructible_v<DocaOffloadSpec>);
static_assert(std::is_trivially_copy_constructible_v<DocaDeployPlan>
              && std::is_trivially_destructible_v<DocaDeployPlan>);
// No declared plan exists before its refined values, and no handle exists
// outside deploy_doca_offload or beside the one it names.
static_assert(!std::is_default_constructible_v<DocaOffloadSpec>);
static_assert(!std::is_default_constructible_v<DeclaredDocaDeployPlan>);
static_assert(!std::is_constructible_v<DocaOffloadHandle, cog::Uuid, DocaProgramId, DocaOffloadKind, DocaQueueDepth>);
static_assert(!std::is_copy_constructible_v<DocaOffloadHandle>
              && std::is_nothrow_move_constructible_v<DocaOffloadHandle>);

}  // namespace crucible::cntp::_wip::doca
