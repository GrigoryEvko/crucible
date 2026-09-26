#pragma once

// Nothing here calls P4 Studio, SAI, a Broadcom SDK, switchd or any vendor
// compiler or deployment daemon.  deploy_p4_program proves the switch and the
// resource budget and then reports deferral or unavailability.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OffloadTarget.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>

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

using P4ProgramId = ::fixy::Refined<::fixy::non_zero, std::uint64_t>;
using P4SourceBytes = ::fixy::Positive<std::uint64_t>;
using P4TcamEntries = ::fixy::Positive<std::uint32_t>;
using P4StageCount = ::fixy::Positive<std::uint16_t>;
using P4RegisterWidthBits = ::fixy::Positive<std::uint16_t>;

// The refined fields take no default.  A budget and a spec name each value
// that their caller admitted, so neither exists before those values do.
struct P4ResourceBudget {
    P4TcamEntries tcam_entries;
    P4StageCount pipeline_stages;
    P4RegisterWidthBits register_width_bits;
};

struct P4ProgramSpec {
    P4ProgramId program_id;
    P4ProgramKind kind = P4ProgramKind::IntTelemetry;
    P4SourceBytes source_bytes;
    P4ResourceBudget budget;
    bool compiler_available = false;
    bool allow_backend_compile = false;
    bool allow_backend_deploy = false;
};

// mint_p4_program is the one function that returns a declared program.
using DeclaredP4Program = ::fixy::Tagged<P4ProgramSpec, wip_source::P4Compiled>;

class P4DeploymentHandle;
using OwnedP4Deployment = ::fixy::Linear<P4DeploymentHandle>;

// The handle of a program that a switch runs.  The constructor is private
// and deploy_p4_program is its one door, so no caller holds a deployment
// that no deploy started.  The door opens for no program while no backend
// exists.  A handle names one deployment, so it moves and does not copy: a
// copy would let a second owner be minted from the first.
class P4DeploymentHandle {
public:
    P4DeploymentHandle(P4DeploymentHandle const&) = delete("a deployment handle names one running program");
    P4DeploymentHandle& operator=(P4DeploymentHandle const&) = delete("a deployment handle names one running program");
    P4DeploymentHandle(P4DeploymentHandle&&) noexcept = default;
    P4DeploymentHandle& operator=(P4DeploymentHandle&&) noexcept = default;
    ~P4DeploymentHandle() = default;

    [[nodiscard]] constexpr cog::Uuid switch_uuid() const noexcept { return switch_uuid_; }
    [[nodiscard]] constexpr P4ProgramId program_id() const noexcept { return program_id_; }
    [[nodiscard]] constexpr P4ProgramKind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr P4ResourceBudget const& budget() const noexcept { return budget_; }

private:
    constexpr P4DeploymentHandle(cog::Uuid switch_uuid, P4ProgramId program_id, P4ProgramKind kind,
                                 P4ResourceBudget budget) noexcept
        : switch_uuid_{switch_uuid}, program_id_{program_id}, kind_{kind}, budget_{budget} {}

    friend constexpr std::expected<OwnedP4Deployment, P4Error>
    deploy_p4_program(cog::CogIdentity sw, cog::NvSwitchTargetCaps const& caps, DeclaredP4Program program) noexcept;

    cog::Uuid switch_uuid_{};
    P4ProgramId program_id_;
    P4ProgramKind kind_ = P4ProgramKind::IntTelemetry;
    P4ResourceBudget budget_;
};

template <class Ctx>
concept CtxFitsP4Mint = ::foundation::effects::IsExecCtx<Ctx>
                     && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

[[nodiscard]] constexpr std::expected<P4ProgramId, P4Error> admit_p4_program_id(std::uint64_t id) noexcept {
    return ::fixy::admit_refined<::fixy::non_zero>(id, P4Error::InvalidProgramId);
}

[[nodiscard]] constexpr std::expected<P4SourceBytes, P4Error> admit_p4_source_bytes(std::uint64_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, P4Error::InvalidSourceBytes);
}

[[nodiscard]] constexpr std::expected<P4TcamEntries, P4Error> admit_p4_tcam_entries(std::uint32_t entries) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(entries, P4Error::InvalidTcamEntries);
}

[[nodiscard]] constexpr std::expected<P4StageCount, P4Error> admit_p4_stage_count(std::uint16_t stages) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(stages, P4Error::InvalidStageCount);
}

[[nodiscard]] constexpr std::expected<P4RegisterWidthBits, P4Error>
admit_p4_register_width_bits(std::uint16_t bits) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bits, P4Error::InvalidRegisterWidthBits);
}

[[nodiscard]] constexpr std::expected<P4ResourceBudget, P4Error>
admit_p4_resource_budget(std::uint32_t tcam_entries, std::uint16_t pipeline_stages,
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

// A P4 program runs on a switch whose capabilities advertise P4.
inline constexpr cog::OffloadTargetRefusals<P4Error> p4_target_refusals{
    .undiscovered = P4Error::ZeroSwitchCog,
    .wrong_kind = P4Error::NonSwitchCog,
    .missing_feature = P4Error::MissingP4Capability,
};

[[nodiscard]] constexpr std::expected<void, P4Error> validate_p4_switch(cog::CogIdentity const& sw,
                                                                        cog::NvSwitchTargetCaps const& caps) noexcept {
    return cog::validate_offload_target<cog::SwitchFeature::P4, cog::CogKind::NvSwitch>(sw, caps, p4_target_refusals);
}

// The refined fields already hold these bounds.  The check reads them
// again, so a value that entered through mint_refined_trusted is still
// refused here.
[[nodiscard]] constexpr std::expected<void, P4Error> validate_p4_spec(P4ProgramSpec const& spec) noexcept {
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

[[nodiscard]] constexpr std::expected<void, P4Error> validate_p4_budget(P4ResourceBudget const& budget,
                                                                        cog::NvSwitchTargetCaps const& caps) noexcept {
    if (budget.tcam_entries.value() > caps.tcam_entries.value()) {
        return std::unexpected(P4Error::TcamBudgetExceeded);
    }
    return {};
}

template <class Ctx>
    requires CtxFitsP4Mint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredP4Program, P4Error>
mint_p4_program(Ctx const&, cog::CogIdentity sw, cog::NvSwitchTargetCaps const& caps, P4ProgramSpec spec) noexcept {
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
    return ::fixy::mint_tagged<wip_source::P4Compiled>(spec);
}

[[nodiscard]] constexpr std::expected<OwnedP4Deployment, P4Error>
deploy_p4_program(cog::CogIdentity sw, cog::NvSwitchTargetCaps const& caps, DeclaredP4Program program) noexcept {
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

// A session takes an owned deployment, and only deploy_p4_program makes
// one, so a session exists only for a program that a switch runs.
class P4DeploymentSession : public ::foundation::Pinned<P4DeploymentSession> {
    OwnedP4Deployment deployment_;

public:
    explicit P4DeploymentSession(OwnedP4Deployment deployment) noexcept : deployment_{std::move(deployment)} {}

    [[nodiscard]] constexpr P4DeploymentHandle const& handle() const noexcept { return deployment_.peek(); }
};

[[nodiscard]] std::expected<OwnedP4Deployment, P4Error>
force_p4_vendor_boundary(cog::CogIdentity sw, cog::NvSwitchTargetCaps const& caps, DeclaredP4Program program) noexcept;

static_assert(sizeof(P4ProgramId) == sizeof(std::uint64_t));
static_assert(sizeof(P4SourceBytes) == sizeof(std::uint64_t));
static_assert(sizeof(P4TcamEntries) == sizeof(std::uint32_t));
static_assert(sizeof(P4StageCount) == sizeof(std::uint16_t));
static_assert(sizeof(P4RegisterWidthBits) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredP4Program) == sizeof(P4ProgramSpec));
static_assert(::fixy::qtt_consume_tracked || sizeof(OwnedP4Deployment) == sizeof(P4DeploymentHandle));
static_assert(CtxFitsP4Mint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsP4Mint<::fixy::BgDrainCtx>);
// A refined member makes a budget, a spec or a handle not trivially
// copyable, because no byte route may build a refined value.  A copy still
// costs what copying the bytes costs.
static_assert(std::is_trivially_copy_constructible_v<P4ResourceBudget>
              && std::is_trivially_destructible_v<P4ResourceBudget>);
static_assert(std::is_trivially_copy_constructible_v<P4ProgramSpec> && std::is_trivially_destructible_v<P4ProgramSpec>);
// No declared program exists before its refined values, and no handle exists
// outside deploy_p4_program or beside the one it names.
static_assert(!std::is_default_constructible_v<P4ResourceBudget>);
static_assert(!std::is_default_constructible_v<DeclaredP4Program>);
static_assert(!std::is_constructible_v<P4DeploymentHandle, cog::Uuid, P4ProgramId, P4ProgramKind, P4ResourceBudget>);
static_assert(!std::is_copy_constructible_v<P4DeploymentHandle>
              && std::is_nothrow_move_constructible_v<P4DeploymentHandle>);

}  // namespace crucible::cntp::_wip::p4
