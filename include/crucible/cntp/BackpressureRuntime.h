#pragma once

// CNT-P runtime ownership for backpressure state.
//
// This is the mutable half of GAPS-137. The transport-facing CNT-P header
// defines typed facts; CNT-P owns live counters. Both controllers are bounded,
// array-backed, and avoid mutex/futex/heap use. Credit grant/consume stay on
// atomic CAS hot paths; flow creation is serialized by a short spin gate so a
// socket cannot be published into two slots concurrently.

#include <crucible/cntp/Backpressure.h>
#include <crucible/concurrent/SpinLock.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>

namespace crucible::cntp {

template <class Ctx>
concept CtxFitsBackpressureMint =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

template <class Ctx>
concept CtxFitsBackpressureRuntime =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsAnyOf<Ctx, effects::Effect::Bg, effects::Effect::Test>;

template <std::size_t MaxFlows>
class CreditFlowControl : public safety::Pinned<CreditFlowControl<MaxFlows>> {
    static_assert(MaxFlows > 0, "CreditFlowControl requires flow slots");

    static constexpr std::uint32_t kEmptyFd =
        static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1u;
    static constexpr std::uint32_t kReservedFd = kEmptyFd + 1u;

    // fixy-A5-011 follow-up: same false-sharing pattern as the A5-011 named
    // structs (AtomicPingmeshPairCounters, AtomicProbeStats).  FlowSlot is
    // embedded in `std::array<FlowSlot, MaxFlows> flows_` where independent
    // flow producers concurrently fetch_sub on credit_bytes via grant /
    // consume paths.  Without the alignment two adjacent FlowSlots fit on
    // one 64-byte line and contend on every RMW (CLAUDE.md §VIII).  The two
    // intra-struct atomics (fd_bits + credit_bytes = 8 bytes) are intentionally
    // co-located — both belong to the SAME flow's producer; only inter-flow
    // contention is the bug.
    struct alignas(64) FlowSlot {
        std::atomic<std::uint32_t> fd_bits{kEmptyFd};
        std::atomic<std::uint32_t> credit_bytes{0};
    };

    static_assert(alignof(FlowSlot) >= 64,
                  "FlowSlot must be cache-line-aligned so that adjacent slots "
                  "in CreditFlowControl::flows_ land on distinct cache lines "
                  "under concurrent grant/consume from independent flows");
    static_assert(sizeof(FlowSlot) >= 64,
                  "FlowSlot occupies a full cache line; trailing padding is "
                  "intentional — see false-sharing rationale above");
    static_assert(sizeof(std::array<FlowSlot, 2>) >= 128,
                  "Two adjacent FlowSlots must span at least two cache lines");

    // fixy-A5-029: cross-thread atomics on the backpressure RMW path must be
    // lock-free on every supported target.  libstdc++ silently substitutes
    // mutex-backed atomic ops on ISAs lacking the required intrinsic — a
    // hidden mutex inside grant / consume would invert the credit-flow-control
    // latency budget by 100-1000×.  Refuse to build instead of regressing.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "std::atomic<uint32_t> must be lock-free on this target — "
                  "fixy-A5-029");
    static_assert(std::atomic<std::uint16_t>::is_always_lock_free,
                  "std::atomic<uint16_t> must be lock-free on this target — "
                  "fixy-A5-029");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "std::atomic<uint64_t> must be lock-free on this target — "
                  "fixy-A5-029");

    std::array<FlowSlot, MaxFlows> flows_{};
    // fixy-A5-022 + FIXY-U-085 consolidation: prior to the migration this
    // field was a raw `std::atomic_flag start_gate_` with NO alignas(64)
    // discipline, and start_flow used a private nested SpinGuard that lacked
    // the _mm_pause hint.  Adopting the canonical primitive inherits both
    // cache-line isolation (alignas(64) on SpinLock) and the pause-hinted
    // spin loop, closing the false-sharing trap and the missing pause in one
    // change.  See concurrent/SpinLock.h header rationale.
    concurrent::SpinLock start_gate_{};

    [[nodiscard]] static constexpr std::uint32_t
    fd_key(cntp::SocketFd fd) noexcept {
        return static_cast<std::uint32_t>(fd.value());
    }

    [[nodiscard]] FlowSlot* find(cntp::SocketFd fd) noexcept {
        const std::uint32_t key = fd_key(fd);
        for (auto& flow : flows_) {
            if (flow.fd_bits.load(std::memory_order_acquire) == key) {
                return &flow;
            }
        }
        return nullptr;
    }

public:
    constexpr CreditFlowControl() noexcept = default;

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] std::expected<void, cntp::BackpressureError>
    start_flow(Ctx const&,
               cntp::SocketFd fd,
               cntp::PositiveBackpressureBytes initial_credit) noexcept {
        concurrent::SpinGuard guard{start_gate_};

        if (FlowSlot* existing = find(fd); existing != nullptr) {
            existing->credit_bytes.store(initial_credit.value(),
                                         std::memory_order_release);
            return {};
        }

        const std::uint32_t key = fd_key(fd);
        for (auto& flow : flows_) {
            std::uint32_t expected = kEmptyFd;
            if (flow.fd_bits.compare_exchange_strong(
                    expected, kReservedFd, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                flow.credit_bytes.store(initial_credit.value(),
                                        std::memory_order_release);
                flow.fd_bits.store(key, std::memory_order_release);
                return {};
            }
        }
        return std::unexpected(cntp::BackpressureError::TooManyCreditFlows);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] std::expected<void, cntp::BackpressureError>
    grant_credit(Ctx const&,
                 cntp::SocketFd fd,
                 cntp::PositiveBackpressureBytes bytes) noexcept {
        FlowSlot* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(cntp::BackpressureError::CreditFlowNotStarted);
        }

        std::uint32_t observed =
            flow->credit_bytes.load(std::memory_order_acquire);
        do {
            const std::uint32_t room =
                std::numeric_limits<std::uint32_t>::max() - observed;
            if (bytes.value() > room) {
                return std::unexpected(cntp::BackpressureError::CreditOverflow);
            }
        } while (!flow->credit_bytes.compare_exchange_weak(
            observed,
            observed + bytes.value(),
            std::memory_order_acq_rel,
            std::memory_order_acquire));
        return {};
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] std::expected<void, cntp::BackpressureError>
    consume_credit(Ctx const&,
                   cntp::SocketFd fd,
                   cntp::PositiveBackpressureBytes bytes) noexcept {
        FlowSlot* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(cntp::BackpressureError::CreditFlowNotStarted);
        }

        std::uint32_t observed =
            flow->credit_bytes.load(std::memory_order_acquire);
        do {
            if (observed < bytes.value()) {
                return std::unexpected(cntp::BackpressureError::CreditExhausted);
            }
        } while (!flow->credit_bytes.compare_exchange_weak(
            observed,
            observed - bytes.value(),
            std::memory_order_acq_rel,
            std::memory_order_acquire));
        return {};
    }

    [[nodiscard]] std::expected<cntp::PositiveBackpressureBytes,
                                cntp::BackpressureError>
    current_credit(cntp::SocketFd fd) const noexcept {
        const std::uint32_t key = fd_key(fd);
        for (auto const& flow : flows_) {
            if (flow.fd_bits.load(std::memory_order_acquire) == key) {
                const std::uint32_t credit =
                    flow.credit_bytes.load(std::memory_order_acquire);
                if (credit == 0) {
                    return std::unexpected(
                        cntp::BackpressureError::CreditExhausted);
                }
                return cntp::PositiveBackpressureBytes{
                    credit, typename cntp::PositiveBackpressureBytes::Trusted{}};
            }
        }
        return std::unexpected(cntp::BackpressureError::CreditFlowNotStarted);
    }
};

template <std::size_t MaxConnections, std::size_t MaxResourceLimits>
class AdmissionController
    : public safety::Pinned<
          AdmissionController<MaxConnections, MaxResourceLimits>> {
    static_assert(MaxConnections > 0, "AdmissionController requires connections");
    static_assert(MaxConnections <=
                      static_cast<std::size_t>(
                          std::numeric_limits<std::uint16_t>::max()),
                  "AdmissionController connection count is uint16-backed");
    static_assert(MaxResourceLimits > 0,
                  "AdmissionController requires resource limits");

    struct LimitSlot {
        bool occupied = false;
        cntp::ResourceLimit limit{};
    };

    std::array<LimitSlot, MaxResourceLimits> limits_{};
    cntp::PositiveConnectionLimit max_connections_{
        static_cast<std::uint16_t>(MaxConnections),
        typename cntp::PositiveConnectionLimit::Trusted{}};
    std::atomic<std::uint16_t> live_connections_{0};
    std::atomic<std::uint64_t> sequence_{0};

    [[nodiscard]] cntp::DeclaredAdmissionDecision
    decision(cntp::AdmissionDecisionKind kind,
             cntp::SocketFd socket,
             effects::ResourceKind resource,
             cntp::ResourcePressurePpm observed,
             cntp::ResourceLimitPpm threshold,
             std::uint32_t retry_after_ms) noexcept {
        const std::uint64_t sequence =
            sequence_.fetch_add(1, std::memory_order_relaxed) + 1u;
        return cntp::mint_admission_decision(cntp::AdmissionDecision{
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
    [[nodiscard]] std::expected<void, cntp::BackpressureError>
    register_resource_limit(Ctx const&, cntp::ResourceLimit limit) noexcept {
        for (auto& slot : limits_) {
            if (slot.occupied && slot.limit.kind == limit.kind) {
                slot.limit = limit;
                return {};
            }
        }
        for (auto& slot : limits_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.limit = limit;
                return {};
            }
        }
        return std::unexpected(cntp::BackpressureError::TooManyResourceLimits);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] std::expected<cntp::DeclaredAdmissionDecision,
                                cntp::BackpressureError>
    try_accept_connection(Ctx const&,
                          cntp::ConnectionRequest request,
                          std::span<const cntp::ResourcePressure> pressures,
                          std::uint32_t retry_after_ms = 1) noexcept {
        for (auto pressure : pressures) {
            for (auto const& slot : limits_) {
                if (slot.occupied &&
                    cntp::resource_pressure_exceeds(pressure, slot.limit)) {
                    return decision(
                        cntp::AdmissionDecisionKind::RejectedResource,
                        request.socket,
                        pressure.kind,
                        pressure.used_ppm,
                        slot.limit.reject_at_or_above_ppm,
                        retry_after_ms);
                }
            }
        }

        std::uint16_t observed =
            live_connections_.load(std::memory_order_acquire);
        do {
            if (observed >= max_connections_.value()) {
                auto zero = cntp::admit_resource_pressure_ppm(0).value();
                auto limit = cntp::admit_resource_limit_ppm(1).value();
                return decision(
                    cntp::AdmissionDecisionKind::RejectedBackoff,
                    request.socket,
                    effects::ResourceKind::NicQ,
                    zero,
                    limit,
                    retry_after_ms);
            }
        } while (!live_connections_.compare_exchange_weak(
            observed,
            static_cast<std::uint16_t>(observed + 1u),
            std::memory_order_acq_rel,
            std::memory_order_acquire));

        auto zero = cntp::admit_resource_pressure_ppm(0).value();
        auto limit = cntp::admit_resource_limit_ppm(1'000'000).value();
        return decision(
            cntp::AdmissionDecisionKind::Accepted,
            request.socket,
            effects::ResourceKind::NicQ,
            zero,
            limit,
            0);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    void release_connection(Ctx const&) noexcept {
        std::uint16_t observed =
            live_connections_.load(std::memory_order_acquire);
        while (observed != 0 &&
               !live_connections_.compare_exchange_weak(
                   observed,
                   static_cast<std::uint16_t>(observed - 1u),
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {}
    }

    [[nodiscard]] std::uint16_t live_connections() const noexcept {
        return live_connections_.load(std::memory_order_acquire);
    }
};

template <std::size_t MaxFlows, class Ctx>
    requires CtxFitsBackpressureMint<Ctx>
[[nodiscard]] constexpr CreditFlowControl<MaxFlows>
mint_credit_flow_control(Ctx const&) noexcept {
    return {};
}

template <std::size_t MaxConnections,
          std::size_t MaxResourceLimits,
          class Ctx>
    requires CtxFitsBackpressureMint<Ctx>
[[nodiscard]] constexpr AdmissionController<MaxConnections, MaxResourceLimits>
mint_admission_controller(Ctx const&) noexcept {
    return {};
}

static_assert(CtxFitsBackpressureMint<effects::ColdInitCtx>);
static_assert(!CtxFitsBackpressureMint<effects::BgDrainCtx>);
static_assert(CtxFitsBackpressureRuntime<effects::BgDrainCtx>);
static_assert(CtxFitsBackpressureRuntime<effects::TestRunnerCtx>);
static_assert(!CtxFitsBackpressureRuntime<effects::HotFgCtx>);

}  // namespace crucible::cntp
