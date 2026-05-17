// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-14: covers the Accept<T, K> empty-choice spec in
// SessionDelegate.h, RIGHT (K) arm of the OR-fold.  K = Select<>
// is empty-choice-bearing; the continuation after .accept() is
// unrunnable.  Symmetric to the Delegate fixture's T-arm: same
// trait shape, different mint position, different OR arm.
//
// Together with neg_session_empty_in_delegate_inner.cpp this
// closes the test coverage for the SessionDelegate.h
// is_empty_choice specs added in fixy-CR-14 (Delegate / Accept /
// EpochedDelegate / EpochedAccept).
//
// Expected diagnostic: [Empty_Choice_Combinator].

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionDelegate.h>

using namespace crucible::safety::proto;

struct R { int sentinel = 4; };

void compile_time_reject() {
    using EmptyContinuation = Accept<End, Select<>>;
    auto h = mint_session_handle<EmptyContinuation>(R{});
    (void)h;
}

int main() { return 0; }
