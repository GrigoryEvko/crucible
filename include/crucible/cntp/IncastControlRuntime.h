#pragma once

#include <crucible/cntp/IncastControl.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/os/Fs.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace crucible::cntp {

template <class Ctx>
concept CtxFitsIncastConfigure =
    ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Init, ::foundation::effects::Effect::Bg>;

template <class Ctx>
concept CtxFitsIncastCredit = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

struct IncastCreditGrant {
    cntp::SocketFd fd = ::fixy::mint_refined<::fixy::non_negative>(0);
    cntp::PositiveCreditBytes bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
    std::uint64_t sequence = 0;
};

template <std::size_t MaxFlows>
class IncastController : public ::foundation::Pinned<IncastController<MaxFlows>> {
    static_assert(MaxFlows > 0, "IncastController requires flow slots");

    struct FlowSlot {
        bool occupied = false;
        cntp::SocketFd fd = ::fixy::mint_refined<::fixy::non_negative>(0);
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

    [[nodiscard]] constexpr FlowSlot const* find(cntp::SocketFd fd) const noexcept {
        for (auto const& flow : flows_) {
            if (flow.occupied && flow.fd.value() == fd.value()) {
                return &flow;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr std::expected<FlowSlot*, cntp::IncastError>
    find_or_insert(cntp::SocketFd fd, cntp::PositiveCreditBytes initial_credit) noexcept {
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

    // Applying the config reads the kernel's algorithm list, so the context
    // must carry what apply_incast_config demands as well.
    template <class Ctx>
        requires CtxFitsIncastConfigure<Ctx> && ::fixy::fs::CtxFitsFileMint<Ctx, cntp::ProcFileReadMode>
    [[nodiscard]] std::expected<void, cntp::IncastError> configure_socket(Ctx const& ctx, cntp::SocketFd fd,
                                                                          cntp::DeclaredIncastConfig config) noexcept {
        auto applied = cntp::apply_incast_config(ctx, fd, config);
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
    start_credit_flow(Ctx const&, cntp::SocketFd fd, cntp::PositiveCreditBytes initial_credit) noexcept {
        auto flow = find_or_insert(fd, initial_credit);
        if (!flow.has_value()) {
            return std::unexpected(flow.error());
        }
        return {};
    }

    template <class Ctx>
        requires CtxFitsIncastCredit<Ctx>
    [[nodiscard]] constexpr std::expected<IncastCreditGrant, cntp::IncastError>
    issue_credit(Ctx const&, cntp::SocketFd fd, cntp::PositiveCreditBytes bytes, std::uint64_t sequence) noexcept {
        FlowSlot* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(cntp::IncastError::FlowNotStarted);
        }
        const std::uint32_t room = std::numeric_limits<std::uint32_t>::max() - flow->credit_bytes;
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

    // This never waits.  The controller is a single-threaded drain-side state
    // machine, and waiting inside it would deadlock against its own
    // issue_credit call site.  A caller that wants producer-consumer queueing
    // blocks on a channel wrapped around this accessor, never in here.
    template <class Ctx>
        requires CtxFitsIncastCredit<Ctx>
    [[nodiscard]] constexpr std::expected<cntp::PositiveCreditBytes, cntp::IncastError>
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
        return ::fixy::mint_refined<::fixy::positive>(granted);
    }

    [[nodiscard]] constexpr std::expected<cntp::PositiveCreditBytes, cntp::IncastError>
    outstanding_credit(cntp::SocketFd fd) const noexcept {
        FlowSlot const* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(cntp::IncastError::FlowNotStarted);
        }
        if (flow->credit_bytes == 0) {
            return std::unexpected(cntp::IncastError::CreditUnavailable);
        }
        return ::fixy::mint_refined<::fixy::positive>(flow->credit_bytes);
    }
};

template <std::size_t MaxFlows, class Ctx>
    requires ::foundation::effects::IsExecCtx<Ctx>
             && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr IncastController<MaxFlows> mint_incast_controller(Ctx const&) noexcept {
    return {};
}

static_assert(std::is_trivially_copy_constructible_v<IncastCreditGrant>
              && std::is_trivially_destructible_v<IncastCreditGrant>);
static_assert(CtxFitsIncastConfigure<::fixy::ColdInitCtx>);
static_assert(CtxFitsIncastConfigure<::fixy::BgDrainCtx>);
static_assert(!CtxFitsIncastConfigure<::fixy::HotFgCtx>);
static_assert(CtxFitsIncastCredit<::fixy::BgDrainCtx>);
static_assert(!CtxFitsIncastCredit<::fixy::HotFgCtx>);
// Configuring a socket also reads /proc, so only a context whose row holds
// IO and Block may do it.
static_assert(::fixy::fs::CtxFitsFileMint<::fixy::InitLoadCtx, ProcFileReadMode>);
static_assert(!::fixy::fs::CtxFitsFileMint<::fixy::ColdInitCtx, ProcFileReadMode>);

}  // namespace crucible::cntp
