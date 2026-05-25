#pragma once

// PermissionedChainEdge — CSL role facade for ChainEdge.
//
// A ChainEdge has exactly one upstream signaler and one downstream
// waiter.  The primitive below encodes those roles as linear
// Permission tokens and makes role misuse structurally impossible:
// SignalerHandle exposes signal-only methods; WaiterHandle exposes
// wait-only methods.

#include <crucible/concurrent/ChainEdge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <type_traits>
#include <utility>

namespace crucible::concurrent {

namespace chainedge_tag {

template <typename UserTag> struct Whole    {};
template <typename UserTag> struct Signaler {};
template <typename UserTag> struct Waiter   {};

}  // namespace chainedge_tag

template <VendorBackend Backend = VendorBackend::CPU, typename UserTag = void>
class PermissionedChainEdge
    : public safety::Pinned<PermissionedChainEdge<Backend, UserTag>> {
public:
    using value_type   = SemaphoreSignal;
    using user_tag     = UserTag;
    using edge_type    = ChainEdge<Backend>;
    using whole_tag    = chainedge_tag::Whole<UserTag>;
    using signaler_tag = chainedge_tag::Signaler<UserTag>;
    using waiter_tag   = chainedge_tag::Waiter<UserTag>;

    static constexpr VendorBackend backend = Backend;

    PermissionedChainEdge(PlanId upstream,
                          PlanId downstream,
                          ChainEdgeId edge,
                          std::uint64_t signal_value = 1) noexcept
        : edge_{upstream, downstream, edge, signal_value}
    {}

    class SignalerHandle {
        PermissionedChainEdge& owner_;
        [[no_unique_address]] safety::Permission<signaler_tag> perm_;

        constexpr SignalerHandle(PermissionedChainEdge& owner,
                                 safety::Permission<signaler_tag>&& perm) noexcept
            : owner_{owner}, perm_{std::move(perm)} {}
        friend class PermissionedChainEdge;

    public:
        using value_type = SemaphoreSignal;
        using tag_type   = signaler_tag;

        SignalerHandle(const SignalerHandle&)
            = delete("ChainEdge SignalerHandle owns the Signaler Permission — copy would duplicate the linear token");
        SignalerHandle& operator=(const SignalerHandle&)
            = delete("ChainEdge SignalerHandle owns the Signaler Permission — assignment would overwrite the linear token");
        constexpr SignalerHandle(SignalerHandle&&) noexcept = default;
        SignalerHandle& operator=(SignalerHandle&&)
            = delete("ChainEdge SignalerHandle binds to one ChainEdge for life — rebinding would orphan the original Permission");

        [[nodiscard]] value_type expected_signal() const noexcept {
            return owner_.edge_.expected_signal();
        }

        void signal(const value_type& signal) noexcept {
            owner_.signal_substrate_(signal);
        }

        [[nodiscard]] value_type signal() noexcept {
            value_type signal = expected_signal();
            owner_.signal_substrate_(signal);
            return signal;
        }

        [[nodiscard]] std::uint64_t current_value() const noexcept {
            return owner_.edge_.current_value();
        }
    };

    class WaiterHandle {
        PermissionedChainEdge& owner_;
        [[no_unique_address]] safety::Permission<waiter_tag> perm_;

        constexpr WaiterHandle(PermissionedChainEdge& owner,
                               safety::Permission<waiter_tag>&& perm) noexcept
            : owner_{owner}, perm_{std::move(perm)} {}
        friend class PermissionedChainEdge;

    public:
        using value_type = SemaphoreSignal;
        using tag_type   = waiter_tag;

        WaiterHandle(const WaiterHandle&)
            = delete("ChainEdge WaiterHandle owns the Waiter Permission — copy would duplicate the linear token");
        WaiterHandle& operator=(const WaiterHandle&)
            = delete("ChainEdge WaiterHandle owns the Waiter Permission — assignment would overwrite the linear token");
        constexpr WaiterHandle(WaiterHandle&&) noexcept = default;
        WaiterHandle& operator=(WaiterHandle&&)
            = delete("ChainEdge WaiterHandle binds to one ChainEdge for life — rebinding would orphan the original Permission");

        [[nodiscard]] value_type expected_signal() const noexcept {
            return owner_.edge_.expected_signal();
        }

        // FIXY-FOUND-123: retry-contract documentation.
        //
        // try_wait observes the semaphore once and returns whether the
        // expected value has been reached.  Semantics:
        //   - returns true  iff current_value() >= signal.value AND
        //                       signal matches the edge's expected
        //                       (edge_id, upstream, downstream, value,
        //                        backend) tuple.
        //   - returns false  otherwise — no spin, no yield, no block.
        //
        // The CALLER owns the retry policy.  On `false` the caller
        // chooses to spin, yield, schedule other work, or compose with
        // a Session<> loop.  This is by design — the substrate cannot
        // pick a backoff strategy that fits every consumer (hot-fg
        // dispatch wants pause-only intra-core spin; bg compile pool
        // wants yield-aware; a Vigil deadline watchdog wants a hard
        // timeout).  The vendor-facing entry point
        // `mimic::<vendor>::semaphore_wait` keeps the "wait" spelling
        // because real backends will land blocking semantics behind it;
        // today every backend delegates to `semaphore_poll_oracle`
        // (one acquire load + compare, returns bool).
        //
        // RECOMMENDED retry pattern (mirrors FIXY-FOUND-111 / 119
        // adaptive backoff — pause-bounded spin then yield escalation
        // so a descheduled signaler doesn't burn the waiter's full
        // 10ms+ quantum):
        //
        //   constexpr std::size_t kPauseBeforeYield = 64;
        //   std::size_t spin_iters = 0;
        //   while (!waiter.try_wait(signal)) {
        //       if (spin_iters < kPauseBeforeYield) {
        //           CRUCIBLE_SPIN_PAUSE;
        //           ++spin_iters;
        //       } else {
        //           std::this_thread::yield();
        //       }
        //   }
        //
        // The canonical session-level pattern in
        // sessions/ChainEdgeSession.h::wait_transport implements this
        // shape directly — Session<> consumers inherit the correct
        // backoff by composition without copying the loop body.
        [[nodiscard]] bool try_wait(const value_type& signal) const noexcept {
            return owner_.wait_substrate_(signal);
        }

        [[nodiscard]] std::uint64_t current_value() const noexcept {
            return owner_.edge_.current_value();
        }
    };

    [[nodiscard]] constexpr SignalerHandle
    signaler(safety::Permission<signaler_tag>&& perm) noexcept {
        return SignalerHandle{*this, std::move(perm)};
    }

    [[nodiscard]] constexpr WaiterHandle
    waiter(safety::Permission<waiter_tag>&& perm) noexcept {
        return WaiterHandle{*this, std::move(perm)};
    }

    [[nodiscard]] safety::Permission<whole_tag>
    reset_under_quiescence(safety::Permission<whole_tag>&& perm,
                           std::uint64_t value = 0) noexcept {
        reset_substrate_(value);
        return std::move(perm);
    }

    [[nodiscard]] edge_type& edge() noexcept {
        return edge_;
    }

    [[nodiscard]] const edge_type& edge() const noexcept {
        return edge_;
    }

private:
    void signal_substrate_(const value_type& signal) noexcept {
        edge_.signal(detail::ChainEdgeAccess{}, signal);
    }

    [[nodiscard]] bool wait_substrate_(const value_type& signal) const noexcept {
        return edge_.wait(detail::ChainEdgeAccess{}, signal);
    }

    void reset_substrate_(std::uint64_t value = 0) noexcept {
        edge_.reset_under_quiescence(detail::ChainEdgeAccess{}, value);
    }

    edge_type edge_;
};

}  // namespace crucible::concurrent

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::chainedge_tag::Whole<UserTag>,
                   concurrent::chainedge_tag::Signaler<UserTag>,
                   concurrent::chainedge_tag::Waiter<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::chainedge_tag::Whole<UserTag>,
                        concurrent::chainedge_tag::Signaler<UserTag>,
                        concurrent::chainedge_tag::Waiter<UserTag>>
    : std::true_type {};

// fixy-M-29 authoring witnesses.
template <typename UserTag>
struct splits_into_authoring_witness<
    concurrent::chainedge_tag::Whole<UserTag>,
    concurrent::chainedge_tag::Signaler<UserTag>,
    concurrent::chainedge_tag::Waiter<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack_authoring_witness<
    concurrent::chainedge_tag::Whole<UserTag>,
    concurrent::chainedge_tag::Signaler<UserTag>,
    concurrent::chainedge_tag::Waiter<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
