// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violations: out-of-range branch indices for both Select::select<I>
// (#432) and Offer::pick<I> (#433).  Per the framework's named-
// diagnostic discipline (#371), the static_assert in each method's
// body fires the `[Branch_Index_Out_Of_Range]` prefix instead of
// GCC's generic "constraints not satisfied" message.

#include <crucible/sessions/Session.h>

using namespace crucible::safety::proto;

struct R { int sentinel = 1; };

void compile_time_reject_select() {
    auto h = make_session_handle<Select<Send<int, End>>>(R{});
    // Only branch 0 exists.  select_local<10> is out of range.
    // (Per #377 the bare `select<10>()` is now a [Wire_Variant_Required]
    // delete; the local variant is the right call to test the index
    // diagnostic in isolation.)
    auto next = std::move(h).select_local<10>();
    (void)next;
}

void compile_time_reject_offer() {
    auto h = make_session_handle<Offer<Recv<int, End>>>(R{});
    // Only branch 0 exists.  pick_local<5> on the Offer side is out of range.
    auto next = std::move(h).pick_local<5>();
    (void)next;
}

int main() { return 0; }
