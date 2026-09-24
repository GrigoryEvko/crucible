#pragma once

// A hardware ledger file.  The loader either refuses the text or returns a
// ledger that writes back to text which loads to a ledger that writes the
// same text: the text of an accepted ledger is a fixed point.

#include "../harness.h"

#include <crucible/ledger/LedgerStore.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_ledger() {
    return {text_bytes(::crucible::ledger::serialize_ledger(::crucible::ledger::Ledger{}))};
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
