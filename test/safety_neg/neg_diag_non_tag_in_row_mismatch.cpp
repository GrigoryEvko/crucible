// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a non-tag type as the first template parameter
// of `row_mismatch_message_v` / `CRUCIBLE_ROW_MISMATCH_ASSERT`.
// The routed `static_assert` in `detail::row_message_check<..., false>`
// must fire with the framework-controlled
// `[RowMismatchTag_NonTag]` diagnostic.

#include <crucible/safety/diag/RowMismatch.h>

inline void some_function() noexcept {}

// Intent: someone passes `int` as the diagnostic tag — a typical
// copy-paste error if the user grabs an arbitrary type from the
// surrounding context.  The framework rejects with the routed message.
constexpr auto& bogus_msg = ::crucible::safety::diag::row_mismatch_message_v<
    int,                          // NOT a tag — must be rejected
    &some_function,
    int,
    float,
    double>;

// Force ODR-use to trigger instantiation.
auto const* g_addr = &bogus_msg;

int main() { return 0; }
