#pragma once

// CNT-P runtime ownership for incast-control state.
//
// cntp/IncastControl.h owns socket-local DCTCP/RTO primitives. CNT-P owns the
// bounded per-flow credit controller because receiver-issued fan-in pacing is
// transport state, not a congestion-control algorithm by itself.

#include <crucible/cntp/IncastControl.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace crucible::cntp {

// fixy-A5-039 worked-example migration: the named CtxOwnsAnyOf /
// CtxOwnsCapability lift in effects/ExecCtx.h replaces the verbose
// row_contains_v<row_type_of_t<Ctx>, ...> expansion.  Same predicate,
// same cost, grep-discoverable authorization shape.
template <class Ctx>
concept CtxFitsIncastConfigure =
    effects::CtxOwnsAnyOf<Ctx, effects::Effect::Init, effects::Effect::Bg>;

template <class Ctx>
concept CtxFitsIncastCredit =
    effects::CtxOwnsCapability<Ctx, effects::Effect::Bg>;

struct IncastCreditGrant {
    cntp::SocketFd fd{0, typename cntp::SocketFd::Trusted{}};
    cntp::PositiveCreditBytes bytes{std::uint32_t{1}};
    std::uint64_t sequence = 0;
};

template <std::size_t MaxFlows>
class IncastController : public safety::Pinned<IncastController<MaxFlows>> {
    static_assert(MaxFlows > 0, "IncastController requires flow slots");

    struct FlowSlot {
        bool occupied = false;
        cntp::SocketFd fd{0, typename cntp::SocketFd::Trusted{}};
        std::uint32_t credit_bytes = 0;
        std::uint64_t sequence = 0;
    };

    std::array<FlowSlot, MaxFlows> flows_{};

    [[nodiscard]] constexpr FlowSlot* find(cntp::SocketFd fd) noexcept {
        for (auto& flow : flows_) {
            if (flow.occupied && flow.fd.value() == fd.value()) {
                return &flow;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr FlowSlot const*
    find(cntp::SocketFd fd) const noexcept {
        for (auto const& flow : flows_) {
            if (flow.occupied && flow.fd.value() == fd.value()) {
                return &flow;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr std::expected<FlowSlot*, cntp::IncastError>
    find_or_insert(cntp::SocketFd fd,
                   cntp::PositiveCreditBytes initial_credit) noexcept {
        if (FlowSlot* existing = find(fd); existing != nullptr) {
            existing->credit_bytes = initial_credit.value();
            return existing;
        }
        for (auto& flow : flows_) {
            if (!flow.occupied) {
                flow.occupied = true;
                flow.fd = fd;
                flow.credit_bytes = initial_credit.value();
                flow.sequence = 0;
                return &flow;
            }
        }
        return std::unexpected(cntp::IncastError::TooManyFlows);
    }

public:
    constexpr IncastController() noexcept = default;

    template <class Ctx>
        requires CtxFitsIncastConfigure<Ctx>
    [[nodiscard]] std::expected<void, cntp::IncastError>
    configure_socket(Ctx const&,
                     cntp::SocketFd fd,
                     cntp::DeclaredIncastConfig config) noexcept {
        auto applied = cntp::apply_incast_config(fd, config);
        if (!applied.has_value()) {
            return std::unexpected(applied.error());
        }
        if (config.value().enable_credit_pacing) {
            auto flow = find_or_insert(fd, config.value().initial_credit_bytes);
            if (!flow.has_value()) {
                return std::unexpected(flow.error());
            }
        }
        return {};
    }

    template <class Ctx>
        requires CtxFitsIncastConfigure<Ctx>
    [[nodiscard]] constexpr std::expected<void, cntp::IncastError>
    start_credit_flow(Ctx const&,
                      cntp::SocketFd fd,
                      cntp::PositiveCreditBytes initial_credit) noexcept {
        auto flow = find_or_insert(fd, initial_credit);
        if (!flow.has_value()) {
            return std::unexpected(flow.error());
        }
        return {};
    }

    template <class Ctx>
        requires CtxFitsIncastCredit<Ctx>
    [[nodiscard]] constexpr std::expected<IncastCreditGrant, cntp::IncastError>
    issue_credit(Ctx const&,
                 cntp::SocketFd fd,
                 cntp::PositiveCreditBytes bytes,
                 std::uint64_t sequence) noexcept {
        FlowSlot* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(cntp::IncastError::FlowNotStarted);
        }
        const std::uint32_t room =
            std::numeric_limits<std::uint32_t>::max() - flow->credit_bytes;
        if (bytes.value() > room) {
            return std::unexpected(cntp::IncastError::CreditOverflow);
        }
        flow->credit_bytes += bytes.value();
        flow->sequence = sequence;
        return IncastCreditGrant{
            .fd = fd,
            .bytes = bytes,
            .sequence = sequence,
        };
    }

    // fixy-A5-005: poll-once-fast consume of issued credit.  The
    // previous name `await_credit` falsely promised a blocking wait —
    // no waiting happens; the controller is a single-threaded BG-drain
    // state machine and a real wait would be a self-deadlock against
    // its own issue_credit call site.  The unused PositiveRtoMinUsec
    // parameter (which encoded a timeout that nothing observed) is
    // dropped.  Callers that want producer-consumer queueing wire
    // PermissionedSpscChannel<PositiveCreditBytes, ...> over this
    // accessor and block on the channel side, never inside the flow
    // controller.
    template <class Ctx>
        requires CtxFitsIncastCredit<Ctx>
    [[nodiscard]] constexpr std::expected<
        cntp::PositiveCreditBytes, cntp::IncastError>
    try_consume_credit(Ctx const&, cntp::SocketFd fd) noexcept {
        FlowSlot* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(cntp::IncastError::FlowNotStarted);
        }
        if (flow->credit_bytes == 0) {
            return std::unexpected(cntp::IncastError::CreditUnavailable);
        }
        const std::uint32_t granted = flow->credit_bytes;
        flow->credit_bytes = 0;
        return cntp::PositiveCreditBytes{
            granted, typename cntp::PositiveCreditBytes::Trusted{}};
    }

    [[nodiscard]] constexpr std::expected<
        cntp::PositiveCreditBytes, cntp::IncastError>
    outstanding_credit(cntp::SocketFd fd) const noexcept {
        FlowSlot const* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(cntp::IncastError::FlowNotStarted);
        }
        if (flow->credit_bytes == 0) {
            return std::unexpected(cntp::IncastError::CreditUnavailable);
        }
        return cntp::PositiveCreditBytes{
            flow->credit_bytes, typename cntp::PositiveCreditBytes::Trusted{}};
    }
};

template <std::size_t MaxFlows, class Ctx>
    requires effects::IsExecCtx<Ctx>
          && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                                     effects::Effect::Init>
[[nodiscard]] constexpr IncastController<MaxFlows>
mint_incast_controller(Ctx const&) noexcept {
    return {};
}

static_assert(std::is_trivially_copyable_v<IncastCreditGrant>);
static_assert(CtxFitsIncastConfigure<effects::ColdInitCtx>);
static_assert(CtxFitsIncastConfigure<effects::BgDrainCtx>);
static_assert(!CtxFitsIncastConfigure<effects::HotFgCtx>);
static_assert(CtxFitsIncastCredit<effects::BgDrainCtx>);
static_assert(!CtxFitsIncastCredit<effects::HotFgCtx>);

}  // namespace crucible::cntp
