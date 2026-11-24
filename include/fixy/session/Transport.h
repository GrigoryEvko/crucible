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
//   a trying write     fixy::Option<T>(Resource&, T&&)    of a linear T, on a
//                                              class with a call operator,
//                                              such as a closure: the value
//                                              back while it has no room, and
//                                              no value when it took the value
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
//
// A transport and a stop test hold no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h).  A step calls them again
// while it waits, and a call is no consume, so a linear value that one of
// them holds would end with no consume.

// A trying write of a copyable payload reads the value through a reference,
// and the caller keeps it while the write has no room.  A trying write of a
// linear payload takes the value by move and gives it back while it has no
// room, so each call consumes its value one time and no write moves a value
// out of a reference.  A linear payload with the reference shape is no trying
// write.  A plain send and a recorded send refuse it with [Transport_Shape],
// and a crash send refuses it with [Crash_Write_Must_Try].
template <typename F, typename Resource, typename T>
concept TryWriteByReference =
    std::is_invocable_v<F&, Resource&, T&> && std::same_as<std::invoke_result_t<F&, Resource&, T&>, bool>;

// The write of the move shape is a class with a call operator, such as a
// closure, and the doors call it as a plain call.  So the call names the body
// that takes the linear value.  A function and a pointer to a function are no
// such class: a pointer can be null, and a call through one names no body.
template <typename F, typename Resource, typename T>
concept TryWriteByMove = std::is_class_v<F> && requires(F& write, Resource& resource, T&& value) {
    { write(resource, static_cast<T&&>(value)) } -> std::same_as<::fixy::Option<T>>;
};

template <typename F, typename Resource, typename T>
concept TryWrite = (::foundation::linear::LinearValue<T> && TryWriteByMove<F, Resource, T>)
                || (!::foundation::linear::LinearValue<T> && TryWriteByReference<F, Resource, T>);

template <typename F, typename Resource, typename T>
concept DeclaredWrite = !TryWrite<F, Resource, T> && std::is_invocable_v<F&, Resource&, T&&, watch::wait_scope&>
                     && std::is_void_v<std::invoke_result_t<F&, Resource&, T&&, watch::wait_scope&>>;

// A write of one of the two shapes.  A door refuses a write of another shape
// with [Transport_Shape].
template <typename F, typename Resource, typename T>
concept WriteShape = TryWrite<F, Resource, T> || DeclaredWrite<F, Resource, T>;

template <typename F, typename Resource, typename T>
concept WriteTransport = WriteShape<F, Resource, T> && ::foundation::linear::HoldsNoLinearValue<F>;

// The reason of the deleted twin of each send of a linear payload, for a
// write that has no write shape of a linear payload.
#define CRUCIBLE_LINEAR_WRITE_REFUSAL                                                                                \
    "[Transport_Shape] a trying write of a linear payload is a closure, or a class with a call operator, of the "    \
    "shape fixy::Option<T>(Resource&, T&&): it takes the value, and it gives the value back while it has no room.  " \
    "A write of a linear payload can also declare its wait, as void(Resource&, T&&, "                                \
    "fixy::session::watch::wait_scope&).  A write through a reference leaves the handle a moved-out value, or a "    \
    "value that ends with no consume.  A function and a pointer to a function are no closure, because a pointer "    \
    "can be null."

// A stop test of a trying write: bool(), true when the write must stop
// and give the value back to the caller.  A send with a stop test calls
// the test and the write as plain calls, so each is a callable object.
template <typename F>
concept WriteStop = ::foundation::linear::HoldsNoLinearValue<F> && requires(F& stop) {
    { stop() } -> std::same_as<bool>;
};

template <typename F, typename Resource, typename T>
concept CallableTryWrite =
    TryWrite<F, Resource, T>
    && (requires(F& write, Resource& resource, T& value) {
           { write(resource, value) } -> std::same_as<bool>;
       } || requires(F& write, Resource& resource, T&& value) {
           { write(resource, static_cast<T&&>(value)) } -> std::same_as<::fixy::Option<T>>;
       });

template <typename F, typename Resource, typename T>
concept PollRead =
    std::is_invocable_v<F&, Resource&> && std::same_as<std::invoke_result_t<F&, Resource&>, ::fixy::Option<T>>;

template <typename F, typename Resource, typename T>
concept DeclaredRead = !PollRead<F, Resource, T> && std::is_invocable_v<F&, Resource&, watch::wait_scope&>
                    && std::same_as<std::invoke_result_t<F&, Resource&, watch::wait_scope&>, T>;

// A read of one of the two shapes.  A door refuses a read of another shape
// with [Transport_Shape].
template <typename F, typename Resource, typename T>
concept ReadShape = PollRead<F, Resource, T> || DeclaredRead<F, Resource, T>;

template <typename F, typename Resource, typename T>
concept ReadTransport = ReadShape<F, Resource, T> && ::foundation::linear::HoldsNoLinearValue<F>;

namespace detail {

template <typename F, typename Resource, typename T>
consteval bool write_is_nothrow() noexcept {
    if constexpr (TryWrite<F, Resource, T> && ::foundation::linear::LinearValue<T>) {
        return std::is_nothrow_invocable_v<F&, Resource&, T&&> && std::is_nothrow_move_constructible_v<T>;
    } else if constexpr (TryWrite<F, Resource, T>) {
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

// The same for a trying write of a linear payload: `back` holds the value
// that the write gave back, and each try moves it in again, until the write
// takes it.
template <typename Transport, typename Resource, typename T>
[[gnu::cold, gnu::noinline]] void
wait_for_room_moved(Transport& transport, Resource& resource, ::fixy::Option<T> back,
                    watch::endpoint_id endpoint) noexcept(std::is_nothrow_invocable_v<Transport&, Resource&, T&&>
                                                          && std::is_nothrow_move_constructible_v<T>) {
    // An Option of a linear value takes no assignment, so each try fills the
    // empty Option again with the value that the write gave back.
    watch::wait_scope wait{endpoint};
    for (;;) {
        wait.poll();
        ::fixy::Option<T> again = transport(resource, back.take().expect("the write gave the value back"));
        if (again.is_none()) return;
        back.fill(again.take().expect("the test above found the value"));
    }
}

// The stop test of a send that has none.  It never reports.
struct no_write_stop {
    [[nodiscard]] constexpr bool operator()() const noexcept { return false; }
};

// Retries a trying write until it takes the value or `stop` reports, as
// wait_for_room retries a write.  It tests `stop` before each try.  The
// result holds the value when `stop` reported, and no value when the
// transport took it.
template <typename Transport, typename Stop, typename Resource, typename T>
[[gnu::cold, gnu::noinline]] ::fixy::Option<T>
wait_for_room_unless(Transport& transport, Stop& stop, Resource& resource, T& value,
                     watch::endpoint_id endpoint) noexcept(std::is_nothrow_invocable_v<Transport&, Resource&, T&>
                                                           && std::is_nothrow_invocable_v<Stop&>
                                                           && std::is_nothrow_move_constructible_v<T>) {
    watch::wait_scope wait{endpoint};
    for (;;) {
        if (stop()) return ::fixy::Option<T>::some(std::move(value));
        if (transport(resource, value)) return ::fixy::none;
        wait.poll();
    }
}

// The same for a trying write of a linear payload: `back` holds the value
// that the write gave back, and the result holds it when `stop` reported.
template <typename Transport, typename Stop, typename Resource, typename T>
[[gnu::cold, gnu::noinline]] ::fixy::Option<T>
wait_for_room_unless_moved(Transport& transport, Stop& stop, Resource& resource, ::fixy::Option<T> back,
                           watch::endpoint_id endpoint) noexcept(std::is_nothrow_invocable_v<Transport&, Resource&, T&&>
                                                                 && std::is_nothrow_invocable_v<Stop&>
                                                                 && std::is_nothrow_move_constructible_v<T>) {
    watch::wait_scope wait{endpoint};
    for (;;) {
        if (stop()) return back;
        ::fixy::Option<T> again = transport(resource, back.take().expect("the write gave the value back"));
        if (again.is_none()) return again;
        back.fill(again.take().expect("the test above found the value"));
        wait.poll();
    }
}

// Writes `value` through a trying write, unless `stop` reports before a
// try.  The result holds the value when `stop` reported, and no value when
// the transport took it.  A write that finds room costs one test of `stop`
// and one call.  The caller gives the value by move, and a write of a
// copyable payload reads it through a reference.
template <typename T, typename Transport, typename Stop, typename Resource>
[[nodiscard]] constexpr ::fixy::Option<T>
write_unless(Transport& transport, Stop& stop, Resource& resource, T&& value,
             watch::endpoint_id endpoint) noexcept(write_is_nothrow<Transport, Resource, T>()
                                                   && std::is_nothrow_invocable_v<Stop&>
                                                   && std::is_nothrow_move_constructible_v<T>) {
    if (stop()) [[unlikely]]
        return ::fixy::Option<T>::some(std::move(value));
    if constexpr (::foundation::linear::LinearValue<T>) {
        ::fixy::Option<T> back = transport(resource, std::move(value));
        if (back.is_none()) [[likely]]
            return back;
        return ::fixy::session::detail::wait_for_room_unless_moved(transport, stop, resource, std::move(back),
                                                                   endpoint);
    } else {
        if (transport(resource, value)) [[likely]]
            return ::fixy::none;
        return wait_for_room_unless(transport, stop, resource, value, endpoint);
    }
}

// Writes `value` through either write shape.  A trying write that finds
// room costs one call.  A declared write gets the scope opened for it.  The
// caller gives the value by move, and a write of a copyable payload reads it
// through a reference.
template <typename T, typename Transport, typename Resource>
constexpr void write_through(Transport& transport, Resource& resource, T&& value,
                             watch::endpoint_id endpoint) noexcept(write_is_nothrow<Transport, Resource, T>()) {
    if constexpr (TryWrite<Transport, Resource, T> && ::foundation::linear::LinearValue<T>) {
        ::fixy::Option<T> back = transport(resource, std::move(value));
        // The call is qualified, so argument-dependent lookup finds no
        // overload of a different file, and a gate that reads this body
        // reads no open template.
        if (back.is_some()) [[unlikely]]
            ::fixy::session::detail::wait_for_room_moved(transport, resource, std::move(back), endpoint);
    } else if constexpr (TryWrite<Transport, Resource, T>) {
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
    if constexpr (TryWrite<Write, Resource, T> && ::foundation::linear::LinearValue<T>) {
        return [&write, taken](Resource& resource, T&& value) mutable noexcept(
                   write_is_nothrow<Write, Resource, T>()) -> ::fixy::Option<T> {
            ::fixy::Option<T> back = write(resource, std::move(value));
            if (back.is_none()) taken();
            return back;
        };
    } else if constexpr (TryWrite<Write, Resource, T>) {
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

// Has no constant definition.  A call to it stops the constant evaluation,
// and the diagnostic names it.
void a_keyed_step_carries_no_message() noexcept;

// True when the payload rule of the family of `message` has the role
// message (fixy/session/Registry.h).
[[nodiscard]] consteval bool is_keyed_message(std::meta::info message) {
    namespace tr = ::fixy::session::transition;
    const tr::payload_lookup rule = tr::lookup_payload_rule(protocol_registry, message);
    return rule.is_found && rule.entry.role == tr::payload_role::message;
}

// The payload of a keyed message: the value that its payload rule names.
[[nodiscard]] consteval std::meta::info keyed_value_of(std::meta::info message) {
    namespace tr = ::fixy::session::transition;
    const tr::payload_lookup rule = tr::lookup_payload_rule(protocol_registry, message);
    const tr::static_run<std::meta::info> argument = tr::template_arguments_run(std::meta::dealias(message));
    if (rule.entry.value_argument >= argument.size()) a_keyed_step_carries_no_message();
    return std::meta::dealias(argument[rule.entry.value_argument]);
}

// The payload of a keyed message, the value of its rule.  The primary has
// no definition, so a message of another family has no member type, and a
// reader that needs one stops the build.
template <typename Message, bool IsKeyed = is_keyed_message(^^Message)>
struct keyed_value;
template <typename Message>
struct keyed_value<Message, true> {
    using type = typename[:keyed_value_of(^^Message):];
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
