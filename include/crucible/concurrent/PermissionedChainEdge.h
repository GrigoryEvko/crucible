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
            owner_.edge_.signal(signal);
        }

        [[nodiscard]] value_type signal() noexcept {
            value_type signal = expected_signal();
            owner_.edge_.signal(signal);
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

        [[nodiscard]] bool try_wait(const value_type& signal) const noexcept {
            return owner_.edge_.wait(signal);
        }

        [[nodiscard]] std::uint64_t current_value() const noexcept {
            return owner_.edge_.current_value();
        }
    };

    [[nodiscard]] SignalerHandle
    signaler(safety::Permission<signaler_tag>&& perm) noexcept {
        return SignalerHandle{*this, std::move(perm)};
    }

    [[nodiscard]] WaiterHandle
    waiter(safety::Permission<waiter_tag>&& perm) noexcept {
        return WaiterHandle{*this, std::move(perm)};
    }

    [[nodiscard]] edge_type& edge() noexcept {
        return edge_;
    }

    [[nodiscard]] const edge_type& edge() const noexcept {
        return edge_;
    }

private:
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

}  // namespace crucible::safety
