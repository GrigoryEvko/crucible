#pragma once

// How a crashed peer stopped.
//
// The calculus of fixy/session/Crash.h has no crash classes.  Crucible
// records how a peer stopped, because replay must tell an abort from an
// error return.  That record is CrashCause, and it is metadata on the
// crash branch: the value the survivor receives through
// Recv<Crash<Peer>, K> carries it.  It is not in any type, it orders
// nothing, and no rule reads it.  It is not a lattice and not a fixy atom.
//
// The session event log (fixy/session/EventLog.h) stores the cause in its
// crash lane, and it needs only this enum.  So the enum has a header of
// its own, and the event log does not include the protocol machinery of
// Crash.h.

#include <cstdint>

namespace fixy::session {

// How the stopped peer ended, as the detector saw it.  The byte values
// are the values of the crash lane in the session event log
// (fixy/session/EventLog.h), and they are fixed, so a stored log decodes
// unchanged.  A stored log can hold 3 for a crash graded "no throw", which
// is a contradiction.  It decodes as Unknown.
enum class CrashCause : std::uint8_t {
    Abort = 0,
    Throw = 1,
    ErrorReturn = 2,
    Unknown = 3,
};

inline constexpr std::uint8_t crash_cause_top_value = static_cast<std::uint8_t>(CrashCause::Unknown);

}  // namespace fixy::session
