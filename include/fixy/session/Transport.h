#pragma once

// The transport of a session handle: the shapes of a transport callable,
// the value of a keyed message, and the word that a choice puts on a
// wire.
//
// fixy/session/Handle.h moves each message through a transport of one of
// these shapes.  The transport moves the bytes, and the handle decides if
// the step is legal at its protocol position.

#include <fixy/Core.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Registry.h>
#include <fixy/session/Watch.h>
#include <fixy/session/Wire.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::session {

// ── The shapes of a transport ────────────────────────────────────────
//
// A transport never waits where the watch of fixy/session/Watch.h cannot
// see the wait.  Each shape either returns at once, or takes the
// wait_scope that the handle opened before the call:
//
//   a trying write     bool(Resource&, T&)     false while it has no room,
//                                              and the value stays as it was
//   a declared write   void(Resource&, T&&, watch::wait_scope&)
//   a polling read     fixy::Option<T>(Resource&)    no value while nothing
//                                                    is there
//   a declared read    T(Resource&, watch::wait_scope&)
//
// The word of a choice or of a keyed message is std::size_t, with the same
// shapes.  When a trying write finds no room, or a polling read finds
// nothing, the handle waits through the watch and tries again.  A declared
// call waits inside the transport, and it calls poll() on the scope while
// it waits.  A transport of any other shape could wait where the watch does
// not see it, so each operation refuses it with [Transport_Shape].

template <typename F, typename Resource, typename T>
concept TryWrite =
    std::is_invocable_v<F&, Resource&, T&> && std::same_as<std::invoke_result_t<F&, Resource&, T&>, bool>;

template <typename F, typename Resource, typename T>
concept DeclaredWrite = !TryWrite<F, Resource, T> && std::is_invocable_v<F&, Resource&, T&&, watch::wait_scope&>
                     && std::is_void_v<std::invoke_result_t<F&, Resource&, T&&, watch::wait_scope&>>;

template <typename F, typename Resource, typename T>
concept WriteTransport = TryWrite<F, Resource, T> || DeclaredWrite<F, Resource, T>;

template <typename F, typename Resource, typename T>
concept PollRead =
    std::is_invocable_v<F&, Resource&> && std::same_as<std::invoke_result_t<F&, Resource&>, ::fixy::Option<T>>;

template <typename F, typename Resource, typename T>
concept DeclaredRead = !PollRead<F, Resource, T> && std::is_invocable_v<F&, Resource&, watch::wait_scope&>
                    && std::same_as<std::invoke_result_t<F&, Resource&, watch::wait_scope&>, T>;

template <typename F, typename Resource, typename T>
concept ReadTransport = PollRead<F, Resource, T> || DeclaredRead<F, Resource, T>;

namespace detail {

template <typename F, typename Resource, typename T>
consteval bool write_is_nothrow() noexcept {
    if constexpr (TryWrite<F, Resource, T>) {
        return std::is_nothrow_invocable_v<F&, Resource&, T&>;
    } else {
        return std::is_nothrow_invocable_v<F&, Resource&, T&&, watch::wait_scope&>;
    }
}

template <typename F, typename Resource, typename T>
consteval bool read_is_nothrow() noexcept {
    if constexpr (PollRead<F, Resource, T>) {
        return std::is_nothrow_invocable_v<F&, Resource&> && std::is_nothrow_move_constructible_v<T>;
    } else {
        return std::is_nothrow_invocable_v<F&, Resource&, watch::wait_scope&>;
    }
}

// Retries a polling read until it gives a value.  It runs only after the
// first read found nothing, so it is outlined and cold.  It waits through
// a watch::wait_scope, which refuses a wait that breaks the priority
// order and aborts on a cycle of waits across sessions.
template <typename Transport, typename Resource>
[[gnu::cold, gnu::noinline]] auto
wait_for_arrival(Transport& transport, Resource& resource,
                 watch::endpoint_id endpoint) noexcept(std::is_nothrow_invocable_v<Transport&, Resource&>)
    -> std::invoke_result_t<Transport&, Resource&> {
    watch::wait_scope wait{endpoint};
    for (;;) {
        auto arrived = std::invoke(transport, resource);
        if (arrived.is_some()) return arrived;
        wait.poll();
    }
}

// Retries a trying write until it takes the value, as wait_for_arrival
// retries a read.
template <typename Transport, typename Resource, typename T>
[[gnu::cold, gnu::noinline]] void
wait_for_room(Transport& transport, Resource& resource, T& value,
              watch::endpoint_id endpoint) noexcept(std::is_nothrow_invocable_v<Transport&, Resource&, T&>) {
    watch::wait_scope wait{endpoint};
    while (!std::invoke(transport, resource, value))
        wait.poll();
}

// Writes `value` through either write shape.  A trying write that finds
// room costs one call.  A declared write gets the scope opened for it.
template <typename T, typename Transport, typename Resource>
constexpr void write_through(Transport& transport, Resource& resource, T& value,
                             watch::endpoint_id endpoint) noexcept(write_is_nothrow<Transport, Resource, T>()) {
    if constexpr (TryWrite<Transport, Resource, T>) {
        if (!std::invoke(transport, resource, value)) [[unlikely]]
            wait_for_room(transport, resource, value, endpoint);
    } else {
        watch::wait_scope wait{endpoint};
        std::invoke(transport, resource, std::move(value), wait);
    }
}

// Reads a value through either read shape.  Each path of a polling read
// takes the value out of one Option, one time.
template <typename T, typename Transport, typename Resource>
[[nodiscard]] constexpr T
read_through(Transport& transport, Resource& resource,
             watch::endpoint_id endpoint) noexcept(read_is_nothrow<Transport, Resource, T>()) {
    if constexpr (PollRead<Transport, Resource, T>) {
        ::fixy::Option<T> arrived = std::invoke(transport, resource);
        if (arrived.is_none()) [[unlikely]]
            return wait_for_arrival(transport, resource, endpoint).expect("wait_for_arrival returns only a value");
        return std::move(arrived).expect("the first read gave a value");
    } else {
        watch::wait_scope wait{endpoint};
        return std::invoke(transport, resource, wait);
    }
}

// A read of the same shape as `read` that gives each value it reads to
// `seen` first.  A decorator uses it to learn what its inner handle read.
template <typename T, typename Resource, typename Read, typename Seen>
[[nodiscard]] constexpr auto observed_read(Read& read, Seen seen) noexcept {
    if constexpr (PollRead<Read, Resource, T>) {
        return [&read,
                seen](Resource& resource) mutable noexcept(read_is_nothrow<Read, Resource, T>()) -> ::fixy::Option<T> {
            ::fixy::Option<T> got = std::invoke(read, resource);
            for (T const& value : got)
                seen(value);
            return got;
        };
    } else {
        return [&read, seen](Resource& resource,
                             watch::wait_scope& wait) mutable noexcept(read_is_nothrow<Read, Resource, T>()) -> T {
            T got = std::invoke(read, resource, wait);
            seen(got);
            return got;
        };
    }
}

// A write of the same shape as `write` that calls `taken` once the
// transport took the value.
template <typename T, typename Resource, typename Write, typename Taken>
[[nodiscard]] constexpr auto observed_write(Write& write, Taken taken) noexcept {
    if constexpr (TryWrite<Write, Resource, T>) {
        return [&write, taken](Resource& resource,
                               T& value) mutable noexcept(write_is_nothrow<Write, Resource, T>()) -> bool {
            const bool is_taken = std::invoke(write, resource, value);
            if (is_taken) taken();
            return is_taken;
        };
    } else {
        return [&write, taken](Resource& resource, T&& value,
                               watch::wait_scope& wait) mutable noexcept(write_is_nothrow<Write, Resource, T>()) {
            std::invoke(write, resource, std::move(value), wait);
            taken();
        };
    }
}

}  // namespace detail

// ── The value of a keyed message ─────────────────────────────────────
//
// A keyed message, a PeerMsg or a Labelled, is its label word and then
// the value of its payload.  The label step sends or reads the word.  The
// handle then stands at the value step, a plain Send or Recv of the
// payload, which moves the value through a transport of that type.  A
// payload of void has no value.  The label word is then the whole
// message, and the handle stands past the label step.
//
// The label step and the value step are one message.  The crash transport
// of fixy/session/CrashTransport.h counts them as one message, and it
// refuses a crash between them.  The label step does not change the
// permission set.  The value step changes it, as each plain Send or Recv
// of the payload does.  The payload walk of fixy/session/Classification.h
// reads the payload of a keyed message, so the permission flow of the
// whole protocol changes the set at the same message.

// The two metafunctions live in detail, so no program specializes them:
// the handle, the crash transport and the checkpoint read the landing of
// a keyed step here, and a specialization would move one end past a value
// that the peer sends.
namespace detail {

// The payload of a keyed message.  Only PeerMsg and Labelled are keyed,
// because the payload rules of fixy/session/Protocol.h are sealed.  The
// primary has no definition, so a message of another shape stops the
// build.
template <typename Message>
struct keyed_value;
template <typename Peer, typename Label, typename Payload>
struct keyed_value<PeerMsg<Peer, Label, Payload>> {
    using type = Payload;
};
template <typename Label, typename Payload>
struct keyed_value<Labelled<Label, Payload>> {
    using type = Payload;
};

// The position after the label word of a keyed step: the value step, or
// the continuation when the payload is void.  The template parameters of
// the partial specializations have the names that the handle classes use,
// because GCC can print a Send or a Recv with the names of another
// specialization, and the fixtures match the printed names.
template <typename Step>
struct keyed_landing;
template <typename T, typename R>
struct keyed_landing<Send<T, R>> {
    using type =
        std::conditional_t<std::is_void_v<typename keyed_value<T>::type>, R, Send<typename keyed_value<T>::type, R>>;
};
template <typename T, typename R>
struct keyed_landing<Recv<T, R>> {
    using type =
        std::conditional_t<std::is_void_v<typename keyed_value<T>::type>, R, Recv<typename keyed_value<T>::type, R>>;
};

}  // namespace detail

template <typename Message>
using keyed_value_t = typename detail::keyed_value<Message>::type;

template <typename Step>
using keyed_landing_t = typename detail::keyed_landing<Step>::type;

// True when the message of a keyed step has a value.
template <typename Step>
concept keyed_step_has_value_v = !std::is_void_v<keyed_value_t<typename Step::message_type>>;

// ── Wire words ───────────────────────────────────────────────────────
//
// The Transport carries a word as std::size_t, which holds the 64 bits
// of a label word on each target this tree supports.  The check file of
// this header pins the width.

// True when each label branch of the Select or the Offer names a label
// key, so its wire words are label words.  It is a concept, so no program
// specializes it: branch_of_wire_word reads it to decode a received word.
template <typename Choice>
concept is_keyed_choice_v = ::fixy::session::transition::is_keyed_choice_type(detail::protocol_registry, ^^Choice);

namespace detail {

// The word of branch I.  A branch that is no label has no word, because
// the peer never picks it: the endpoint enters it on an event.
template <typename Choice, std::size_t I>
consteval std::uint64_t branch_wire_word_of() {
    constexpr ::fixy::session::transition::wire_word word =
        ::fixy::session::transition::wire_word_of(protocol_registry, ^^Choice, I);
    static_assert(word.is_wired, "fixy::session::diagnostic [Branch_Has_No_Wire_Word]: branch I of the choice is no "
                                 "label, so no endpoint picks it with a word.  The endpoint enters such a branch on "
                                 "an event, for example the detection of a crash.");
    return word.value;
}

// The word of each branch of the choice, and whether the branch has one.
template <typename Choice, std::size_t Count>
consteval std::array<::fixy::session::transition::wire_word, Count> wire_words_of() {
    std::array<::fixy::session::transition::wire_word, Count> words{};
    for (std::size_t index = 0; index < Count; ++index) {
        words[index] = ::fixy::session::transition::wire_word_of(protocol_registry, ^^Choice, index);
    }
    return words;
}

template <typename Choice>
inline constexpr auto wire_words_v = wire_words_of<Choice, Choice::branch_count>();

}  // namespace detail

// The word that select<I>() sends for branch I of Choice.  The handle reads
// the registry and not this spelling, so a specialization of it changes
// only what its author reads.
template <typename Choice, std::size_t I>
inline constexpr std::uint64_t branch_wire_word_v = detail::branch_wire_word_of<Choice, I>();

inline constexpr std::size_t no_branch = static_cast<std::size_t>(-1);

// The branch of Choice that a received word names, or no_branch.  A keyed
// choice compares the word with the label word of each label branch, so
// a word that no label of Choice has names no branch.  A positional
// choice reads the word as a position.  In either kind a branch that is
// no label has no word, so the wire never enters it: the crash transport
// of fixy/session/CrashTransport.h enters a crash branch with
// HandleFactory::recover.
// Complexity: linear in the branches, each step a compare with a
// constant.
template <typename Choice>
[[nodiscard]] constexpr std::size_t branch_of_wire_word(std::uint64_t word) noexcept {
    constexpr std::size_t count = Choice::branch_count;
    if constexpr (!is_keyed_choice_v<Choice>) {
        constexpr auto& words = detail::wire_words_v<Choice>;
        if (word >= count || !words[static_cast<std::size_t>(word)].is_wired) return no_branch;
        return static_cast<std::size_t>(word);
    } else {
        constexpr auto& words = detail::wire_words_v<Choice>;
        for (std::size_t index = 0; index < count; ++index) {
            if (words[index].is_wired && words[index].value == word) return index;
        }
        return no_branch;
    }
}

namespace detail {

template <typename Choice, std::size_t I>
consteval auto branch_landing_of() {
    using Branch = std::tuple_element_t<I, typename Choice::branches_tuple>;
    if constexpr (is_keyed_choice_v<Choice> && wire_words_v<Choice>[I].is_wired) {
        return std::type_identity<keyed_landing_t<Branch>>{};
    } else {
        return std::type_identity<Branch>{};
    }
}

}  // namespace detail

// The protocol at which a choice enters branch I.  A label branch of a
// keyed choice enters past its label word: at the value step, or past the
// message when its payload is void.  Every other branch enters at the
// branch.  A decorator that tracks the protocol of its inner handle reads
// it here.
template <typename Choice, std::size_t I>
using branch_landing_t = typename decltype(detail::branch_landing_of<Choice, I>())::type;

}  // namespace fixy::session
