#pragma once

// GAPS-144 WIP. CNT-P DOCA / DPU offload sketch.
//
// This header owns typed admission for DPU / SmartNIC offload intent.
// It deliberately does not link NVIDIA DOCA, Pensando SDKs, Nitro
// tooling, or any vendor userspace daemon. Live backends must consume
// DeclaredDocaDeployPlan / DocaChannelConfig values and currently report
// explicit deferral or unavailability after the request shape is proven.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

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
[[nodiscard]] std::string_view
doca_offload_kind_name(DocaOffloadKind kind) noexcept;

using DocaProgramId = safety::Refined<safety::non_zero, std::uint64_t>;
using DocaImageBytes = safety::Positive<std::uint64_t>;
using DocaQueueDepth = safety::Positive<std::uint16_t>;
using DocaPayloadBytes = safety::Positive<std::uint32_t>;

struct DocaOffloadSpec {
    DocaProgramId program_id{std::uint64_t{1},
                             typename DocaProgramId::Trusted{}};
    DocaOffloadKind kind = DocaOffloadKind::SwimGossip;
    DocaImageBytes image_bytes{std::uint64_t{1},
                               typename DocaImageBytes::Trusted{}};
    DocaQueueDepth queue_depth{std::uint16_t{1},
                               typename DocaQueueDepth::Trusted{}};
    bool runtime_loaded = false;
    bool allow_backend_deploy = false;
};

struct DocaDeployPlan {
    cog::CogIdentity dpu{};
    DocaOffloadSpec spec{};
};

struct DocaOffloadHandle {
    cog::Uuid dpu_uuid{};
    DocaProgramId program_id{std::uint64_t{1},
                             typename DocaProgramId::Trusted{}};
    DocaOffloadKind kind = DocaOffloadKind::SwimGossip;
    DocaQueueDepth queue_depth{std::uint16_t{1},
                               typename DocaQueueDepth::Trusted{}};
};

struct DocaChannelConfig {
    DocaPayloadBytes max_payload_bytes{std::uint32_t{1},
                                       typename DocaPayloadBytes::Trusted{}};
    bool comm_channel_ready = false;
};

using DeclaredDocaDeployPlan =
    safety::Tagged<DocaDeployPlan, wip_source::DocaOffload>;
using OwnedDocaOffload = safety::Linear<DocaOffloadHandle>;

template <class Ctx>
concept CtxFitsDocaMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsDocaComm =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Bg>>;

[[nodiscard]] constexpr std::expected<DocaProgramId, DocaError>
admit_doca_program_id(std::uint64_t id) noexcept {
    if (id == 0u) {
        return std::unexpected(DocaError::InvalidProgramId);
    }
    return DocaProgramId{id, typename DocaProgramId::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DocaImageBytes, DocaError>
admit_doca_image_bytes(std::uint64_t bytes) noexcept {
    if (bytes == 0u) {
        return std::unexpected(DocaError::InvalidProgramImageBytes);
    }
    return DocaImageBytes{bytes, typename DocaImageBytes::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DocaQueueDepth, DocaError>
admit_doca_queue_depth(std::uint16_t depth) noexcept {
    if (depth == 0u) {
        return std::unexpected(DocaError::InvalidQueueDepth);
    }
    return DocaQueueDepth{depth, typename DocaQueueDepth::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DocaPayloadBytes, DocaError>
admit_doca_payload_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0u) {
        return std::unexpected(DocaError::InvalidPayloadBytes);
    }
    return DocaPayloadBytes{bytes, typename DocaPayloadBytes::Trusted{}};
}

[[nodiscard]] constexpr bool is_dpu_carrier(cog::CogKind kind) noexcept {
    return kind == cog::CogKind::NicCard || kind == cog::CogKind::NvSwitch;
}

[[nodiscard]] constexpr std::expected<void, DocaError>
validate_doca_dpu(cog::CogIdentity dpu,
                  cog::NvSwitchTargetCaps const& caps) noexcept {
    if (dpu.uuid.is_zero()) {
        return std::unexpected(DocaError::ZeroDpuCog);
    }
    if (!is_dpu_carrier(dpu.kind)) {
        return std::unexpected(DocaError::NonDpuCog);
    }
    if (!caps.features.test(cog::SwitchFeature::Doca)) {
        return std::unexpected(DocaError::MissingDocaCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, DocaError>
validate_doca_spec(DocaOffloadSpec const& spec) noexcept {
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

[[nodiscard]] constexpr std::expected<DeclaredDocaDeployPlan, DocaError>
validate_doca_deploy_plan(DocaDeployPlan plan,
                          cog::NvSwitchTargetCaps const& caps) noexcept {
    auto dpu_valid = validate_doca_dpu(plan.dpu, caps);
    if (!dpu_valid.has_value()) {
        return std::unexpected(dpu_valid.error());
    }
    auto spec_valid = validate_doca_spec(plan.spec);
    if (!spec_valid.has_value()) {
        return std::unexpected(spec_valid.error());
    }
    return DeclaredDocaDeployPlan{plan};
}

template <class Ctx>
    requires CtxFitsDocaMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredDocaDeployPlan, DocaError>
mint_doca_deploy_plan(Ctx const&,
                      cog::CogIdentity dpu,
                      cog::NvSwitchTargetCaps caps,
                      DocaOffloadSpec spec) noexcept {
    return validate_doca_deploy_plan(DocaDeployPlan{
        .dpu = dpu,
        .spec = spec,
    }, caps);
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

class DpuCommChannel : public safety::Pinned<DpuCommChannel> {
    OwnedDocaOffload offload_;
    DocaChannelConfig config_{};

public:
    DpuCommChannel(OwnedDocaOffload offload,
                   DocaChannelConfig config) noexcept
        : offload_{std::move(offload)}, config_{config} {}

    template <class Ctx>
        requires CtxFitsDocaComm<Ctx>
    [[nodiscard]] std::expected<void, DocaError>
    send_to_dpu(Ctx const&, std::span<const std::byte> payload) noexcept {
        if (payload.size() > config_.max_payload_bytes.value()) {
            return std::unexpected(DocaError::PayloadTooLarge);
        }
        if (!config_.comm_channel_ready) {
            return std::unexpected(DocaError::CommChannelUnavailable);
        }
        return std::unexpected(DocaError::VendorBackendUnavailable);
    }

    template <class Ctx>
        requires CtxFitsDocaComm<Ctx>
    [[nodiscard]] std::expected<std::size_t, DocaError>
    recv_from_dpu(Ctx const&, std::span<std::byte> output) noexcept {
        if (output.size() < config_.max_payload_bytes.value()) {
            return std::unexpected(DocaError::OutputBufferTooSmall);
        }
        if (!config_.comm_channel_ready) {
            return std::unexpected(DocaError::CommChannelUnavailable);
        }
        return std::unexpected(DocaError::VendorBackendUnavailable);
    }

    [[nodiscard]] constexpr DocaOffloadHandle const& handle() const noexcept {
        return offload_.peek();
    }
};

[[nodiscard]] std::expected<OwnedDocaOffload, DocaError>
force_doca_backend_boundary(DeclaredDocaDeployPlan plan) noexcept;

static_assert(sizeof(DocaProgramId) == sizeof(std::uint64_t));
static_assert(sizeof(DocaImageBytes) == sizeof(std::uint64_t));
static_assert(sizeof(DocaQueueDepth) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredDocaDeployPlan) == sizeof(DocaDeployPlan));
static_assert(sizeof(OwnedDocaOffload) == sizeof(DocaOffloadHandle));
static_assert(CtxFitsDocaMint<effects::ColdInitCtx>);
static_assert(!CtxFitsDocaMint<effects::BgDrainCtx>);
static_assert(CtxFitsDocaComm<effects::BgDrainCtx>);
static_assert(!CtxFitsDocaComm<effects::ColdInitCtx>);
static_assert(std::is_trivially_copyable_v<DocaOffloadSpec>);
static_assert(std::is_trivially_copyable_v<DocaDeployPlan>);
static_assert(std::is_trivially_copyable_v<DocaOffloadHandle>);

}  // namespace crucible::cntp::_wip::doca
