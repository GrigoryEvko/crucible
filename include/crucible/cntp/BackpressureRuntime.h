#pragma once

#include <crucible/cntp/Backpressure.h>
#include <fixy/Ctx.h>
#include <fixy/os/SpinLock.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

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

    static constexpr std::uint32_t kEmptyFd = static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1u;
    static constexpr std::uint32_t kReservedFd = kEmptyFd + 1u;

    // Cache-line aligned so that two flows never share a line.  Independent
    // producers update credit_bytes concurrently, and adjacent slots on one
    // line would contend on every read-modify-write.  The two atomics inside a
    // slot stay together on purpose: both belong to the same flow's producer,
    // and only contention between flows is a problem.
    struct alignas(64) FlowSlot {
        std::atomic<std::uint32_t> fd_bits{kEmptyFd};
        std::atomic<std::uint32_t> credit_bytes{0};
    };

    static_assert(alignof(FlowSlot) >= 64, "FlowSlot must be cache-line-aligned so that adjacent slots "
                                           "in CreditFlowControl::flows_ land on distinct cache lines "
                                           "under concurrent grant/consume from independent flows");
    static_assert(sizeof(FlowSlot) >= 64, "FlowSlot occupies a full cache line; trailing padding is "
                                          "intentional — see false-sharing rationale above");
    static_assert(sizeof(std::array<FlowSlot, 2>) >= 128, "Two adjacent FlowSlots must span at least two cache lines");

    // A standard library substitutes mutex-backed atomics on an ISA that lacks
    // the intrinsic, and it does so silently.  A hidden mutex inside grant and
    // consume is worse than a failed build.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "std::atomic<uint32_t> must be lock-free on this target");

    // Each start_flow mints a token of this tag, so the token witnesses that
    // the acquisition comes from inside this class.
    struct StartGateTag {
        using permission_row = ::foundation::effects::Row<>;
    };

    std::array<FlowSlot, MaxFlows> flows_{};
    // start_flow searches the table and then reserves a slot, and those two
    // steps are not atomic together.  The gate serializes them so one socket
    // cannot be published into two slots at once.
    ::fixy::spin::BlockingLock<StartGateTag> start_gate_{};

    [[nodiscard]] static constexpr std::uint32_t fd_key(SocketFd fd) noexcept {
        return static_cast<std::uint32_t>(fd.value());
    }

    [[nodiscard]] FlowSlot* find(SocketFd fd) noexcept {
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
        requires CtxFitsBackpressureStart<Ctx>
    [[nodiscard]] std::expected<void, BackpressureError> start_flow(Ctx const& ctx, SocketFd fd,
                                                                    PositiveBackpressureBytes initial_credit) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<StartGateTag>();
        ::fixy::spin::GateGuard guard{ctx, start_gate_, proof};

        if (FlowSlot* existing = find(fd); existing != nullptr) {
            existing->credit_bytes.store(initial_credit.value(), std::memory_order_release);
            return {};
        }

        const std::uint32_t key = fd_key(fd);
        for (auto& flow : flows_) {
            std::uint32_t expected = kEmptyFd;
            if (flow.fd_bits.compare_exchange_strong(expected, kReservedFd, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
                flow.credit_bytes.store(initial_credit.value(), std::memory_order_release);
                flow.fd_bits.store(key, std::memory_order_release);
                return {};
            }
        }
        return std::unexpected(BackpressureError::TooManyCreditFlows);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] std::expected<void, BackpressureError> grant_credit(Ctx const&, SocketFd fd,
                                                                      PositiveBackpressureBytes bytes) noexcept {
        FlowSlot* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(BackpressureError::CreditFlowNotStarted);
        }

        std::uint32_t observed = flow->credit_bytes.load(std::memory_order_acquire);
        do {
            const std::uint32_t room = std::numeric_limits<std::uint32_t>::max() - observed;
            if (bytes.value() > room) {
                return std::unexpected(BackpressureError::CreditOverflow);
            }
        } while (!flow->credit_bytes.compare_exchange_weak(observed, observed + bytes.value(),
                                                           std::memory_order_acq_rel, std::memory_order_acquire));
        return {};
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] std::expected<void, BackpressureError> consume_credit(Ctx const&, SocketFd fd,
                                                                        PositiveBackpressureBytes bytes) noexcept {
        FlowSlot* flow = find(fd);
        if (flow == nullptr) {
            return std::unexpected(BackpressureError::CreditFlowNotStarted);
        }

        std::uint32_t observed = flow->credit_bytes.load(std::memory_order_acquire);
        do {
            if (observed < bytes.value()) {
                return std::unexpected(BackpressureError::CreditExhausted);
            }
        } while (!flow->credit_bytes.compare_exchange_weak(observed, observed - bytes.value(),
                                                           std::memory_order_acq_rel, std::memory_order_acquire));
        return {};
    }

    [[nodiscard]] std::expected<PositiveBackpressureBytes, BackpressureError>
    current_credit(SocketFd fd) const noexcept {
        const std::uint32_t key = fd_key(fd);
        for (auto const& flow : flows_) {
            if (flow.fd_bits.load(std::memory_order_acquire) == key) {
                return admit_backpressure_credit(flow.credit_bytes.load(std::memory_order_acquire))
                    .transform_error([](BackpressureError) noexcept { return BackpressureError::CreditExhausted; });
            }
        }
        return std::unexpected(BackpressureError::CreditFlowNotStarted);
    }
};

template <std::size_t MaxConnections, std::size_t MaxResourceLimits>
class AdmissionController : public ::foundation::Pinned<AdmissionController<MaxConnections, MaxResourceLimits>> {
    static_assert(MaxConnections > 0, "AdmissionController requires connections");
    static_assert(MaxConnections <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()),
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

    std::array<LimitSlot, MaxResourceLimits> limits_{};
    std::atomic<std::uint16_t> live_connections_{0};
    std::atomic<std::uint64_t> sequence_{0};

    static_assert(std::atomic<std::uint16_t>::is_always_lock_free,
                  "std::atomic<uint16_t> must be lock-free on this target");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "std::atomic<uint64_t> must be lock-free on this target");

    [[nodiscard]] DeclaredAdmissionDecision decision(AdmissionDecisionKind kind, SocketFd socket,
                                                     ::foundation::effects::ResourceKind resource,
                                                     ResourcePressurePpm observed, ResourceLimitPpm threshold,
                                                     std::uint32_t retry_after_ms) noexcept {
        const std::uint64_t sequence = sequence_.fetch_add(1, std::memory_order_acq_rel) + 1u;
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
    [[nodiscard]] std::expected<void, BackpressureError> register_resource_limit(Ctx const&,
                                                                                 ResourceLimit limit) noexcept {
        for (auto& slot : limits_) {
            if (slot.occupied && slot.limit.kind() == limit.kind()) {
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
        return std::unexpected(BackpressureError::TooManyResourceLimits);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    [[nodiscard]] std::expected<DeclaredAdmissionDecision, BackpressureError>
    try_accept_connection(Ctx const&, ConnectionRequest request, std::span<const ResourcePressure> pressures,
                          std::uint32_t retry_after_ms = 1) noexcept {
        for (auto pressure : pressures) {
            for (auto const& slot : limits_) {
                if (slot.occupied && resource_pressure_exceeds(pressure, slot.limit)) {
                    return decision(AdmissionDecisionKind::RejectedResource, request.socket, pressure.kind(),
                                    pressure.used_ppm(), slot.limit.reject_at_or_above_ppm(), retry_after_ms);
                }
            }
        }

        std::uint16_t observed = live_connections_.load(std::memory_order_acquire);
        do {
            if (observed >= kMaxConnections) {
                return decision(AdmissionDecisionKind::RejectedBackoff, request.socket,
                                ::foundation::effects::ResourceKind::NicQ, kNoPressure, kLowestLimit, retry_after_ms);
            }
        } while (!live_connections_.compare_exchange_weak(observed, static_cast<std::uint16_t>(observed + 1u),
                                                          std::memory_order_acq_rel, std::memory_order_acquire));

        return decision(AdmissionDecisionKind::Accepted, request.socket, ::foundation::effects::ResourceKind::NicQ,
                        kNoPressure, kFullScaleLimit, 0);
    }

    template <class Ctx>
        requires CtxFitsBackpressureRuntime<Ctx>
    void release_connection(Ctx const&) noexcept {
        std::uint16_t observed = live_connections_.load(std::memory_order_acquire);
        while (observed != 0
               && !live_connections_.compare_exchange_weak(observed, static_cast<std::uint16_t>(observed - 1u),
                                                           std::memory_order_acq_rel, std::memory_order_acquire)) {}
    }

    [[nodiscard]] std::uint16_t live_connections() const noexcept {
        return live_connections_.load(std::memory_order_acquire);
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

static_assert(CtxFitsBackpressureMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsBackpressureMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsBackpressureRuntime<::fixy::BgDrainCtx>);
static_assert(CtxFitsBackpressureRuntime<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsBackpressureRuntime<::fixy::HotFgCtx>);
static_assert(CtxFitsBackpressureStart<::fixy::BgLoadCtx>);
static_assert(CtxFitsBackpressureStart<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsBackpressureStart<::fixy::BgDrainCtx>,
              "a context that owns no Block cannot wait on the start gate");

}  // namespace crucible::cntp
