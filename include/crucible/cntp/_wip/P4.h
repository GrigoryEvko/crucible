#pragma once

// GAPS-145 WIP. CNT-P P4 programmable-switch sketch.
//
// This header owns typed admission for switch-dataplane program intent.
// It does not call P4 Studio, SAI, Broadcom SDK, switchd, or any vendor
// compiler/deployment daemon. Live backends consume DeclaredP4Program
// values and currently report explicit deferral or unavailability after
// the switch Cog and resource budget are proven.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp::_wip::p4 {

namespace wip_source {
struct P4Compiled {};
}  // namespace wip_source

enum class P4Error : std::uint8_t {
    None = 0,
    ZeroSwitchCog,
    NonSwitchCog,
    MissingP4Capability,
    InvalidProgramId,
    InvalidSourceBytes,
    InvalidTcamEntries,
    InvalidStageCount,
    InvalidRegisterWidthBits,
    TcamBudgetExceeded,
    CompilerUnavailable,
    CompileDeferred,
    DeploymentDeferred,
    VendorBackendUnavailable,
};

enum class P4ProgramKind : std::uint8_t {
    IntTelemetry = 0,
    SharpAssist,
    ContentRoute,
    FabricMulticast,
    FlowAcl,
    LoadBalancer,
};

[[nodiscard]] std::string_view p4_error_name(P4Error error) noexcept;
[[nodiscard]] std::string_view p4_program_kind_name(P4ProgramKind kind) noexcept;

using P4ProgramId = safety::Refined<safety::non_zero, std::uint64_t>;
using P4SourceBytes = safety::Positive<std::uint64_t>;
using P4TcamEntries = safety::Positive<std::uint32_t>;
using P4StageCount = safety::Positive<std::uint16_t>;
using P4RegisterWidthBits = safety::Positive<std::uint16_t>;

struct P4ResourceBudget {
    P4TcamEntries tcam_entries{std::uint32_t{1},
                               typename P4TcamEntries::Trusted{}};
    P4StageCount pipeline_stages{std::uint16_t{1},
                                 typename P4StageCount::Trusted{}};
    P4RegisterWidthBits register_width_bits{
        std::uint16_t{1}, typename P4RegisterWidthBits::Trusted{}};
};

struct P4ProgramSpec {
    P4ProgramId program_id{std::uint64_t{1},
                           typename P4ProgramId::Trusted{}};
    P4ProgramKind kind = P4ProgramKind::IntTelemetry;
    P4SourceBytes source_bytes{std::uint64_t{1},
                               typename P4SourceBytes::Trusted{}};
    P4ResourceBudget budget{};
    bool compiler_available = false;
    bool allow_backend_compile = false;
    bool allow_backend_deploy = false;
};

struct P4DeploymentHandle {
    cog::Uuid switch_uuid{};
    P4ProgramId program_id{std::uint64_t{1},
                           typename P4ProgramId::Trusted{}};
    P4ProgramKind kind = P4ProgramKind::IntTelemetry;
    P4ResourceBudget budget{};
};

using DeclaredP4Program =
    safety::Tagged<P4ProgramSpec, wip_source::P4Compiled>;
using OwnedP4Deployment = safety::Linear<P4DeploymentHandle>;

template <class Ctx>
concept CtxFitsP4Mint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

[[nodiscard]] constexpr std::expected<P4ProgramId, P4Error>
admit_p4_program_id(std::uint64_t id) noexcept {
    if (id == 0u) {
        return std::unexpected(P4Error::InvalidProgramId);
    }
    return P4ProgramId{id, typename P4ProgramId::Trusted{}};
}

[[nodiscard]] constexpr std::expected<P4SourceBytes, P4Error>
admit_p4_source_bytes(std::uint64_t bytes) noexcept {
    if (bytes == 0u) {
        return std::unexpected(P4Error::InvalidSourceBytes);
    }
    return P4SourceBytes{bytes, typename P4SourceBytes::Trusted{}};
}

[[nodiscard]] constexpr std::expected<P4TcamEntries, P4Error>
admit_p4_tcam_entries(std::uint32_t entries) noexcept {
    if (entries == 0u) {
        return std::unexpected(P4Error::InvalidTcamEntries);
    }
    return P4TcamEntries{entries, typename P4TcamEntries::Trusted{}};
}

[[nodiscard]] constexpr std::expected<P4StageCount, P4Error>
admit_p4_stage_count(std::uint16_t stages) noexcept {
    if (stages == 0u) {
        return std::unexpected(P4Error::InvalidStageCount);
    }
    return P4StageCount{stages, typename P4StageCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<P4RegisterWidthBits, P4Error>
admit_p4_register_width_bits(std::uint16_t bits) noexcept {
    if (bits == 0u) {
        return std::unexpected(P4Error::InvalidRegisterWidthBits);
    }
    return P4RegisterWidthBits{
        bits, typename P4RegisterWidthBits::Trusted{}};
}

[[nodiscard]] constexpr std::expected<P4ResourceBudget, P4Error>
admit_p4_resource_budget(std::uint32_t tcam_entries,
                         std::uint16_t pipeline_stages,
                         std::uint16_t register_width_bits) noexcept {
    auto tcam = admit_p4_tcam_entries(tcam_entries);
    if (!tcam.has_value()) {
        return std::unexpected(tcam.error());
    }
    auto stages = admit_p4_stage_count(pipeline_stages);
    if (!stages.has_value()) {
        return std::unexpected(stages.error());
    }
    auto reg = admit_p4_register_width_bits(register_width_bits);
    if (!reg.has_value()) {
        return std::unexpected(reg.error());
    }
    return P4ResourceBudget{
        .tcam_entries = *tcam,
        .pipeline_stages = *stages,
        .register_width_bits = *reg,
    };
}

[[nodiscard]] constexpr std::expected<void, P4Error>
validate_p4_switch(cog::CogIdentity sw,
                   cog::NvSwitchTargetCaps const& caps) noexcept {
    if (sw.uuid.is_zero()) {
        return std::unexpected(P4Error::ZeroSwitchCog);
    }
    if (sw.kind != cog::CogKind::NvSwitch) {
        return std::unexpected(P4Error::NonSwitchCog);
    }
    if (!caps.features.test(cog::SwitchFeature::P4)) {
        return std::unexpected(P4Error::MissingP4Capability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, P4Error>
validate_p4_spec(P4ProgramSpec const& spec) noexcept {
    if (spec.program_id.value() == 0u) {
        return std::unexpected(P4Error::InvalidProgramId);
    }
    if (spec.source_bytes.value() == 0u) {
        return std::unexpected(P4Error::InvalidSourceBytes);
    }
    if (spec.budget.tcam_entries.value() == 0u) {
        return std::unexpected(P4Error::InvalidTcamEntries);
    }
    if (spec.budget.pipeline_stages.value() == 0u) {
        return std::unexpected(P4Error::InvalidStageCount);
    }
    if (spec.budget.register_width_bits.value() == 0u) {
        return std::unexpected(P4Error::InvalidRegisterWidthBits);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, P4Error>
validate_p4_budget(P4ResourceBudget budget,
                   cog::NvSwitchTargetCaps const& caps) noexcept {
    if (budget.tcam_entries.value() > caps.tcam_entries.value()) {
        return std::unexpected(P4Error::TcamBudgetExceeded);
    }
    return {};
}

template <class Ctx>
    requires CtxFitsP4Mint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredP4Program, P4Error>
mint_p4_program(Ctx const&,
                cog::CogIdentity sw,
                cog::NvSwitchTargetCaps caps,
                P4ProgramSpec spec) noexcept {
    auto switch_valid = validate_p4_switch(sw, caps);
    if (!switch_valid.has_value()) {
        return std::unexpected(switch_valid.error());
    }
    auto spec_valid = validate_p4_spec(spec);
    if (!spec_valid.has_value()) {
        return std::unexpected(spec_valid.error());
    }
    auto budget_valid = validate_p4_budget(spec.budget, caps);
    if (!budget_valid.has_value()) {
        return std::unexpected(budget_valid.error());
    }
    return DeclaredP4Program{spec};
}

[[nodiscard]] constexpr std::expected<OwnedP4Deployment, P4Error>
deploy_p4_program(cog::CogIdentity sw,
                  cog::NvSwitchTargetCaps caps,
                  DeclaredP4Program program) noexcept {
    auto switch_valid = validate_p4_switch(sw, caps);
    if (!switch_valid.has_value()) {
        return std::unexpected(switch_valid.error());
    }
    auto const& spec = program.value();
    auto budget_valid = validate_p4_budget(spec.budget, caps);
    if (!budget_valid.has_value()) {
        return std::unexpected(budget_valid.error());
    }
    if (!spec.compiler_available) {
        return std::unexpected(P4Error::CompilerUnavailable);
    }
    if (!spec.allow_backend_compile) {
        return std::unexpected(P4Error::CompileDeferred);
    }
    if (!spec.allow_backend_deploy) {
        return std::unexpected(P4Error::DeploymentDeferred);
    }
    return std::unexpected(P4Error::VendorBackendUnavailable);
}

class P4DeploymentSession : public safety::Pinned<P4DeploymentSession> {
    OwnedP4Deployment deployment_;

public:
    explicit P4DeploymentSession(OwnedP4Deployment deployment) noexcept
        : deployment_{std::move(deployment)} {}

    [[nodiscard]] constexpr P4DeploymentHandle const& handle() const noexcept {
        return deployment_.peek();
    }
};

[[nodiscard]] std::expected<OwnedP4Deployment, P4Error>
force_p4_vendor_boundary(cog::CogIdentity sw,
                         cog::NvSwitchTargetCaps caps,
                         DeclaredP4Program program) noexcept;

static_assert(sizeof(P4ProgramId) == sizeof(std::uint64_t));
static_assert(sizeof(P4SourceBytes) == sizeof(std::uint64_t));
static_assert(sizeof(P4TcamEntries) == sizeof(std::uint32_t));
static_assert(sizeof(P4StageCount) == sizeof(std::uint16_t));
static_assert(sizeof(P4RegisterWidthBits) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredP4Program) == sizeof(P4ProgramSpec));
static_assert(sizeof(OwnedP4Deployment) == sizeof(P4DeploymentHandle));
static_assert(CtxFitsP4Mint<effects::ColdInitCtx>);
static_assert(!CtxFitsP4Mint<effects::BgDrainCtx>);
static_assert(std::is_trivially_copyable_v<P4ResourceBudget>);
static_assert(std::is_trivially_copyable_v<P4ProgramSpec>);
static_assert(std::is_trivially_copyable_v<P4DeploymentHandle>);

}  // namespace crucible::cntp::_wip::p4
