#pragma once

// A hardware ledger file.  The loader either refuses the text or returns a
// ledger that writes back to text which loads to a ledger that writes the
// same text: the text of an accepted ledger is a fixed point.

#include "../harness.h"

#include <crucible/ledger/LedgerStore.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_ledger() {
    // Regression: perf_event_paranoid is -1 on a host that allows every
    // event.  The reader parsed the field as unsigned, read -1 as 0, and
    // the text was no longer a fixed point.
    ::crucible::ledger::Ledger paranoid_minus_one{};
    paranoid_minus_one.competence.perf_event_paranoid = -1;
    return {
        text_bytes(::crucible::ledger::serialize_ledger(::crucible::ledger::Ledger{})),
        text_bytes(::crucible::ledger::serialize_ledger(paranoid_minus_one)),
        // Regression: a competence field that holds more than 32 bits.  The
        // reader truncated it to a negative int, which it then wrote back
        // and read as 0.
        text_bytes("crucible-hwledger\t1\nfingerprint\t0\t0\n"
                   "competence\t0\t0\t0\t0\t0\t1000000050000\t0\t0\t0\tnone\n"),
    };
}

inline void run_ledger(std::span<const std::uint8_t> bytes) {
    auto ledger = ::crucible::ledger::deserialize_ledger(as_text(bytes));
    if (!ledger) return;
    const std::string text = ::crucible::ledger::serialize_ledger(*ledger);
    auto again = ::crucible::ledger::deserialize_ledger(text);
    CRUCIBLE_FUZZ_CLAIM("ledger", again.has_value());
    CRUCIBLE_FUZZ_CLAIM("ledger", ::crucible::ledger::serialize_ledger(*again) == text);
}

}  // namespace crucible::fuzz::boundary
