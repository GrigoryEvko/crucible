#pragma once

// The binding of a channel handle to its channel: a pointer that a move
// clears.  ChannelIdentity names the instance of a binding by equality
// only.  EndpointClaim, at the foot of this file, keeps one live handle of
// each linear role of a channel.
//
// A handle that holds its channel by reference or by plain pointer keeps
// that binding when it is moved from, because a defaulted move copies a
// pointer and cannot reseat a reference.  The moved-from handle then goes
// on pushing and popping, although its Permission or its pool share now
// belongs to the handle it was moved into.  Two producers run where the
// type counted one.  On a pool-backed channel the moved-from producer
// also pushes during the exclusive window, which assumes that every
// producer is out.
//
// A C++ move is an affine use of the source: the source can still be
// dropped, and it must not be used again.  The language checks the first
// half and not the second.  This binding makes the second half hold at
// run time.  The move hands the pointer on and leaves the source empty,
// and every use checks that the binding is still there.  The check is one
// comparison on the pointer that the operation loads anyway, and a failed
// check ends the process through a cold path that does not return.
//
// The check is a fatal invariant and not a contract, because it must hold
// under every contract semantic.  Under ignore a contract check is absent.
// Under observe and enforce GCC does not treat the violation as the end of
// the path, and at -O3 it then sees the use go on through the null pointer.
// A use through a null pointer is undefined behaviour, and no build flag
// makes it a reliable trap: it reads or writes the channel member at its
// offset from address zero, and the member of a large channel lies far
// past the unmapped first page.  The failed check calls a function that
// does not return, and no path reaches the use.
//
// The check stays at run time because the compile-time rules do not refuse
// each route to a moved-from handle.  The rule use-after-move refuses a use
// of a moved local and of a plain reference to it.  It does not see these
// shapes, and each one compiles:
//
//   - a move through a callee that takes the handle by lvalue reference
//   - a use through a pointer to the handle
//   - a use through a reference that a call gives back, such as the Ref of
//     foundation::core::to(handle), taken before the move
//   - a try_ call that moves the handle also when it does not succeed
//
// The check can go when a place-based borrow checker refuses a borrow of a
// handle that lives across a move of that handle, and the move of a binding
// holder through a borrow is an error.  Until then no rule stands for it, so
// the check runs in each build, Release too.
//
// A handle that declares its own move operations defaulted gets this
// behaviour with no further code.  A handle that turns into a handle of
// another state, such as a close() that consumes the open handle, moves
// the binding into the new handle so that the consumed one is left empty.
//
// The permissioned channels of fixy/concurrent and crucible/
// PermissionedMetaLog.h hold their channel through this binding.

#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Report.h>

#include <type_traits>
#include <utility>

namespace foundation {

// The identity of the channel instance that a binding names.  Two
// identities compare equal when they name one instance, and an identity
// gives no access to the channel.  A pipeline compares the identity of the
// producer of one stage with the identity of the consumer of the next.
// An identity holds the address of its channel only to compare it, and no
// code reads the channel through it, so it moves between threads and two
// threads read it at once (foundation/Transfer.h).
template <typename Channel>
class[[= ::foundation::transfer::sendable{}]][[= ::foundation::transfer::shareable{}]] ChannelIdentity {
public:
    constexpr ChannelIdentity() noexcept = default;
    explicit constexpr ChannelIdentity(Channel const* instance) noexcept : instance_{instance} {}

    // False for the identity of a handle that was moved from or consumed.
    [[nodiscard]] constexpr bool is_bound() const noexcept { return instance_ != nullptr; }

    [[nodiscard]] friend constexpr bool operator==(ChannelIdentity, ChannelIdentity) noexcept = default;

private:
    Channel const* instance_ = nullptr;
};

// The key of a binding to a channel of the type Channel.  Only Channel and
// its nested handles build one, for the handles that the channel makes, so
// no other code binds a handle to an object that is no channel.  The seal
// closes the byte routes to a key.
template <typename Channel>
class BindingKey final {
    friend Channel;
    constexpr BindingKey() noexcept = default;

    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// A binding points at a channel that its handles on several threads use at
// once by design.  So it moves between threads as a reference to its
// channel does: only when two threads can write the channel
// (foundation/Transfer.h).  Each channel states that with synchronized{}
// and its reason.  A binding owns nothing, so a value that holds one is not
// SendableOwned, and no channel takes a handle as its value.
//
// The constructor takes the key of the channel, BindingKey<Channel>, which
// only the channel builds.
template <typename Channel>
class[[= ::foundation::transfer::refers{^^Channel}]] ChannelBinding {
public:
    using channel_type = Channel;

    explicit constexpr ChannelBinding(Channel& channel, BindingKey<Channel> /*key*/) noexcept : channel_{&channel} {}

    ChannelBinding(const ChannelBinding&) = delete("a copy would bind a second handle to the channel of the first one");
    ChannelBinding&
    operator=(const ChannelBinding&) = delete("a copy would bind a second handle to the channel of the first one");

    constexpr ChannelBinding(ChannelBinding&& other) noexcept : channel_{std::exchange(other.channel_, nullptr)} {}

    // Kept for the handles that allow move assignment.  A handle that
    // binds to one channel for life deletes its own.
    constexpr ChannelBinding& operator=(ChannelBinding&& other) noexcept {
        channel_ = std::exchange(other.channel_, nullptr);
        return *this;
    }

    ~ChannelBinding() = default;

    // The check sits in the run-time branch only.  A constant evaluation
    // cannot read through a null pointer anyway, and GCC refuses a contract
    // whose condition reads a run-time object while it tries to fold a
    // const initializer, which every inline function here is exposed to.
    [[nodiscard, gnu::always_inline]] constexpr Channel* operator->() const noexcept {
        if !consteval {
            CRUCIBLE_FATAL_INVARIANT(channel_ != nullptr);
        }
        return channel_;
    }

    [[nodiscard, gnu::always_inline]] constexpr Channel& operator*() const noexcept {
        if !consteval {
            CRUCIBLE_FATAL_INVARIANT(channel_ != nullptr);
        }
        return *channel_;
    }

    // For a consuming member that ends the handle without moving it into
    // another handle, such as one that hands its Permission back.  A handle
    // that holds an EndpointClaim gives the claim back before it unbinds.
    constexpr void unbind() noexcept { channel_ = nullptr; }

    // False only for a handle that was moved from or consumed.
    [[nodiscard]] constexpr bool is_bound() const noexcept { return channel_ != nullptr; }

    // The identity of the channel instance.  The identity of a handle that
    // was moved from or consumed is not bound.
    [[nodiscard]] constexpr ChannelIdentity<Channel> identity() const noexcept {
        return ChannelIdentity<Channel>{channel_};
    }

private:
    Channel* channel_;
};

// One linear role of a channel, which at most one live handle holds.
//
// A permission brand tells apart two channels when their roots come from
// two call sites.  Nothing in a type tells apart two roots of one call site: a
// site that runs two times mints two tokens of one brand, and each token
// opens the role.  This is fact 2 of foundation/Brand.h: the brand of a mint
// that gives a value out names its call site, and a run-time id tells apart
// two runs of the site.  The claim is that run-time id here.  The channel takes
// the claim before it builds a handle, and the handle releases the claim
// when it ends.  So a second live handle of one role ends the process, and
// a handle made after the first one ended takes the role again.
//
// The claim costs one exchange when the channel builds a handle and one
// store when the handle ends, and nothing on a push or a pop.  The exchange acquires
// what the last holder released, so a new holder sees each write of the
// last one.
//
// Two threads take and release one claim at the same time through its one
// member, an Atomic of the base.  So the class is Synchronized by its
// structure, and it states no transfer fact (the shared write in
// foundation/Transfer.h).
class EndpointClaim {
public:
    constexpr EndpointClaim() noexcept = default;

    EndpointClaim(const EndpointClaim&) = delete("a claim belongs to one channel, and the channel does not move");
    EndpointClaim&
    operator=(const EndpointClaim&) = delete("a claim belongs to one channel, and the channel does not move");
    EndpointClaim(EndpointClaim&&) = delete("a claim belongs to one channel, and the channel does not move");
    EndpointClaim& operator=(EndpointClaim&&) = delete("a claim belongs to one channel, and the channel does not move");
    ~EndpointClaim() = default;

    // Takes the role for a new handle, or ends the process when a live
    // handle holds it.  The detail names the role in the report.
    // The check stands for fact 2 of foundation/Brand.h, because no type tells two runs of one site apart.
    void take(::foundation::core::TextView detail) noexcept {
        if (held_.exchange_acq_rel(true)) [[unlikely]] {
            second_holder_abort_(detail);
        }
    }

    // The handle that holds the role calls this when it ends.
    void release() noexcept { held_.store_release(false); }

    [[nodiscard]] bool is_held() const noexcept { return held_.load_acquire(); }

private:
    // The text names the category LinearityViolation of the diagnostic
    // catalog, which gives the remediation.
    [[noreturn]] CRUCIBLE_COLD static void second_holder_abort_(::foundation::core::TextView detail) noexcept {
        ::foundation::core::fatal("LinearityViolation: a second live handle took one linear role of a channel: {}",
                                  detail);
    }

    ::foundation::core::Atomic<bool> held_{false};
};

}  // namespace foundation
