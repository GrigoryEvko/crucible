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

template <VendorBackend Backend, typename UserTag>
class PermissionedChainEdge;

namespace detail {

class ChainEdgeAccess {
    constexpr ChainEdgeAccess() noexcept = default;

    template <VendorBackend Backend, typename UserTag>
    friend class ::crucible::concurrent::PermissionedChainEdge;
};

}  // namespace detail

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

    void signal(const SemaphoreSignal&) noexcept
        = delete("raw ChainEdge::signal is substrate-only; use PermissionedChainEdge::SignalerHandle so the Signaler Permission gates the operation.");

    [[nodiscard]] bool wait(const SemaphoreSignal&) const noexcept
        = delete("raw ChainEdge::wait is substrate-only; use PermissionedChainEdge::WaiterHandle so the Waiter Permission gates the operation.");

    [[nodiscard]] std::uint64_t current_value() const noexcept {
        return value_.load(std::memory_order_acquire);
    }

    void reset_under_quiescence(std::uint64_t value = 0) noexcept
        = delete("raw ChainEdge::reset_under_quiescence is substrate-only; use PermissionedChainEdge::reset_under_quiescence with the Whole Permission.");

private:
    template <VendorBackend, typename>
    friend class PermissionedChainEdge;

    void signal(detail::ChainEdgeAccess, const SemaphoreSignal& signal) noexcept {
        if (!matches_expected_signal_(signal)) return;
        mimic::detail::semaphore_signal<Backend>(semaphore_, signal.value);
    }

    // ── FIXY-FOUND-015: substrate `wait` is a POLL, NOT a block ────────
    //
    // The function name `wait` and `bool` return type would naturally
    // read as blocking semantics: "wait blocks until the signal arrives;
    // returns true on signal, false on timeout/error".  THAT IS NOT
    // WHAT THIS DOES TODAY.
    //
    // The implementation delegates to
    //   `mimic::detail::semaphore_wait<Backend>(semaphore_, value)`
    // which today routes — for EVERY backend (NV / AMD / TPU / TRN / CPU)
    // — through `mimic::detail::semaphore_poll_oracle` (one acquire
    // load + one compare; mimic/Semaphore.h:43-51).  The TRUE current
    // semantic is:
    //   true   ↔ current_value >= signal.value AT THE LOAD MOMENT
    //              (snapshot, may be stale by the time caller observes)
    //   false  ↔ current_value <  signal.value AT THE LOAD MOMENT, OR
    //              the signal doesn't match this ChainEdge's expected
    //              (matches_expected_signal_() short-circuit returned
    //               false; see early-return above)
    //
    // The `wait` SPELLING is preserved because real vendor backends
    // (fixy-V-205 future work) will land blocking implementations
    // behind the SAME function name and same `bool` return — but with
    // DIFFERENT post-conditions:
    //   true   ↔ signal arrived (may have blocked, semantics unchanged
    //              from caller's perspective if they did spin-then-
    //              try-again)
    //   false  ↔ timeout / cancellation / driver error (NOT "not ready
    //              yet" — that's the polling-era semantic that goes
    //              away once real vendor backends ship)
    //
    // The semantic SHIFT is silent at the type level — same function
    // signature, same return type, different post-condition contract.
    // Consumers MUST NOT rely on the polling-era "false means not-
    // ready-yet, retry me" behavior; the canonical retry pattern lives
    // in PermissionedChainEdge::WaiterHandle::try_wait (public-facing
    // surface CORRECTLY renamed to `try_wait`, documenting the polling
    // semantic AND the adaptive spin-then-yield retry shape per
    // FIXY-FOUND-111/119).  Session-level consumers route through
    // sessions/ChainEdgeSession.h::wait_transport which implements the
    // retry directly.
    //
    // Substrate-direct callers (= friend-access via detail::ChainEdge
    // Access, currently ONLY PermissionedChainEdge::wait_substrate_)
    // MUST treat the bool return as "polling oracle result, retry on
    // false" until the vendor backends migrate.  Tests calling this
    // path: same discipline.
    [[nodiscard]] bool wait(detail::ChainEdgeAccess,
                            const SemaphoreSignal& signal) const noexcept {
        if (!matches_expected_signal_(signal)) return false;
        return mimic::detail::semaphore_wait<Backend>(semaphore_, signal.value);
    }

    void reset_under_quiescence(detail::ChainEdgeAccess,
                                std::uint64_t value = 0) noexcept {
        value_.store(value, std::memory_order_release);
    }

    [[nodiscard]] constexpr bool
    matches_expected_signal_(const SemaphoreSignal& signal) const noexcept {
        return signal.edge == edge_
            && signal.upstream == upstream_
            && signal.downstream == downstream_
            && signal.value == signal_value_
            && signal.backend == Backend;
    }

    PlanId upstream_;
    PlanId downstream_;
    ChainEdgeId edge_;
    std::uint64_t signal_value_;
    mutable std::atomic<std::uint64_t> value_{0};
    mimic::DeviceSemaphore semaphore_;
};

}  // namespace crucible::concurrent
