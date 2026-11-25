// The compile-time checks of fixy/session/EventLog.h.

#include <fixy/session/EventLog.h>

#include <foundation/Lifetime.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Region.h>

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
static_assert(::foundation::core::CopiedElement<SessionEvent>,
              "the copy of SessionEvent must be trivial and hold no borrow, so a log gives an event by value with no "
              "lend.");

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

// A log gives an event by value and no iterator, and its frame door takes a
// body that keeps no borrow.  The appends of a writer do not throw.
template <class Log>
concept Iterates = requires(Log const& log) { log.begin(); };
static_assert(std::is_same_v<decltype(std::declval<SessionEventLog const&>()[std::size_t{0}]), SessionEvent>);
static_assert(std::is_same_v<decltype(std::declval<SessionEventLog const&>().back()), SessionEvent>);
static_assert(!Iterates<SessionEventLog>);
static_assert(noexcept(std::declval<SessionEventWriter&>().record_now(std::declval<SessionEvent>()))
              && noexcept(std::declval<SessionEventWriter&>().record(std::declval<SessionEvent>())));
static_assert(sizeof(SessionEventWriter) == sizeof(SessionEventLog*),
              "the allocation capability of a writer must take no byte");

struct CountsEvents final {
    [[nodiscard]] std::size_t operator()(Events events) const noexcept { return events.size(); }
};
struct GivesEvents final {
    [[nodiscard]] Events operator()(Events events) const noexcept { return events; }
};
struct KeepsEvents final {
    Events& kept;
    void operator()(Events events) const noexcept { kept = events; }
};
template <class Log, class Body>
concept Lends = requires(Log&& log, Body body) { static_cast<Log&&>(log).with_view(static_cast<Body&&>(body)); };
static_assert(Lends<SessionEventLog&, CountsEvents> && Lends<SessionEventLog const&, CountsEvents>);
static_assert(!Lends<SessionEventLog&, GivesEvents> && !Lends<SessionEventLog&, KeepsEvents>);
static_assert(!Lends<SessionEventLog, CountsEvents>);

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
