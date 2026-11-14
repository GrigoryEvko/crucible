#pragma once

// A block of session-event records from the durable log.  The decoder
// either refuses the block or returns events that encode back to the exact
// bytes they came from: a record the decoder accepts carries no byte that
// its type does not account for.

#include "../harness.h"

#include <fixy/Core.h>
#include <fixy/session/EventLog.h>
#include <foundation/Quarantine.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_session_log() {
    using ::fixy::session::RoleTagId;
    using ::fixy::session::SessionEvent;
    std::vector<std::uint8_t> log;
    for (std::uint64_t role = 1; role <= 3; ++role) {
        const auto record = SessionEvent::close(RoleTagId{role}, RoleTagId{role + 1}).encode();
        for (const std::byte b : record)
            log.push_back(static_cast<std::uint8_t>(b));
    }
    return {log};
}

inline void run_session_log(std::span<const std::uint8_t> bytes) {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fuzzer gives its input as a pointer to raw bytes and a count")
    const std::span<const std::byte> raw = as_bytes_view(bytes);
    const ::fixy::View<std::byte const> block = ::fixy::mint_foreign_view(raw.data(), raw.size());
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    auto events = ::fixy::session::decode_session_log(block);
    if (!events) {
        CRUCIBLE_FUZZ_CLAIM("session_log", events.error().index * ::fixy::session::session_event_size <= bytes.size());
        return;
    }
    CRUCIBLE_FUZZ_CLAIM("session_log", events->size() * ::fixy::session::session_event_size == bytes.size());
    for (std::size_t i = 0; i < events->size(); ++i) {
        const auto encoded = (*events)[i].encode();
        const ::fixy::View<std::byte const, ::fixy::session::session_event_size> record =
            block.window<::fixy::session::session_event_size>(i * ::fixy::session::session_event_size)
                .expect("each event has its record in the block");
        CRUCIBLE_FUZZ_CLAIM("session_log", ::fixy::equal(::fixy::bytes(encoded), record));
    }
}

}  // namespace crucible::fuzz::boundary
