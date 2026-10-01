// The compile-time checks of fixy/session/EventLog.h.

#include <fixy/session/EventLog.h>

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
static_assert(!std::is_implicit_lifetime_v<SessionEvent>,
              "SessionEvent must not be an implicit-lifetime type, or std::start_lifetime_as builds an event over a "
              "buffer.");

}  // namespace fixy::session
