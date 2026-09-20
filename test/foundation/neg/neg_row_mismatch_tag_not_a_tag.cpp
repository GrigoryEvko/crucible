// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A type that is not a diagnostic tag in the tag slot of
// row_mismatch_message_v.  The routed static_assert in
// detail::row_message_check<..., false> fires with the header's own
// wording rather than the compiler's, so the message a reader sees is
// the one this tree controls.

#include <foundation/diag/RowMismatch.h>

inline void some_function() noexcept {}

// int stands where a tag class belongs.
constexpr auto& bogus_msg = ::foundation::diag::row_mismatch_message_v<int, &some_function, int, float, double>;

// Taking the address is what instantiates the variable.
auto const* g_addr = &bogus_msg;

int main() { return 0; }
