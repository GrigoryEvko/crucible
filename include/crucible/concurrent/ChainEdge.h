#pragma once

// ChainEdge — semaphore-driven causal edge between execution plans.
//
// This is the substrate GAPS-062 needs before the permissioned/session
// facade can exist.  It models the host-visible part of a device
// semaphore edge: upstream plan signals a value, downstream plan waits
// for that value.  The current implementation routes every vendor
// through mimic::<vendor>::semaphore_signal/wait stubs backed by a CPU
// oracle atomic; future vendor backends replace the stub bodies without
// changing the ChainEdge or session surface.

#include <crucible/algebra/lattices/VendorLattice.h>
#include <crucible/mimic/Semaphore.h>
#include <crucible/safety/Pinned.h>

#include <atomic>
#include <compare>
#include <cstdint>
#include <type_traits>

namespace crucible::concurrent {

using ::crucible::algebra::lattices::VendorBackend;

struct PlanId {
private:
    std::uint32_t value_ = 0;

public:
    constexpr PlanId() noexcept = default;
    explicit constexpr PlanId(std::uint32_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
        return value_;
    }

    constexpr auto operator<=>(const PlanId&) const noexcept = default;
};

struct ChainEdgeId {
private:
    std::uint32_t value_ = 0;

public:
    constexpr ChainEdgeId() noexcept = default;
    explicit constexpr ChainEdgeId(std::uint32_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
        return value_;
    }

    constexpr auto operator<=>(const ChainEdgeId&) const noexcept = default;
};

struct SemaphoreSignal {
    ChainEdgeId edge;
    PlanId upstream;
    PlanId downstream;
    std::uint64_t value = 0;
    VendorBackend backend = VendorBackend::CPU;

    constexpr auto operator<=>(const SemaphoreSignal&) const noexcept = default;
};

static_assert(std::is_trivially_copyable_v<PlanId>);
static_assert(std::is_trivially_copyable_v<ChainEdgeId>);
static_assert(std::is_trivially_copyable_v<SemaphoreSignal>);

template <VendorBackend Backend = VendorBackend::CPU>
class ChainEdge : public safety::Pinned<ChainEdge<Backend>> {
public:
    static constexpr VendorBackend backend = Backend;

    ChainEdge(PlanId upstream,
              PlanId downstream,
              ChainEdgeId edge,
              std::uint64_t signal_value = 1) noexcept
        : upstream_{upstream}
        , downstream_{downstream}
        , edge_{edge}
        , signal_value_{signal_value}
        , semaphore_{Backend, edge.raw(), &value_}
    {}

    ChainEdge(const ChainEdge&) = delete;
    ChainEdge& operator=(const ChainEdge&) = delete;
    ChainEdge(ChainEdge&&) = delete;
    ChainEdge& operator=(ChainEdge&&) = delete;

    [[nodiscard]] constexpr PlanId upstream_plan() const noexcept {
        return upstream_;
    }

    [[nodiscard]] constexpr PlanId downstream_plan() const noexcept {
        return downstream_;
    }

    [[nodiscard]] constexpr ChainEdgeId edge_id() const noexcept {
        return edge_;
    }

    [[nodiscard]] constexpr std::uint64_t signal_value() const noexcept {
        return signal_value_;
    }

    [[nodiscard]] SemaphoreSignal expected_signal() const noexcept {
        return SemaphoreSignal{
            .edge = edge_,
            .upstream = upstream_,
            .downstream = downstream_,
            .value = signal_value_,
            .backend = Backend,
        };
    }

    void signal(const SemaphoreSignal& signal) noexcept {
        mimic::detail::semaphore_signal<Backend>(semaphore_, signal.value);
    }

    [[nodiscard]] bool wait(const SemaphoreSignal& signal) const noexcept {
        return mimic::detail::semaphore_wait<Backend>(semaphore_, signal.value);
    }

    [[nodiscard]] std::uint64_t current_value() const noexcept {
        return value_.load(std::memory_order_acquire);
    }

    void reset_under_quiescence(std::uint64_t value = 0) noexcept {
        value_.store(value, std::memory_order_release);
    }

private:
    PlanId upstream_;
    PlanId downstream_;
    ChainEdgeId edge_;
    std::uint64_t signal_value_;
    mutable std::atomic<std::uint64_t> value_{0};
    mimic::DeviceSemaphore semaphore_;
};

}  // namespace crucible::concurrent
