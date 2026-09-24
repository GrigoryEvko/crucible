#pragma once

// The binding of a channel handle to its channel: a pointer that a move
// clears.
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
// comparison on the pointer that the operation loads anyway.  In a
// translation unit built with the ignore semantic the check compiles to
// nothing, and a use of the empty binding then reads through a null
// pointer.  That read traps, because the tree builds with
// -fno-delete-null-pointer-checks, so the use still fails closed.
//
// A handle that declares its own move operations defaulted gets this
// behaviour with no further code.  A handle that turns into a handle of
// another state, such as a close() that consumes the open handle, moves
// the binding into the new handle so that the consumed one is left empty.
//
// The binding lives in foundation because the channels of both trees use
// it, and the old tree cannot include a header that opens namespace fixy:
// its tests alias that name to the old fixy namespace.

#include <foundation/Platform.h>

#include <type_traits>
#include <utility>

namespace foundation {

template <typename Channel>
class ChannelBinding {
public:
    using channel_type = Channel;

    explicit constexpr ChannelBinding(Channel& channel) noexcept : channel_{&channel} {}

    ChannelBinding(const ChannelBinding&) =
        delete("a copy would bind a second handle to the channel of the first one");
    ChannelBinding& operator=(const ChannelBinding&) =
        delete("a copy would bind a second handle to the channel of the first one");

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
            CRUCIBLE_ASSERT(channel_ != nullptr);
        }
        return channel_;
    }

    [[nodiscard, gnu::always_inline]] constexpr Channel& operator*() const noexcept {
        if !consteval {
            CRUCIBLE_ASSERT(channel_ != nullptr);
        }
        return *channel_;
    }

    // For a consuming member that ends the handle without moving it into
    // another handle, such as one that hands its Permission back.
    constexpr void unbind() noexcept { channel_ = nullptr; }

    // False only for a handle that was moved from or consumed.
    [[nodiscard]] constexpr bool is_bound() const noexcept { return channel_ != nullptr; }

private:
    Channel* channel_;
};

// The binding must cost what a reference costs, or every handle grows.
static_assert(sizeof(ChannelBinding<int>) == sizeof(int*));
static_assert(alignof(ChannelBinding<int>) == alignof(int*));
static_assert(!std::is_copy_constructible_v<ChannelBinding<int>>);
static_assert(!std::is_copy_assignable_v<ChannelBinding<int>>);
static_assert(std::is_nothrow_move_constructible_v<ChannelBinding<int>>);
static_assert(!std::is_default_constructible_v<ChannelBinding<int>>);

}  // namespace foundation
