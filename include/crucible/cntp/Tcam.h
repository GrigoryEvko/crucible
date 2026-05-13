#pragma once

// GAPS-148. CNT-P hardware TCAM flow-rule substrate.
//
// This header owns the typed admission layer for NIC / switch match-action
// rules. It does not call rdma-core DV, DPDK rte_flow, tc-flower, switchd,
// netlink, or vendor SDKs. Live backends consume DeclaredTcamFlowRule values
// and currently report explicit unavailability after target capability and
// table budget are proven.

#include <crucible/Platform.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Checked.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp::tcam {

inline constexpr std::uint32_t kMaxStaticTcamRules = 65'536;

enum class TcamError : std::uint8_t {
    None = 0,
    ZeroTargetCog,
    WrongTargetKind,
    MissingTcamCapability,
    InvalidRuleId,
    InvalidEntryCount,
    InvalidMatchParameter,
    InvalidActionParameter,
    CapacityExceeded,
    TableFull,
    InvalidRuleHandle,
    CounterOverflow,
    VendorBackendUnavailable,
};

enum class TcamTargetKind : std::uint8_t {
    NicPort = 0,
    Switch,
};

enum class FlowAction : std::uint8_t {
    Drop = 0,
    Pass,
    Redirect,
    MarkDscp,
    Count,
    Mirror,
};

[[nodiscard]] std::string_view tcam_error_name(TcamError error) noexcept;
[[nodiscard]] std::string_view tcam_target_kind_name(TcamTargetKind kind) noexcept;
[[nodiscard]] std::string_view flow_action_name(FlowAction action) noexcept;

using TcamRuleId = safety::Refined<safety::non_zero, std::uint64_t>;
using TcamEntryCount = safety::Positive<std::uint32_t>;
using TcamPriority =
    safety::Bounded<std::uint16_t{0}, std::uint16_t{65'535}, std::uint16_t>;
using TcamDscp =
    safety::Bounded<std::uint8_t{0}, std::uint8_t{63}, std::uint8_t>;

template <std::uint32_t MaxRules>
concept TcamTableShape =
    MaxRules > 0u && MaxRules <= kMaxStaticTcamRules;

struct FiveTuple {
    std::uint32_t src_ipv4_be = 0;
    std::uint32_t dst_ipv4_be = 0;
    std::uint16_t src_port = 0;
    std::uint16_t dst_port = 0;
    std::uint8_t protocol = 0;
    std::uint8_t src_prefix_bits = 0;
    std::uint8_t dst_prefix_bits = 0;
    bool src_port_wildcard = true;
    bool dst_port_wildcard = true;
};

struct TcamFlowAction {
    FlowAction kind = FlowAction::Drop;
    std::uint32_t redirect_ifindex = 0;
    TcamDscp dscp{std::uint8_t{0}, typename TcamDscp::Trusted{}};
};

struct TcamFlowRule {
    TcamRuleId rule_id{std::uint64_t{1}, typename TcamRuleId::Trusted{}};
    FiveTuple match{};
    TcamFlowAction action{};
    TcamPriority priority{std::uint16_t{0}, typename TcamPriority::Trusted{}};
    bool audit_to_cipher = true;
};

struct TcamTablePlan {
    cog::CogIdentity target{};
    TcamTargetKind target_kind = TcamTargetKind::NicPort;
    TcamEntryCount capacity{std::uint32_t{1},
                            typename TcamEntryCount::Trusted{}};
    bool backend_ready = false;
};

struct TcamRuleHandle {
    cog::Uuid target_uuid{};
    TcamRuleId rule_id{std::uint64_t{1}, typename TcamRuleId::Trusted{}};
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
};

using DeclaredTcamTable =
    safety::Tagged<TcamTablePlan, safety::source::TcamTable>;
using DeclaredTcamFlowRule =
    safety::Tagged<TcamFlowRule, safety::source::TcamFlowRule>;
using OwnedTcamRule = safety::Linear<TcamRuleHandle>;

template <class Ctx>
concept CtxFitsTcamMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

[[nodiscard]] constexpr std::expected<TcamRuleId, TcamError>
admit_tcam_rule_id(std::uint64_t id) noexcept {
    if (id == 0u) {
        return std::unexpected(TcamError::InvalidRuleId);
    }
    return TcamRuleId{id, typename TcamRuleId::Trusted{}};
}

[[nodiscard]] constexpr std::expected<TcamEntryCount, TcamError>
admit_tcam_entries(std::uint32_t entries) noexcept {
    if (entries == 0u || entries > kMaxStaticTcamRules) {
        return std::unexpected(TcamError::InvalidEntryCount);
    }
    return TcamEntryCount{entries, typename TcamEntryCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<TcamDscp, TcamError>
admit_tcam_dscp(std::uint8_t dscp) noexcept {
    if (dscp > 63u) {
        return std::unexpected(TcamError::InvalidActionParameter);
    }
    return TcamDscp{dscp, typename TcamDscp::Trusted{}};
}

[[nodiscard]] constexpr std::expected<TcamPriority, TcamError>
admit_tcam_priority(std::uint16_t priority) noexcept {
    return TcamPriority{priority, typename TcamPriority::Trusted{}};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_match(FiveTuple match) noexcept {
    if (match.src_prefix_bits > 32u || match.dst_prefix_bits > 32u) {
        return std::unexpected(TcamError::InvalidMatchParameter);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_action(TcamFlowAction action) noexcept {
    if ((action.kind == FlowAction::Redirect || action.kind == FlowAction::Mirror)
        && action.redirect_ifindex == 0u) {
        return std::unexpected(TcamError::InvalidActionParameter);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_rule(TcamFlowRule const& rule) noexcept {
    if (rule.rule_id.value() == 0u) {
        return std::unexpected(TcamError::InvalidRuleId);
    }
    auto match_valid = validate_tcam_match(rule.match);
    if (!match_valid.has_value()) {
        return std::unexpected(match_valid.error());
    }
    return validate_tcam_action(rule.action);
}

[[nodiscard]] constexpr std::expected<DeclaredTcamFlowRule, TcamError>
declare_tcam_rule(TcamFlowRule rule) noexcept {
    auto valid = validate_tcam_rule(rule);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return DeclaredTcamFlowRule{rule};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_target(cog::CogIdentity target,
                     cog::NicPortTargetCaps const& caps) noexcept {
    if (target.uuid.is_zero()) {
        return std::unexpected(TcamError::ZeroTargetCog);
    }
    if (target.kind != cog::CogKind::NicPort) {
        return std::unexpected(TcamError::WrongTargetKind);
    }
    if (!caps.features.test(cog::NicFeature::Tcam)) {
        return std::unexpected(TcamError::MissingTcamCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_target(cog::CogIdentity target,
                     cog::NvSwitchTargetCaps const& caps) noexcept {
    if (target.uuid.is_zero()) {
        return std::unexpected(TcamError::ZeroTargetCog);
    }
    if (target.kind != cog::CogKind::NvSwitch) {
        return std::unexpected(TcamError::WrongTargetKind);
    }
    if (!caps.features.test(cog::SwitchFeature::Tcam)) {
        return std::unexpected(TcamError::MissingTcamCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_capacity(TcamEntryCount requested,
                       cog::NicPortTargetCaps const& caps) noexcept {
    if (requested.value() > caps.tcam_entries.value()) {
        return std::unexpected(TcamError::CapacityExceeded);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_capacity(TcamEntryCount requested,
                       cog::NvSwitchTargetCaps const& caps) noexcept {
    if (requested.value() > caps.tcam_entries.value()) {
        return std::unexpected(TcamError::CapacityExceeded);
    }
    return {};
}

template <class Ctx, class Caps>
    requires CtxFitsTcamMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredTcamTable, TcamError>
mint_tcam_table(Ctx const&,
                cog::CogIdentity target,
                Caps const& caps,
                TcamEntryCount capacity,
                bool backend_ready = false) noexcept {
    auto target_valid = validate_tcam_target(target, caps);
    if (!target_valid.has_value()) {
        return std::unexpected(target_valid.error());
    }
    auto capacity_valid = validate_tcam_capacity(capacity, caps);
    if (!capacity_valid.has_value()) {
        return std::unexpected(capacity_valid.error());
    }
    constexpr bool is_nic = std::is_same_v<Caps, cog::NicPortTargetCaps>;
    return DeclaredTcamTable{TcamTablePlan{
        .target = target,
        .target_kind = is_nic ? TcamTargetKind::NicPort : TcamTargetKind::Switch,
        .capacity = capacity,
        .backend_ready = backend_ready,
    }};
}

template <std::uint32_t MaxRules>
    requires TcamTableShape<MaxRules>
class TcamRules : public safety::Pinned<TcamRules<MaxRules>> {
    struct Slot {
        TcamFlowRule rule{};
        std::uint32_t generation = 1;
        std::uint64_t counter = 0;
        bool occupied = false;
    };

    DeclaredTcamTable plan_;
    std::array<Slot, MaxRules> slots_{};
    std::uint32_t installed_ = 0;

    [[nodiscard]] constexpr std::expected<std::uint32_t, TcamError>
    slot_for(TcamRuleHandle const& handle) const noexcept {
        if (handle.slot >= MaxRules) {
            return std::unexpected(TcamError::InvalidRuleHandle);
        }
        Slot const& slot = slots_[handle.slot];
        if (!slot.occupied || slot.generation != handle.generation
            || slot.rule.rule_id.value() != handle.rule_id.value()
            || handle.target_uuid != plan_.value().target.uuid) {
            return std::unexpected(TcamError::InvalidRuleHandle);
        }
        return handle.slot;
    }

public:
    explicit constexpr TcamRules(DeclaredTcamTable plan) noexcept
        : plan_{plan} {}

    [[nodiscard]] constexpr TcamTablePlan const& plan() const noexcept {
        return plan_.value();
    }

    [[nodiscard]] constexpr std::uint32_t installed_rules() const noexcept {
        return installed_;
    }

    [[nodiscard]] constexpr std::uint32_t
    available_rules_remaining() const noexcept {
        auto const cap = plan_.value().capacity.value();
        return installed_ >= cap ? 0u : cap - installed_;
    }

    [[nodiscard]] constexpr std::expected<OwnedTcamRule, TcamError>
    add_rule(DeclaredTcamFlowRule rule) noexcept {
        auto valid = validate_tcam_rule(rule.value());
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        if (installed_ >= plan_.value().capacity.value() || installed_ >= MaxRules) {
            return std::unexpected(TcamError::TableFull);
        }
        for (std::uint32_t i = 0; i < MaxRules; ++i) {
            Slot& slot = slots_[i];
            if (!slot.occupied) {
                slot.rule = rule.value();
                slot.counter = 0;
                slot.occupied = true;
                ++installed_;
                return OwnedTcamRule{TcamRuleHandle{
                    .target_uuid = plan_.value().target.uuid,
                    .rule_id = slot.rule.rule_id,
                    .slot = i,
                    .generation = slot.generation,
                }};
            }
        }
        return std::unexpected(TcamError::TableFull);
    }

    [[nodiscard]] constexpr std::expected<void, TcamError>
    remove_rule(OwnedTcamRule handle) noexcept {
        TcamRuleHandle raw = std::move(handle).consume();
        auto slot_index = slot_for(raw);
        if (!slot_index.has_value()) {
            return std::unexpected(slot_index.error());
        }
        Slot& slot = slots_[*slot_index];
        slot.occupied = false;
        slot.counter = 0;
        ++slot.generation;
        --installed_;
        return {};
    }

    [[nodiscard]] CRUCIBLE_HOT std::expected<std::uint64_t, TcamError>
    query_counter(OwnedTcamRule const& handle) const noexcept {
        auto slot_index = slot_for(handle.peek());
        if (!slot_index.has_value()) {
            return std::unexpected(slot_index.error());
        }
        return slots_[*slot_index].counter;
    }

    [[nodiscard]] constexpr std::expected<void, TcamError>
    note_match(OwnedTcamRule const& handle, std::uint64_t count = 1) noexcept {
        auto slot_index = slot_for(handle.peek());
        if (!slot_index.has_value()) {
            return std::unexpected(slot_index.error());
        }
        auto updated = safety::checked_add(slots_[*slot_index].counter, count);
        if (!updated.has_value()) {
            return std::unexpected(TcamError::CounterOverflow);
        }
        slots_[*slot_index].counter = *updated;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, TcamError>
    require_backend_ready() const noexcept {
        if (!plan_.value().backend_ready) {
            return std::unexpected(TcamError::VendorBackendUnavailable);
        }
        return {};
    }
};

[[nodiscard]] std::expected<void, TcamError>
force_tcam_backend_boundary(DeclaredTcamTable table,
                            DeclaredTcamFlowRule rule) noexcept;

static_assert(sizeof(TcamRuleId) == sizeof(std::uint64_t));
static_assert(sizeof(TcamEntryCount) == sizeof(std::uint32_t));
static_assert(sizeof(TcamPriority) == sizeof(std::uint16_t));
static_assert(sizeof(TcamDscp) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredTcamFlowRule) == sizeof(TcamFlowRule));
static_assert(sizeof(OwnedTcamRule) == sizeof(TcamRuleHandle));
static_assert(TcamTableShape<1>);
static_assert(TcamTableShape<kMaxStaticTcamRules>);
static_assert(!TcamTableShape<0>);
static_assert(!TcamTableShape<kMaxStaticTcamRules + 1>);
static_assert(CtxFitsTcamMint<effects::ColdInitCtx>);
static_assert(!CtxFitsTcamMint<effects::BgDrainCtx>);
static_assert(std::is_trivially_copyable_v<FiveTuple>);
static_assert(std::is_trivially_copyable_v<TcamFlowAction>);
static_assert(std::is_trivially_copyable_v<TcamFlowRule>);
static_assert(std::is_trivially_copyable_v<TcamTablePlan>);
static_assert(std::is_trivially_copyable_v<TcamRuleHandle>);

}  // namespace crucible::cntp::tcam
