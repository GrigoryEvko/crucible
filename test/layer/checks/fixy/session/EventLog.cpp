// The compile-time checks of fixy/session/EventLog.h.

#include <fixy/session/EventLog.h>

#include <foundation/Lifetime.h>
#include <foundation/Transfer.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>
#include <foundation/reflect/Annotation.h>

#include <cstddef>

namespace fixy::session {

namespace detail::event_log {

static_assert(sizeof(RawEvent) == session_event_size);
static_assert(offsetof(RawEvent, pad) == 67, "the padding of the wire record starts at byte 67");

}  // namespace detail::event_log

static_assert(sizeof(SessionEvent) == session_event_size,
              "SessionEvent must be exactly 72 bytes, because durable storage and the decoder read that record size.");
static_assert(!std::is_trivially_copyable_v<SessionEvent>,
              "SessionEvent must not be trivially copyable, or std::bit_cast builds an event that no session step "
              "wrote.  encode() and decode_session_event are the byte routes.");
static_assert(std::is_trivially_copy_constructible_v<SessionEvent> && std::is_trivially_copy_assignable_v<SessionEvent>
                  && std::is_trivially_destructible_v<SessionEvent>,
              "the copies of SessionEvent must be trivial, so a copy is one copy of 72 bytes and runs no code.");
static_assert(!std::is_trivially_move_assignable_v<SessionEvent> && std::is_nothrow_move_assignable_v<SessionEvent>,
              "the move assignment of SessionEvent must stay user-provided.  A default of it makes the event trivially "
              "copyable, and std::bit_cast then builds an event.");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<SessionEvent>,
              "the checked lifetime start must refuse SessionEvent, or a start over a buffer builds an event that no "
              "session step wrote.");
static_assert(::foundation::core::HoldsNoBorrow<SessionEvent>,
              "an event must hold no borrow, so a copy of it outlives its log.");

namespace detail::event_log_checks {

using ::foundation::core::StoresNoBorrowOutside;
using Events = ::foundation::core::View<SessionEvent const>;

// The bodies of a frame door that reach a log.  The store check reads only the
// events of a log, so a log by reference and a writer by value are no borrow
// slots.  A View of events by value stays in the body.  A View by reference is
// a slot that the caller reads after the frame, and a writer by reference holds
// the address of its log, which the caller reads too.
struct HoldsLog final {
    SessionEventLog log;
};
struct ReachesLog final {
    SessionEventLog& log;
};
struct HoldsWriter final {
    SessionEventWriter writer;
};
struct ReachesWriter final {
    SessionEventWriter& writer;
};
struct HoldsEvents final {
    Events events;
};
struct ReachesEvents final {
    Events& events;
};
static_assert(StoresNoBorrowOutside<HoldsLog> && StoresNoBorrowOutside<HoldsLog&>);
static_assert(StoresNoBorrowOutside<ReachesLog> && StoresNoBorrowOutside<HoldsWriter>);
static_assert(StoresNoBorrowOutside<HoldsEvents> && !StoresNoBorrowOutside<ReachesEvents>);
static_assert(!StoresNoBorrowOutside<ReachesWriter>);

// A log gives a const event and a prefix of its events, and a temporary log
// gives none.  No verb of a log gives its events away.  The appends of a writer
// do not throw.
template <class Log>
concept Reads = requires(Log&& log) {
    { static_cast<Log&&>(log)[std::size_t{0}] } noexcept -> std::same_as<SessionEvent const&>;
    { static_cast<Log&&>(log).back() } noexcept -> std::same_as<SessionEvent const&>;
    {
        static_cast<Log&&>(log).events()
    } noexcept -> std::same_as<::foundation::core::SegmentPrefix<SessionEvent const>>;
};
template <class Log>
concept Drains = requires(Log&& log) { static_cast<Log&&>(log).drain(); };
static_assert(Reads<SessionEventLog&> && Reads<SessionEventLog const&>);
static_assert(!Reads<SessionEventLog> && !Reads<SessionEventLog const>);
static_assert(!Drains<SessionEventLog> && !Drains<SessionEventLog&>);
static_assert(noexcept(std::declval<SessionEventWriter&>().record_now(std::declval<SessionEvent>()))
              && noexcept(std::declval<SessionEventWriter&>().record(std::declval<SessionEvent>())));
static_assert(sizeof(SessionEventWriter) == sizeof(SessionEventLog*),
              "the allocation capability of a writer must take no byte");

// A log is the lines of its LiveList, and one line for the session, the step
// counter and the claim of the writer.
static_assert(sizeof(SessionEventLog) == 7 * 64 && alignof(SessionEventLog) == 64);

// A body beside its caller can hold a reference to a log and read it, while the
// caller appends to it through the writer.  Two threads can hold a log that is
// not const too: each append needs the writer, and the claim of the mint keeps
// one.  No body can hold the writer of a log.
static_assert(::foundation::transfer::Shareable<SessionEventLog>
              && ::foundation::transfer::Synchronized<SessionEventLog>);
static_assert(::foundation::transfer::Sendable<SessionEventLog const&>
              && ::foundation::transfer::SendableBeside<SessionEventLog const&>
              && ::foundation::transfer::SendableBeside<SessionEventLog&>);
static_assert(!::foundation::transfer::Sendable<SessionEventWriter>
              && !::foundation::transfer::Sendable<SessionEventWriter&>);

// A call of a function with no body in the unit ends no borrow of a log, which
// states that it keeps its storage and which no code can move or assign.  The
// rule view-escape reads the body, and nothing calls it.
static_assert(::foundation::reflect::carries_annotation(^^SessionEventLog, ^^::foundation::core::owns_its_storage));
static_assert(!std::is_move_constructible_v<SessionEventLog> && !std::is_move_assignable_v<SessionEventLog>);
void record_out_of_line(SessionEventLog& log) noexcept;
[[nodiscard]] inline std::size_t read_across_a_call(SessionEventLog& log) noexcept {
    SessionEventLog::events_type const events = log.events();
    auto const first = events.get(0).expect("the log holds an event");
    record_out_of_line(log);
    return events.size() + first->step_id().raw();
}

// Only the allocation capability mints a writer.
template <class Marker, class Log>
concept MintsWriter = requires(Marker marker, Log&& log) {
    { mint_session_event_writer(marker, static_cast<Log&&>(log)) } noexcept -> std::same_as<SessionEventWriter>;
};
struct OtherTag final {};
static_assert(MintsWriter<::foundation::core::Alloc, SessionEventLog&>);
static_assert(!MintsWriter<OtherTag, SessionEventLog&> && !MintsWriter<::foundation::core::Alloc, SessionEventLog>);
static_assert(!MintsWriter<::foundation::core::Alloc, SessionEventLog const&>);

}  // namespace detail::event_log_checks

// The two byte routes read and write the types of the base: the record is
// a FixedArray, and the decoder reads a View of bytes.
static_assert(std::is_same_v<decltype(std::declval<SessionEvent const&>().encode()),
                             ::foundation::core::FixedArray<std::byte, session_event_size>>);
static_assert(std::is_invocable_v<decltype(&decode_session_event), ::foundation::core::View<std::byte const>>);

}  // namespace fixy::session
