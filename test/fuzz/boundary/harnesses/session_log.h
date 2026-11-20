#pragma once

// A block of session-event records from the durable log.  The decoder
// either refuses the block or returns events that encode back to the exact
// bytes they came from: a record the decoder accepts carries no byte that
// its type does not account for.

#include "../harness.h"

#include <fixy/Core.h>
#include <fixy/session/EventLog.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline SeedList seeds_session_log() noexcept {
    using ::fixy::session::RoleTagId;
    using ::fixy::session::SessionEvent;
    SeedWriter log{};
    for (std::uint64_t role = 1; role <= 3; ++role) {
        const auto record = SessionEvent::close(RoleTagId{role}, RoleTagId{role + 1}).encode();
        log.append_bytes(::fixy::bytes(record));
    }
    SeedList seeds = empty_seed_list();
    add_seed(seeds, log.finish());
    return seeds;
}

inline void run_session_log(::fixy::View<std::byte const> block) noexcept {
    auto decoded = ::fixy::session::decode_session_log(test_alloc(), block);
    if (decoded.is_err()) {
        CRUCIBLE_FUZZ_CLAIM("session_log",
                            decoded.err().expect("the decode failed").index * ::fixy::session::session_event_size
                                <= block.size());
        return;
    }
    const ::fixy::HeapArray<::fixy::session::SessionEvent> events = std::move(decoded).expect("the block decoded");
    CRUCIBLE_FUZZ_CLAIM("session_log", events.size() * ::fixy::session::session_event_size == block.size());
    std::size_t position = 0;
    for (const ::fixy::session::SessionEvent& event : events.view()) {
        const auto encoded = event.encode();
        const ::fixy::View<std::byte const, ::fixy::session::session_event_size> record =
            block.window<::fixy::session::session_event_size>(position * ::fixy::session::session_event_size)
                .expect("each event has its record in the block");
        CRUCIBLE_FUZZ_CLAIM("session_log", ::fixy::equal(::fixy::bytes(encoded), record));
        ++position;
    }
}

}  // namespace crucible::fuzz::boundary
