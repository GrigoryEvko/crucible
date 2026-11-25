// The compile-time checks of fixy/session/EventLog.h.

#include <fixy/session/EventLog.h>

#include <foundation/Lifetime.h>
#include <foundation/core/Grow.h>

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

// The two byte routes read and write the types of the base: the record is
// a FixedArray, and the decoder reads a View of bytes.
static_assert(std::is_same_v<decltype(std::declval<SessionEvent const&>().encode()),
                             ::foundation::core::FixedArray<std::byte, session_event_size>>);
static_assert(std::is_invocable_v<decltype(&decode_session_event), ::foundation::core::View<std::byte const>>);

}  // namespace fixy::session
