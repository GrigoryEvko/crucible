#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Identity.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/EnumName.h>

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

class TcamTableDoor;

// A table plan that mint_tcam_table checked.  The constructor is private and
// the door of the mint is the one friend, so no default, aggregate or
// copied-then-edited plan goes around the target and capacity checks.  The
// fields are private for the same reason.
class TcamTablePlan {
    cog::CogIdentity target_{};
    TcamTargetKind target_kind_ = TcamTargetKind::NicPort;
    TcamEntryCount capacity_ = ::fixy::mint_refined<tcam_entry_range>(std::uint32_t{1});
    bool backend_ready_ = false;

    constexpr TcamTablePlan(cog::CogIdentity target, TcamTargetKind target_kind, TcamEntryCount capacity,
                            bool backend_ready) noexcept
        : target_{target}, target_kind_{target_kind}, capacity_{capacity}, backend_ready_{backend_ready} {}

    friend class TcamTableDoor;

public:
    [[nodiscard]] constexpr cog::CogIdentity const& target() const noexcept { return target_; }
    [[nodiscard]] constexpr TcamTargetKind target_kind() const noexcept { return target_kind_; }
    [[nodiscard]] constexpr TcamEntryCount capacity() const noexcept { return capacity_; }
    [[nodiscard]] constexpr bool backend_ready() const noexcept { return backend_ready_; }
};

// The one door to a table plan.  It checks that the target carries a TCAM
// and that the caps hold the capacity.
template <::foundation::effects::IsExecCtx Ctx, class Caps>
    requires CtxFitsTcamMint<Ctx, Caps>
[[nodiscard]] constexpr ::fixy::Result<DeclaredTcamTable, TcamError>
mint_tcam_table(Ctx const&, cog::CogIdentity target, Caps const& caps, TcamEntryCount capacity,
                bool backend_ready = false) noexcept;

// The constructor of a table plan.  Its one member is private, and the
// mint is its one friend.  The friend names the Result of a plan, so it
// stands after the plan is complete.
class TcamTableDoor {
    [[nodiscard]] static constexpr TcamTablePlan build(cog::CogIdentity target, TcamTargetKind target_kind,
                                                       TcamEntryCount capacity, bool backend_ready) noexcept {
        return TcamTablePlan{target, target_kind, capacity, backend_ready};
    }

    template <::foundation::effects::IsExecCtx FriendCtx, class FriendCaps>
        requires CtxFitsTcamMint<FriendCtx, FriendCaps>
    friend constexpr ::fixy::Result<DeclaredTcamTable, TcamError>
    mint_tcam_table(FriendCtx const&, cog::CogIdentity, FriendCaps const&, TcamEntryCount, bool) noexcept;
};

// Generation is the word of the generation counter of each slot.  A slot
// whose counter has no next value retires, so no generation of a slot
// comes back.
template <std::uint32_t MaxRules, ::fixy::UnsignedInteger Generation = std::uint64_t>
    requires TcamTableShape<MaxRules>
class TcamRules;

// The proof that one table installed one rule.  The constructor is private
// and the rule table is the one friend, so a handle comes only from
// add_rule.  The copy is deleted, so the Linear wrapper around a handle is
// its one owner.  The table also compares its identity, the slot and the
// generation of each handle.  So a handle of a different table, or of a
// removed rule, reaches no slot.
class TcamRuleHandle {
    ::foundation::core::Identity table_;
    cog::Uuid target_uuid_{};
    TcamRuleId rule_id_ = ::fixy::mint_refined<::fixy::non_zero>(std::uint64_t{1});
    std::uint64_t slot_ = 0;
    std::uint64_t generation_ = 0;

    constexpr TcamRuleHandle(::foundation::core::Identity table, cog::Uuid target_uuid, TcamRuleId rule_id,
                             std::uint64_t slot, std::uint64_t generation) noexcept
        : table_{table}, target_uuid_{target_uuid}, rule_id_{rule_id}, slot_{slot}, generation_{generation} {}

    template <std::uint32_t MaxRules, ::fixy::UnsignedInteger Generation>
        requires TcamTableShape<MaxRules>
    friend class TcamRules;

public:
    TcamRuleHandle(TcamRuleHandle const&) = delete("a handle is the one owner of an installed rule");
    TcamRuleHandle& operator=(TcamRuleHandle const&) = delete("a handle is the one owner of an installed rule");

    // A move leaves generation 0 in the source, and no slot holds generation
    // 0, so a moved-from handle reaches no slot.
    constexpr TcamRuleHandle(TcamRuleHandle&& other) noexcept
        : table_{other.table_},
          target_uuid_{other.target_uuid_},
          rule_id_{other.rule_id_},
          slot_{other.slot_},
          generation_{::fixy::exchange(other.generation_, 0)} {}
    constexpr TcamRuleHandle& operator=(TcamRuleHandle&& other) noexcept {
        table_ = other.table_;
        target_uuid_ = other.target_uuid_;
        rule_id_ = other.rule_id_;
        slot_ = other.slot_;
        generation_ = ::fixy::exchange(other.generation_, 0);
        return *this;
    }
    constexpr ~TcamRuleHandle() = default;

    [[nodiscard]] constexpr cog::Uuid target_uuid() const noexcept { return target_uuid_; }
    [[nodiscard]] constexpr TcamRuleId rule_id() const noexcept { return rule_id_; }
    [[nodiscard]] constexpr std::uint64_t slot() const noexcept { return slot_; }
    [[nodiscard]] constexpr std::uint64_t generation() const noexcept { return generation_; }
};

using DeclaredTcamFlowRule = ::fixy::Tagged<TcamFlowRule, ::fixy::tags::source::TcamFlowRule>;
using OwnedTcamRule = ::fixy::Linear<TcamRuleHandle>;

[[nodiscard]] constexpr ::fixy::Result<TcamRuleId, TcamError> admit_tcam_rule_id(std::uint64_t id) noexcept {
    return ::fixy::admit_refined<::fixy::non_zero>(id, TcamError::InvalidRuleId);
}

[[nodiscard]] constexpr ::fixy::Result<TcamEntryCount, TcamError> admit_tcam_entries(std::uint32_t entries) noexcept {
    return ::fixy::admit_refined<tcam_entry_range>(entries, TcamError::InvalidEntryCount);
}

[[nodiscard]] constexpr ::fixy::Result<TcamDscp, TcamError> admit_tcam_dscp(std::uint8_t dscp) noexcept {
    return ::fixy::admit_refined<tcam_dscp_range>(dscp, TcamError::InvalidActionParameter);
}

// Every 16-bit value is a priority, so this admission cannot fail.
[[nodiscard]] constexpr std::expected<TcamPriority, TcamError> admit_tcam_priority(std::uint16_t priority) noexcept {
    return ::fixy::mint_refined<tcam_priority_range>(priority);
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError> validate_tcam_match(FiveTuple match) noexcept {
    if (match.src_prefix_bits > 32u || match.dst_prefix_bits > 32u) {
        return ::fixy::err(TcamError::InvalidMatchParameter);
    }
    return ::fixy::Unit{};
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError>
validate_tcam_action(TcamFlowAction const& action) noexcept {
    if ((action.kind == FlowAction::Redirect || action.kind == FlowAction::Mirror) && action.redirect_ifindex == 0u) {
        return ::fixy::err(TcamError::InvalidActionParameter);
    }
    return ::fixy::Unit{};
}

// The rule id and the DSCP are refined, so the match and the action are the
// fields left to check.
[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError> validate_tcam_rule(TcamFlowRule const& rule) noexcept {
    CRUCIBLE_TRY(_, validate_tcam_match(rule.match));
    return validate_tcam_action(rule.action);
}

[[nodiscard]] constexpr ::fixy::Result<DeclaredTcamFlowRule, TcamError> declare_tcam_rule(TcamFlowRule rule) noexcept {
    CRUCIBLE_TRY(_, validate_tcam_rule(rule));
    return ::fixy::mint_tagged<::fixy::tags::source::TcamFlowRule>(std::move(rule));
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError>
validate_tcam_target(cog::CogIdentity target, cog::NicPortTargetCaps const& caps) noexcept {
    if (target.uuid.is_zero()) {
        return ::fixy::err(TcamError::ZeroTargetCog);
    }
    if (target.kind != cog::CogKind::NicPort) {
        return ::fixy::err(TcamError::WrongTargetKind);
    }
    if (!caps.features.test(cog::NicFeature::Tcam)) {
        return ::fixy::err(TcamError::MissingTcamCapability);
    }
    return ::fixy::Unit{};
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError>
validate_tcam_target(cog::CogIdentity target, cog::NvSwitchTargetCaps const& caps) noexcept {
    if (target.uuid.is_zero()) {
        return ::fixy::err(TcamError::ZeroTargetCog);
    }
    if (target.kind != cog::CogKind::NvSwitch) {
        return ::fixy::err(TcamError::WrongTargetKind);
    }
    if (!caps.features.test(cog::SwitchFeature::Tcam)) {
        return ::fixy::err(TcamError::MissingTcamCapability);
    }
    return ::fixy::Unit{};
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError>
validate_tcam_capacity(TcamEntryCount requested, cog::NicPortTargetCaps const& caps) noexcept {
    if (requested.value() > caps.tcam_entries.value()) {
        return ::fixy::err(TcamError::CapacityExceeded);
    }
    return ::fixy::Unit{};
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError>
validate_tcam_capacity(TcamEntryCount requested, cog::NvSwitchTargetCaps const& caps) noexcept {
    if (requested.value() > caps.tcam_entries.value()) {
        return ::fixy::err(TcamError::CapacityExceeded);
    }
    return ::fixy::Unit{};
}

template <::foundation::effects::IsExecCtx Ctx, class Caps>
    requires CtxFitsTcamMint<Ctx, Caps>
[[nodiscard]] constexpr ::fixy::Result<DeclaredTcamTable, TcamError>
mint_tcam_table(Ctx const&, cog::CogIdentity target, Caps const& caps, TcamEntryCount capacity,
                bool backend_ready) noexcept {
    CRUCIBLE_TRY(_, validate_tcam_target(target, caps));
    CRUCIBLE_TRY(_, validate_tcam_capacity(capacity, caps));
    constexpr TcamTargetKind target_kind =
        std::same_as<Caps, cog::NicPortTargetCaps> ? TcamTargetKind::NicPort : TcamTargetKind::Switch;
    return ::fixy::mint_tagged<::fixy::tags::source::TcamTable>(
        TcamTableDoor::build(target, target_kind, capacity, backend_ready));
}

template <std::uint32_t MaxRules, ::fixy::UnsignedInteger Generation>
    requires TcamTableShape<MaxRules>
class TcamRules : public ::foundation::Pinned<TcamRules<MaxRules, Generation>> {
    using SlotIndex = ::fixy::Index<MaxRules>;

    // The generation of a slot starts at 1, so the generation 0 of a
    // moved-from handle matches no slot.
    struct Slot {
        TcamFlowRule rule{};
        std::uint64_t counter = 0;
        Generation generation = 1;
        bool occupied = false;
        bool retired = false;
    };

    DeclaredTcamTable plan_;
    // Each table mints its identity when it starts, and the table is
    // pinned.  So two tables of one plan have different identities.
    ::foundation::core::Identity identity_;
    ::foundation::core::FixedArray<Slot, MaxRules> slots_{};
    std::uint32_t installed_ = 0;
    std::uint32_t retired_ = 0;

    // The table holds no more rules than the plan asks for, and no more
    // than its slots that did not retire.
    [[nodiscard]] constexpr std::uint32_t rule_limit() const noexcept {
        std::uint32_t const planned = plan_.value().capacity().value();
        return ::fixy::min(planned, MaxRules - retired_);
    }

    // The slot that the handle names.  A handle of a removed rule, a handle
    // of a different table and a moved-from handle name no slot.  Only this
    // table gives a handle its identity, so the identity also fixes the target.
    [[nodiscard]] constexpr ::fixy::Result<SlotIndex, TcamError> slot_for(TcamRuleHandle const& handle) const noexcept {
        CRUCIBLE_TRY(index, SlotIndex::check(handle.slot()).ok_or(TcamError::InvalidRuleHandle));
        Slot const& slot = slots_.at(index);
        if (!slot.occupied || ::fixy::widen<std::uint64_t>(slot.generation) != handle.generation()
            || slot.rule.rule_id.value() != handle.rule_id().value() || handle.table_ != identity_) {
            return ::fixy::err(TcamError::InvalidRuleHandle);
        }
        return index;
    }

public:
    explicit TcamRules(DeclaredTcamTable plan) noexcept
        : plan_{std::move(plan)}, identity_{::foundation::core::mint_identity()} {}

    [[nodiscard]] constexpr TcamTablePlan const& plan() const noexcept { return plan_.value(); }

    [[nodiscard]] constexpr std::uint32_t installed_rules() const noexcept { return installed_; }

    [[nodiscard]] constexpr std::uint32_t available_rules_remaining() const noexcept {
        auto const limit = rule_limit();
        return installed_ >= limit ? 0u : limit - installed_;
    }

    [[nodiscard]] constexpr ::fixy::Result<OwnedTcamRule, TcamError> add_rule(DeclaredTcamFlowRule rule) noexcept {
        CRUCIBLE_TRY(_, validate_tcam_rule(rule.value()));
        if (installed_ >= rule_limit()) {
            return ::fixy::err(TcamError::TableFull);
        }
        for (SlotIndex index : slots_.indices()) {
            Slot& slot = slots_.at(index);
            if (!slot.occupied && !slot.retired) {
                slot.rule = rule.value();
                slot.counter = 0;
                slot.occupied = true;
                ++installed_;
                TcamRuleHandle handle{identity_, plan_.value().target().uuid, slot.rule.rule_id, index.raw(),
                                      ::fixy::widen<std::uint64_t>(slot.generation)};
                return ::fixy::mint_linear<TcamRuleHandle>(std::move(handle));
            }
        }
        return ::fixy::err(TcamError::TableFull);
    }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError> remove_rule(OwnedTcamRule handle) noexcept {
        TcamRuleHandle raw = std::move(handle).consume();
        CRUCIBLE_TRY(index, slot_for(raw));
        Slot& slot = slots_.at(index);
        slot.occupied = false;
        slot.counter = 0;
        --installed_;
        ::foundation::core::checked_add(slot.generation, Generation{1})
            .match([&slot](Generation next) noexcept { slot.generation = next; },
                   [this, &slot]() noexcept {
                       slot.retired = true;
                       ++retired_;
                   });
        return ::fixy::Unit{};
    }

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Result<std::uint64_t, TcamError>
    query_counter(OwnedTcamRule const& handle) const noexcept {
        CRUCIBLE_TRY(index, slot_for(handle.peek()));
        return slots_.at(index).counter;
    }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError> note_match(OwnedTcamRule const& handle,
                                                                               std::uint64_t count = 1) noexcept {
        CRUCIBLE_TRY(index, slot_for(handle.peek()));
        Slot& slot = slots_.at(index);
        CRUCIBLE_TRY(updated, ::foundation::core::checked_add(slot.counter, count).ok_or(TcamError::CounterOverflow));
        slot.counter = updated;
        return ::fixy::Unit{};
    }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, TcamError> require_backend_ready() const noexcept {
        if (!plan_.value().backend_ready()) {
            return ::fixy::err(TcamError::VendorBackendUnavailable);
        }
        return ::fixy::Unit{};
    }
};

// Carries [[deprecated]] not because it is going away but because the
// attribute makes every call site warn, so a stub cannot be reached without
// notice at compile time.  A table whose backend_ready flag is set makes this
// return success while programming nothing.
[[nodiscard, deprecated("CRUCIBLE_STUB: no vendor path installs the rule on the "
                        "device. The success path is substrate bookkeeping, not device programming")]]
constexpr ::fixy::Result<::fixy::Unit, TcamError>
force_tcam_backend_boundary(DeclaredTcamTable const& table, DeclaredTcamFlowRule const& rule) noexcept {
    CRUCIBLE_TRY(_, validate_tcam_rule(rule.value()));
    if (!table.value().backend_ready()) {
        return ::fixy::err(TcamError::VendorBackendUnavailable);
    }
    return ::fixy::Unit{};
}

}  // namespace crucible::cntp::tcam
