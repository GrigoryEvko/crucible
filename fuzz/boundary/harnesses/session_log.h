#pragma once

// A block of session-event records from the durable log.  The decoder
// either refuses the block or returns events that encode back to the exact
// bytes they came from: a record the decoder accepts carries no byte that
// its type does not account for.

#include "../harness.h"

#include <fixy/session/EventLog.h>

#include <algorithm>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_session_log() {
    using ::fixy::session::RoleTagId;
    using ::fixy::session::SessionEvent;
    std::vector<std::uint8_t> log;
    for (std::uint64_t role = 1; role <= 3; ++role) {
        const auto record = SessionEvent::close(RoleTagId{role}, RoleTagId{role + 1}).encode();
        for (const std::byte b : record) log.push_back(static_cast<std::uint8_t>(b));
    }
    return {log};
}

inline void run_session_log(std::span<const std::uint8_t> bytes) {
    const auto block = as_bytes_view(bytes);
    auto events = ::fixy::session::decode_session_log(block);
    if (!events) {
        CRUCIBLE_FUZZ_CLAIM("session_log", events.error().index * ::fixy::session::session_event_size <= bytes.size());
        return;
    }
    CRUCIBLE_FUZZ_CLAIM("session_log", events->size() * ::fixy::session::session_event_size == bytes.size());
    for (std::size_t i = 0; i < events->size(); ++i) {
        const auto encoded = (*events)[i].encode();
        const auto record = block.subspan(i * ::fixy::session::session_event_size, ::fixy::session::session_event_size);
        CRUCIBLE_FUZZ_CLAIM("session_log", std::ranges::equal(encoded, record));
    }
}

}  // namespace crucible::fuzz::boundary
