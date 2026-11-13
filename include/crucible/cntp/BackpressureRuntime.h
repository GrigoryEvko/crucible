#pragma once

#include <crucible/cntp/Backpressure.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/SpinLock.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>

namespace crucible::cntp {

template <class Ctx>
concept CtxFitsBackpressureMint = ::foundation::effects::IsExecCtx<Ctx>
                               && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

template <class Ctx>
concept CtxFitsBackpressureRuntime =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Test>;

// Starting a flow waits on the start gate, and that wait is a block.
template <class Ctx>
concept CtxFitsBackpressureStart = CtxFitsBackpressureRuntime<Ctx> && ::fixy::spin::CtxMayBlock<Ctx>;

template <std::size_t MaxFlows>
class CreditFlowControl : public ::foundation::Pinned<CreditFlowControl<MaxFlows>> {
    static_assert(MaxFlows > 0, "CreditFlowControl requires flow slots");

    static constexpr std::uint32_t kEmptyFd = static_cast<std::uint32_t>(::fixy::max_of<int>) + 1u;
    static constexpr std::uint32_t kReservedFd = kEmptyFd + 1u;

    // Cache-line aligned so that two flows never share a line.  Independent
    // producers update credit_bytes concurrently, and adjacent slots on one
    // line would contend on every read-modify-write.  The two atomics inside a
    // slot stay together on purpose: both belong to the same flow's producer,
    // and only contention between flows is a problem.
    struct alignas(64) FlowSlot {
        ::fixy::Atomic<std::uint32_t> fd_bits{kEmptyFd};
        ::fixy::Atomic<std::uint32_t> credit_bytes{0};
    };

    static_assert(alignof(FlowSlot) >= 64, "FlowSlot must be cache-line-aligned so that adjacent slots "
                                           "in CreditFlowControl::flows_ land on distinct cache lines "
                                           "under concurrent grant/consume from independent flows");
    static_assert(sizeof(FlowSlot) >= 64, "FlowSlot occupies a full cache line; trailing padding is "
                                          "intentional — see false-sharing rationale above");
    static_assert(sizeof(::foundation::core::FixedArray<FlowSlot, 2>) >= 128,
                  "Two adjacent FlowSlots must span at least two cache lines");

    using FlowIndex = ::fixy::Index<MaxFlows>;

    // Each start_flow mints a token of this tag, so the token witnesses that
    // the acquisition comes from inside this class.
    struct StartGateTag {
        using permission_row = ::foundation::effects::Row<>;
    };

    ::foundation::core::FixedArray<FlowSlot, MaxFlows> flows_{};
    // start_flow searches the table and then reserves a slot, and those two
    // steps are not atomic together.  The gate serializes them so one socket
    // cannot be published into two slots at once.
    ::fixy::spin::BlockingLock<StartGateTag> start_gate_{};

    [[nodiscard]] static constexpr std::uint32_t fd_key(SocketFd fd) noexcept {
        return static_cast<std::uint32_t>(fd.value());
    }

    // The slot of the flow of fd, or no value when no flow of fd started.
    [[nodiscard]] ::fixy::Option<FlowIndex> find(SocketFd fd) const noexcept {
        const std::uint32_t key = fd_key(fd);
        for (FlowIndex index : flows_.indices()) {
            if (flows_.at(index).fd_bits.load_acquire() == key) {
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    // One compare-and-swap of the credit of a flow.  True when it stored
    // desired.  Else observed takes the credit that the cell held.
    [[nodiscard]] static bool swap_credit(FlowSlot& flow, std::uint32_t& observed, std::uint32_t desired) noexcept {
        return flow.credit_bytes.cas_acq_rel(observed, desired)
            .match([](::fixy::Unit) noexcept { return true; },
                   [&observed](::fixy::CasRefusal<std::uint32_t> refusal) noexcept {
                       observed = refusal.observed;
                       return false;
                   });
    }

public:
    constexpr CreditFlowControl() noexcept = default;

    template <class Ctx>
        requires CtxFitsBackpressureStart<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, BackpressureError>
    start_flow(Ctx const& ctx, SocketFd fd, PositiveBackpressureBytes initial_credit) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<StartGateTag>();
        ::fixy::spin::GateGuard guard{ctx, start_gate_, proof};

        for (FlowIndex index : find(fd)) {
            flows_.at(index).credit_bytes.store_release(initial_credit.value());
            return ::fixy::Unit{};
        }

        const std::uint32_t key = fd_key(fd);
        for (FlowSlot& flow : flows_) {
            if (flow.fd_bits.cas_acq_rel(kEmptyFd, kReservedFd).is_ok()) {
                flow.credit_bytes.store_release(initial_credit.value());
                flow.fd_bits.store_release(key);
                return ::fixy::Unit{};
            }
        }
        return ::fixy::err(BackpressureError::TooManyCreditFlows);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, BackpressureError>
    grant_credit(Ctx const&, SocketFd fd, PositiveBackpressureBytes bytes) noexcept {
        CRUCIBLE_TRY(index, find(fd).ok_or(BackpressureError::CreditFlowNotStarted));
        FlowSlot& flow = flows_.at(index);

        std::uint32_t observed = flow.credit_bytes.load_acquire();
        for (;;) {
            if (bytes.value() > ::fixy::max_of<std::uint32_t> - observed) {
                return ::fixy::err(BackpressureError::CreditOverflow);
            }
            if (swap_credit(flow, observed, observed + bytes.value())) {
                return ::fixy::Unit{};
            }
        }
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, BackpressureError>
    consume_credit(Ctx const&, SocketFd fd, PositiveBackpressureBytes bytes) noexcept {
        CRUCIBLE_TRY(index, find(fd).ok_or(BackpressureError::CreditFlowNotStarted));
        FlowSlot& flow = flows_.at(index);

        std::uint32_t observed = flow.credit_bytes.load_acquire();
        for (;;) {
            if (observed < bytes.value()) {
                return ::fixy::err(BackpressureError::CreditExhausted);
            }
            if (swap_credit(flow, observed, observed - bytes.value())) {
                return ::fixy::Unit{};
            }
        }
    }

    [[nodiscard]] ::fixy::Result<PositiveBackpressureBytes, BackpressureError>
    current_credit(SocketFd fd) const noexcept {
        CRUCIBLE_TRY(index, find(fd).ok_or(BackpressureError::CreditFlowNotStarted));
        const std::uint32_t credit = flows_.at(index).credit_bytes.load_acquire();
        if (credit == 0) {
            return ::fixy::err(BackpressureError::CreditExhausted);
        }
        return ::fixy::mint_refined<::fixy::positive>(credit);
    }
};

template <std::size_t MaxConnections, std::size_t MaxResourceLimits>
class AdmissionController : public ::foundation::Pinned<AdmissionController<MaxConnections, MaxResourceLimits>> {
    static_assert(MaxConnections > 0, "AdmissionController requires connections");
    static_assert(MaxConnections <= std::size_t{::fixy::max_of<std::uint16_t>},
                  "AdmissionController connection count is uint16-backed");
    static_assert(MaxResourceLimits > 0, "AdmissionController requires resource limits");

    static constexpr std::uint16_t kMaxConnections = static_cast<std::uint16_t>(MaxConnections);

    // The pressure and the limit that a decision reports when no resource
    // limited it.  A backoff for the connection count reports the lowest
    // limit, and an acceptance reports the full scale.
    static constexpr ResourcePressurePpm kNoPressure = ::fixy::mint_refined<resource_pressure_ppm>(std::uint32_t{0});
    static constexpr ResourceLimitPpm kLowestLimit = ::fixy::mint_refined<resource_limit_ppm>(std::uint32_t{1});
    static constexpr ResourceLimitPpm kFullScaleLimit = ::fixy::mint_refined<resource_limit_ppm>(kPpmFull);

    struct LimitSlot {
        bool occupied = false;
        ResourceLimit limit{};
    };

    ::foundation::core::FixedArray<LimitSlot, MaxResourceLimits> limits_{};
    ::fixy::Atomic<std::uint16_t> live_connections_{0};
    ::fixy::Atomic<std::uint64_t> sequence_{0};

    [[nodiscard]] DeclaredAdmissionDecision decision(AdmissionDecisionKind kind, SocketFd socket,
                                                     ::foundation::effects::ResourceKind resource,
                                                     ResourcePressurePpm observed, ResourceLimitPpm threshold,
                                                     std::uint32_t retry_after_ms) noexcept {
        const std::uint64_t sequence = sequence_.fetch_add_acq_rel(1) + 1u;
        return mint_admission_decision(AdmissionDecision{
            .kind = kind,
            .socket = socket,
            .limiting_resource = resource,
            .observed_ppm = observed,
            .threshold_ppm = threshold,
            .retry_after_ms = retry_after_ms,
            .sequence = sequence,
        });
    }

public:
    constexpr AdmissionController() noexcept = default;

    template <class Ctx>
        requires CtxFitsBackpressureMint<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, BackpressureError>
    register_resource_limit(Ctx const&, ResourceLimit limit) noexcept {
        for (LimitSlot& slot : limits_) {
            if (slot.occupied && slot.limit.kind() == limit.kind()) {
                slot.limit = limit;
                return ::fixy::Unit{};
            }
        }
        for (LimitSlot& slot : limits_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.limit = limit;
                return ::fixy::Unit{};
            }
        }
        return ::fixy::err(BackpressureError::TooManyResourceLimits);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] ::fixy::Result<DeclaredAdmissionDecision, BackpressureError>
    try_accept_connection(Ctx const&, ConnectionRequest request, ::fixy::View<ResourcePressure const> pressures,
                          std::uint32_t retry_after_ms = 1) noexcept {
        for (ResourcePressure const& pressure : pressures) {
            for (LimitSlot const& slot : limits_) {
                if (slot.occupied && resource_pressure_exceeds(pressure, slot.limit)) {
                    return decision(AdmissionDecisionKind::RejectedResource, request.socket, pressure.kind(),
                                    pressure.used_ppm(), slot.limit.reject_at_or_above_ppm(), retry_after_ms);
                }
            }
        }

        std::uint16_t observed = live_connections_.load_acquire();
        for (;;) {
            if (observed >= kMaxConnections) {
                return decision(AdmissionDecisionKind::RejectedBackoff, request.socket,
                                ::foundation::effects::ResourceKind::NicQ, kNoPressure, kLowestLimit, retry_after_ms);
            }
            if (swap_live(observed, static_cast<std::uint16_t>(observed + 1u))) {
                break;
            }
        }

        return decision(AdmissionDecisionKind::Accepted, request.socket, ::foundation::effects::ResourceKind::NicQ,
                        kNoPressure, kFullScaleLimit, 0);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    void release_connection(Ctx const&) noexcept {
        std::uint16_t observed = live_connections_.load_acquire();
        while (observed != 0 && !swap_live(observed, static_cast<std::uint16_t>(observed - 1u))) {}
    }

    [[nodiscard]] std::uint16_t live_connections() const noexcept { return live_connections_.load_acquire(); }

private:
    // One compare-and-swap of the live count.  True when it stored desired.
    // Else observed takes the count that the cell held.
    [[nodiscard]] bool swap_live(std::uint16_t& observed, std::uint16_t desired) noexcept {
        return live_connections_.cas_acq_rel(observed, desired)
            .match([](::fixy::Unit) noexcept { return true; },
                   [&observed](::fixy::CasRefusal<std::uint16_t> refusal) noexcept {
                       observed = refusal.observed;
                       return false;
                   });
    }
};

template <std::size_t MaxFlows, class Ctx>
    requires CtxFitsBackpressureMint<Ctx>
[[nodiscard]] constexpr CreditFlowControl<MaxFlows> mint_credit_flow_control(Ctx const&) noexcept {
    return {};
}

template <std::size_t MaxConnections, std::size_t MaxResourceLimits, class Ctx>
    requires CtxFitsBackpressureMint<Ctx>
[[nodiscard]] constexpr AdmissionController<MaxConnections, MaxResourceLimits>
mint_admission_controller(Ctx const&) noexcept {
    return {};
}

}  // namespace crucible::cntp
