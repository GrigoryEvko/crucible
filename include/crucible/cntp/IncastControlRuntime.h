#pragma once

#include <crucible/cntp/IncastControl.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/os/Fs.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
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

    using FlowIndex = ::fixy::Index<MaxFlows>;

    ::foundation::core::FixedArray<FlowSlot, MaxFlows> flows_{};

    // The slot of the flow of fd, or no value when no flow of fd started.
    [[nodiscard]] constexpr ::fixy::Option<FlowIndex> find(cntp::SocketFd fd) const noexcept {
        for (FlowIndex index : flows_.indices()) {
            FlowSlot const& flow = flows_.at(index);
            if (flow.occupied && flow.fd.value() == fd.value()) {
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    [[nodiscard]] constexpr ::fixy::Result<FlowIndex, cntp::IncastError>
    find_or_insert(cntp::SocketFd fd, cntp::PositiveCreditBytes initial_credit) noexcept {
        for (FlowIndex index : find(fd)) {
            flows_.at(index).credit_bytes = initial_credit.value();
            return index;
        }
        for (FlowIndex index : flows_.indices()) {
            FlowSlot& flow = flows_.at(index);
            if (!flow.occupied) {
                flow.occupied = true;
                flow.fd = fd;
                flow.credit_bytes = initial_credit.value();
                flow.sequence = 0;
                return index;
            }
        }
        return ::fixy::err(cntp::IncastError::TooManyFlows);
    }

public:
    constexpr IncastController() noexcept = default;

    // Applying the config reads the kernel's algorithm list, so the context
    // must carry what apply_incast_config demands as well.
    template <class Ctx>
        requires CtxFitsIncastConfigure<Ctx> && ::fixy::fs::CtxFitsFileMint<Ctx, cntp::ProcFileReadMode>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, cntp::IncastError>
    configure_socket(Ctx const& ctx, cntp::SocketFd fd, cntp::DeclaredIncastConfig config) noexcept {
        CRUCIBLE_TRY(_, cntp::apply_incast_config(ctx, fd, config));
        if (config.value().enable_credit_pacing) {
            CRUCIBLE_TRY(_, find_or_insert(fd, config.value().initial_credit_bytes));
        }
        return ::fixy::Unit{};
    }

    template <class Ctx>
        requires CtxFitsIncastConfigure<Ctx>
    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, cntp::IncastError>
    start_credit_flow(Ctx const&, cntp::SocketFd fd, cntp::PositiveCreditBytes initial_credit) noexcept {
        CRUCIBLE_TRY(_, find_or_insert(fd, initial_credit));
        return ::fixy::Unit{};
    }

    template <class Ctx>
        requires CtxFitsIncastCredit<Ctx>
    [[nodiscard]] constexpr ::fixy::Result<IncastCreditGrant, cntp::IncastError>
    issue_credit(Ctx const&, cntp::SocketFd fd, cntp::PositiveCreditBytes bytes, std::uint64_t sequence) noexcept {
        CRUCIBLE_TRY(index, find(fd).ok_or(cntp::IncastError::FlowNotStarted));
        FlowSlot& flow = flows_.at(index);
        const std::uint32_t room = ::fixy::max_of<std::uint32_t> - flow.credit_bytes;
        if (bytes.value() > room) {
            return ::fixy::err(cntp::IncastError::CreditOverflow);
        }
        flow.credit_bytes += bytes.value();
        flow.sequence = sequence;
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
    [[nodiscard]] constexpr ::fixy::Result<cntp::PositiveCreditBytes, cntp::IncastError>
    try_consume_credit(Ctx const&, cntp::SocketFd fd) noexcept {
        CRUCIBLE_TRY(index, find(fd).ok_or(cntp::IncastError::FlowNotStarted));
        FlowSlot& flow = flows_.at(index);
        if (flow.credit_bytes == 0) {
            return ::fixy::err(cntp::IncastError::CreditUnavailable);
        }
        const std::uint32_t granted = ::fixy::exchange(flow.credit_bytes, 0);
        return ::fixy::mint_refined<::fixy::positive>(granted);
    }

    [[nodiscard]] constexpr ::fixy::Result<cntp::PositiveCreditBytes, cntp::IncastError>
    outstanding_credit(cntp::SocketFd fd) const noexcept {
        CRUCIBLE_TRY(index, find(fd).ok_or(cntp::IncastError::FlowNotStarted));
        FlowSlot const& flow = flows_.at(index);
        if (flow.credit_bytes == 0) {
            return ::fixy::err(cntp::IncastError::CreditUnavailable);
        }
        return ::fixy::mint_refined<::fixy::positive>(flow.credit_bytes);
    }
};

template <std::size_t MaxFlows, class Ctx>
    requires ::foundation::effects::IsExecCtx<Ctx>
          && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr IncastController<MaxFlows> mint_incast_controller(Ctx const&) noexcept {
    return {};
}

}  // namespace crucible::cntp
