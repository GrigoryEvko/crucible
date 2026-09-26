#pragma once

#include <crucible/Platform.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Checked.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/EnumName.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp::tcam {

inline constexpr std::uint32_t kMaxStaticTcamRules = 65536;

// False: nothing here reaches the device.  No rdma-core, no DPDK rte_flow, no
// tc-flower, no switchd, no netlink, no vendor SDK, and the rule table below
// is process-local bookkeeping.  The per-table backend_ready flag is an
// administrator's assertion rather than proof of an installed backend, so
// require_backend_ready() returning success means only that somebody claimed
// the table is usable.
inline constexpr bool vendor_backend_attached = false;

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

// The log spelling of each enum comes from reflection.  A value that names
// no enumerator reads as "<unknown E>".
[[nodiscard]] constexpr std::string_view tcam_error_name(TcamError error) noexcept {
    return ::foundation::reflect::enum_name(error);
}
[[nodiscard]] constexpr std::string_view tcam_target_kind_name(TcamTargetKind kind) noexcept {
    return ::foundation::reflect::enum_name(kind);
}
[[nodiscard]] constexpr std::string_view flow_action_name(FlowAction action) noexcept {
    return ::foundation::reflect::enum_name(action);
}

inline constexpr std::uint8_t kMaxTcamDscp = 63;
inline constexpr std::uint16_t kMaxTcamPriority = 65535;

// One predicate for each bounded value.  The alias and each mint read the
// same constant, so the two cannot drift apart.
inline constexpr auto tcam_entry_range = ::fixy::in_range<std::uint32_t{1}, kMaxStaticTcamRules>;
inline constexpr auto tcam_priority_range = ::fixy::in_range<std::uint16_t{0}, kMaxTcamPriority>;
inline constexpr auto tcam_dscp_range = ::fixy::in_range<std::uint8_t{0}, kMaxTcamDscp>;

using TcamRuleId = ::fixy::NonZero<std::uint64_t>;
using TcamEntryCount = ::fixy::Refined<tcam_entry_range, std::uint32_t>;
using TcamPriority = ::fixy::Refined<tcam_priority_range, std::uint16_t>;
using TcamDscp = ::fixy::Refined<tcam_dscp_range, std::uint8_t>;

template <std::uint32_t MaxRules>
concept TcamTableShape = MaxRules > 0u && MaxRules <= kMaxStaticTcamRules;

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
    TcamDscp dscp = ::fixy::mint_refined<tcam_dscp_range>(std::uint8_t{0});
};

// A rule as the caller writes it.  declare_tcam_rule checks the fields that
// no refinement holds, and add_rule checks them again.
struct TcamFlowRule {
    TcamRuleId rule_id = ::fixy::mint_refined<::fixy::non_zero>(std::uint64_t{1});
    FiveTuple match{};
    TcamFlowAction action{};
    TcamPriority priority = ::fixy::mint_refined<tcam_priority_range>(std::uint16_t{0});
    bool audit_to_cipher = true;
};

// The two kinds of target that carry a TCAM.
template <class Caps>
concept TcamTargetCaps = std::same_as<Caps, cog::NicPortTargetCaps> || std::same_as<Caps, cog::NvSwitchTargetCaps>;

// A table plan is start-up work on a target that carries a TCAM, so the
// mint takes a context that admits the initialization row.
template <class Ctx, class Caps>
concept CtxFitsTcamMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>
    && TcamTargetCaps<Caps>;

class TcamTablePlan;

using DeclaredTcamTable = ::fixy::Tagged<TcamTablePlan, ::fixy::tags::source::TcamTable>;

// The one door to a table plan.  It checks that the target carries a TCAM
// and that the caps hold the capacity.
template <::foundation::effects::IsExecCtx Ctx, class Caps>
    requires CtxFitsTcamMint<Ctx, Caps>
[[nodiscard]] constexpr std::expected<DeclaredTcamTable, TcamError>
mint_tcam_table(Ctx const&, cog::CogIdentity target, Caps const& caps, TcamEntryCount capacity,
                bool backend_ready = false) noexcept;

// A table plan that mint_tcam_table checked.  The constructor is private and
// the mint is the one friend, so no default, aggregate or copied-then-edited
// plan goes around the target and capacity checks.  The fields are private
// for the same reason.
class TcamTablePlan {
    cog::CogIdentity target_{};
    TcamTargetKind target_kind_ = TcamTargetKind::NicPort;
    TcamEntryCount capacity_ = ::fixy::mint_refined<tcam_entry_range>(std::uint32_t{1});
    bool backend_ready_ = false;

    constexpr TcamTablePlan(cog::CogIdentity target, TcamTargetKind target_kind, TcamEntryCount capacity,
                            bool backend_ready) noexcept
        : target_{target}, target_kind_{target_kind}, capacity_{capacity}, backend_ready_{backend_ready} {}

    template <::foundation::effects::IsExecCtx FriendCtx, class FriendCaps>
        requires CtxFitsTcamMint<FriendCtx, FriendCaps>
    friend constexpr std::expected<DeclaredTcamTable, TcamError>
    mint_tcam_table(FriendCtx const&, cog::CogIdentity, FriendCaps const&, TcamEntryCount, bool) noexcept;

public:
    [[nodiscard]] constexpr cog::CogIdentity const& target() const noexcept { return target_; }
    [[nodiscard]] constexpr TcamTargetKind target_kind() const noexcept { return target_kind_; }
    [[nodiscard]] constexpr TcamEntryCount capacity() const noexcept { return capacity_; }
    [[nodiscard]] constexpr bool backend_ready() const noexcept { return backend_ready_; }
};

template <std::uint32_t MaxRules>
    requires TcamTableShape<MaxRules>
class TcamRules;

// The proof that one table installed one rule.  The constructor is private
// and the rule table is the one friend, so a handle comes only from
// add_rule.  The copy is deleted, so the Linear wrapper around a handle is
// its one owner.  The table also compares the slot, the generation and the
// target of each handle, so a handle whose rule was removed reaches no slot.
class TcamRuleHandle {
    cog::Uuid target_uuid_{};
    TcamRuleId rule_id_ = ::fixy::mint_refined<::fixy::non_zero>(std::uint64_t{1});
    std::uint32_t slot_ = 0;
    std::uint32_t generation_ = 0;

    constexpr TcamRuleHandle(cog::Uuid target_uuid, TcamRuleId rule_id, std::uint32_t slot,
                             std::uint32_t generation) noexcept
        : target_uuid_{target_uuid}, rule_id_{rule_id}, slot_{slot}, generation_{generation} {}

    template <std::uint32_t MaxRules>
        requires TcamTableShape<MaxRules>
    friend class TcamRules;

public:
    TcamRuleHandle(TcamRuleHandle const&) = delete("a handle is the one owner of an installed rule");
    TcamRuleHandle& operator=(TcamRuleHandle const&) = delete("a handle is the one owner of an installed rule");
    constexpr TcamRuleHandle(TcamRuleHandle&&) noexcept = default;
    constexpr TcamRuleHandle& operator=(TcamRuleHandle&&) noexcept = default;
    constexpr ~TcamRuleHandle() = default;

    [[nodiscard]] constexpr cog::Uuid target_uuid() const noexcept { return target_uuid_; }
    [[nodiscard]] constexpr TcamRuleId rule_id() const noexcept { return rule_id_; }
    [[nodiscard]] constexpr std::uint32_t slot() const noexcept { return slot_; }
    [[nodiscard]] constexpr std::uint32_t generation() const noexcept { return generation_; }
};

using DeclaredTcamFlowRule = ::fixy::Tagged<TcamFlowRule, ::fixy::tags::source::TcamFlowRule>;
using OwnedTcamRule = ::fixy::Linear<TcamRuleHandle>;

[[nodiscard]] constexpr std::expected<TcamRuleId, TcamError> admit_tcam_rule_id(std::uint64_t id) noexcept {
    return ::fixy::admit_refined<::fixy::non_zero>(id, TcamError::InvalidRuleId);
}

[[nodiscard]] constexpr std::expected<TcamEntryCount, TcamError> admit_tcam_entries(std::uint32_t entries) noexcept {
    return ::fixy::admit_refined<tcam_entry_range>(entries, TcamError::InvalidEntryCount);
}

[[nodiscard]] constexpr std::expected<TcamDscp, TcamError> admit_tcam_dscp(std::uint8_t dscp) noexcept {
    return ::fixy::admit_refined<tcam_dscp_range>(dscp, TcamError::InvalidActionParameter);
}

// Every 16-bit value is a priority, so this admission cannot fail.
[[nodiscard]] constexpr std::expected<TcamPriority, TcamError> admit_tcam_priority(std::uint16_t priority) noexcept {
    return ::fixy::mint_refined<tcam_priority_range>(priority);
}

[[nodiscard]] constexpr std::expected<void, TcamError> validate_tcam_match(FiveTuple match) noexcept {
    if (match.src_prefix_bits > 32u || match.dst_prefix_bits > 32u) {
        return std::unexpected(TcamError::InvalidMatchParameter);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, TcamError> validate_tcam_action(TcamFlowAction const& action) noexcept {
    if ((action.kind == FlowAction::Redirect || action.kind == FlowAction::Mirror) && action.redirect_ifindex == 0u) {
        return std::unexpected(TcamError::InvalidActionParameter);
    }
    return {};
}

// The rule id and the DSCP are refined, so the match and the action are the
// fields left to check.
[[nodiscard]] constexpr std::expected<void, TcamError> validate_tcam_rule(TcamFlowRule const& rule) noexcept {
    auto match_valid = validate_tcam_match(rule.match);
    if (!match_valid.has_value()) {
        return std::unexpected(match_valid.error());
    }
    return validate_tcam_action(rule.action);
}

[[nodiscard]] constexpr std::expected<DeclaredTcamFlowRule, TcamError> declare_tcam_rule(TcamFlowRule rule) noexcept {
    auto valid = validate_tcam_rule(rule);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return ::fixy::mint_tagged<::fixy::tags::source::TcamFlowRule>(std::move(rule));
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_target(cog::CogIdentity target, cog::NicPortTargetCaps const& caps) noexcept {
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
validate_tcam_target(cog::CogIdentity target, cog::NvSwitchTargetCaps const& caps) noexcept {
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
validate_tcam_capacity(TcamEntryCount requested, cog::NicPortTargetCaps const& caps) noexcept {
    if (requested.value() > caps.tcam_entries.value()) {
        return std::unexpected(TcamError::CapacityExceeded);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, TcamError>
validate_tcam_capacity(TcamEntryCount requested, cog::NvSwitchTargetCaps const& caps) noexcept {
    if (requested.value() > caps.tcam_entries.value()) {
        return std::unexpected(TcamError::CapacityExceeded);
    }
    return {};
}

template <::foundation::effects::IsExecCtx Ctx, class Caps>
    requires CtxFitsTcamMint<Ctx, Caps>
[[nodiscard]] constexpr std::expected<DeclaredTcamTable, TcamError>
mint_tcam_table(Ctx const&, cog::CogIdentity target, Caps const& caps, TcamEntryCount capacity,
                bool backend_ready) noexcept {
    auto target_valid = validate_tcam_target(target, caps);
    if (!target_valid.has_value()) {
        return std::unexpected(target_valid.error());
    }
    auto capacity_valid = validate_tcam_capacity(capacity, caps);
    if (!capacity_valid.has_value()) {
        return std::unexpected(capacity_valid.error());
    }
    constexpr TcamTargetKind target_kind =
        std::same_as<Caps, cog::NicPortTargetCaps> ? TcamTargetKind::NicPort : TcamTargetKind::Switch;
    return ::fixy::mint_tagged<::fixy::tags::source::TcamTable>(
        TcamTablePlan{target, target_kind, capacity, backend_ready});
}

template <std::uint32_t MaxRules>
    requires TcamTableShape<MaxRules>
class TcamRules : public ::foundation::Pinned<TcamRules<MaxRules>> {
    struct Slot {
        TcamFlowRule rule{};
        std::uint32_t generation = 1;
        std::uint64_t counter = 0;
        bool occupied = false;
    };

    DeclaredTcamTable plan_;
    std::array<Slot, MaxRules> slots_{};
    std::uint32_t installed_ = 0;

    // The table holds no more rules than the plan asks for, and no more
    // than its own slots.
    [[nodiscard]] constexpr std::uint32_t rule_limit() const noexcept {
        std::uint32_t const planned = plan_.value().capacity().value();
        return std::min(planned, MaxRules);
    }

    [[nodiscard]] constexpr std::expected<std::uint32_t, TcamError>
    slot_for(TcamRuleHandle const& handle) const noexcept {
        if (handle.slot() >= MaxRules) {
            return std::unexpected(TcamError::InvalidRuleHandle);
        }
        Slot const& slot = slots_[handle.slot()];
        if (!slot.occupied || slot.generation != handle.generation()
            || slot.rule.rule_id.value() != handle.rule_id().value()
            || handle.target_uuid() != plan_.value().target().uuid) {
            return std::unexpected(TcamError::InvalidRuleHandle);
        }
        return handle.slot();
    }

public:
    explicit constexpr TcamRules(DeclaredTcamTable plan) noexcept : plan_{std::move(plan)} {}

    [[nodiscard]] constexpr TcamTablePlan const& plan() const noexcept { return plan_.value(); }

    [[nodiscard]] constexpr std::uint32_t installed_rules() const noexcept { return installed_; }

    [[nodiscard]] constexpr std::uint32_t available_rules_remaining() const noexcept {
        auto const limit = rule_limit();
        return installed_ >= limit ? 0u : limit - installed_;
    }

    [[nodiscard]] constexpr std::expected<OwnedTcamRule, TcamError> add_rule(DeclaredTcamFlowRule rule) noexcept {
        auto valid = validate_tcam_rule(rule.value());
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        if (installed_ >= rule_limit()) {
            return std::unexpected(TcamError::TableFull);
        }
        for (std::uint32_t i = 0; i < MaxRules; ++i) {
            Slot& slot = slots_[i];
            if (!slot.occupied) {
                slot.rule = rule.value();
                slot.counter = 0;
                slot.occupied = true;
                ++installed_;
                TcamRuleHandle handle{plan_.value().target().uuid, slot.rule.rule_id, i, slot.generation};
                return ::fixy::mint_linear<TcamRuleHandle>(std::move(handle));
            }
        }
        return std::unexpected(TcamError::TableFull);
    }

    [[nodiscard]] constexpr std::expected<void, TcamError> remove_rule(OwnedTcamRule handle) noexcept {
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

    [[nodiscard]] constexpr std::expected<void, TcamError> note_match(OwnedTcamRule const& handle,
                                                                      std::uint64_t count = 1) noexcept {
        auto slot_index = slot_for(handle.peek());
        if (!slot_index.has_value()) {
            return std::unexpected(slot_index.error());
        }
        auto updated = ::fixy::checked_add(slots_[*slot_index].counter, count);
        if (!updated.has_value()) {
            return std::unexpected(TcamError::CounterOverflow);
        }
        slots_[*slot_index].counter = *updated;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, TcamError> require_backend_ready() const noexcept {
        if (!plan_.value().backend_ready()) {
            return std::unexpected(TcamError::VendorBackendUnavailable);
        }
        return {};
    }
};

// Carries [[deprecated]] not because it is going away but because the
// attribute makes every call site warn, so a stub cannot be reached without
// notice at compile time.  A table whose backend_ready flag is set makes this
// return success while programming nothing.
[[nodiscard, deprecated("CRUCIBLE_STUB: no vendor path installs the rule on the "
                        "device. The success path is substrate bookkeeping, not device programming")]]
constexpr std::expected<void, TcamError> force_tcam_backend_boundary(DeclaredTcamTable const& table,
                                                                     DeclaredTcamFlowRule const& rule) noexcept {
    auto valid = validate_tcam_rule(rule.value());
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (!table.value().backend_ready()) {
        return std::unexpected(TcamError::VendorBackendUnavailable);
    }
    return {};
}

static_assert(sizeof(TcamRuleId) == sizeof(std::uint64_t));
static_assert(sizeof(TcamEntryCount) == sizeof(std::uint32_t));
static_assert(sizeof(TcamPriority) == sizeof(std::uint16_t));
static_assert(sizeof(TcamDscp) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredTcamFlowRule) == sizeof(TcamFlowRule));
static_assert(::fixy::qtt_consume_tracked || sizeof(OwnedTcamRule) == sizeof(TcamRuleHandle));
static_assert(TcamTableShape<1>);
static_assert(TcamTableShape<kMaxStaticTcamRules>);
static_assert(!TcamTableShape<0>);
static_assert(!TcamTableShape<kMaxStaticTcamRules + 1>);
static_assert(!admit_tcam_entries(0).has_value());
static_assert(!admit_tcam_entries(kMaxStaticTcamRules + 1).has_value());
static_assert(admit_tcam_entries(kMaxStaticTcamRules).value().value() == kMaxStaticTcamRules);
static_assert(CtxFitsTcamMint<::fixy::ColdInitCtx, cog::NicPortTargetCaps>);
static_assert(CtxFitsTcamMint<::fixy::ColdInitCtx, cog::NvSwitchTargetCaps>);
static_assert(!CtxFitsTcamMint<::fixy::BgDrainCtx, cog::NicPortTargetCaps>,
              "a table plan is built at start-up, not on a background drain");
static_assert(!CtxFitsTcamMint<::fixy::TestRunnerCtx, cog::NicPortTargetCaps>,
              "a test context carries no initialization effect");
static_assert(!CtxFitsTcamMint<::fixy::ColdInitCtx, cog::GpuTargetCaps>, "a GPU carries no TCAM");

// A table plan and a rule handle each come from their one door.
static_assert(!std::is_default_constructible_v<TcamTablePlan>);
static_assert(!std::is_default_constructible_v<DeclaredTcamTable>,
              "a default table plan would skip the target and capacity checks");
static_assert(!std::is_default_constructible_v<TcamRuleHandle>);
static_assert(!std::is_constructible_v<TcamRuleHandle, cog::Uuid, TcamRuleId, std::uint32_t, std::uint32_t>);
static_assert(!std::is_copy_constructible_v<TcamRuleHandle>);
static_assert(std::is_nothrow_move_constructible_v<TcamRuleHandle>);

// A refined field keeps a rule, a plan and a handle from being trivially
// copyable, so no byte copy builds one.
static_assert(std::is_trivially_copyable_v<FiveTuple>);
static_assert(!std::is_trivially_copyable_v<TcamFlowAction>);
static_assert(!std::is_trivially_copyable_v<TcamFlowRule>);
static_assert(!std::is_trivially_copyable_v<TcamTablePlan>);
static_assert(std::is_trivially_copy_constructible_v<TcamFlowRule>);

}  // namespace crucible::cntp::tcam
