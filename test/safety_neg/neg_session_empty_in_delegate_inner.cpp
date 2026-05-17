// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-14: covers the Delegate<T, K> empty-choice spec in
// SessionDelegate.h.  The DELEGATED INNER protocol T (= Select<>)
// is empty-choice-bearing — if mint accepts this outer handle,
// the user calls .delegate(inner_handle) at runtime; inner_handle
// must satisfy SessionHandle<T> which is unrunnable.  Rejecting
// at the outer mint catches the bug at construction time rather
// than at the eventual .delegate() call (which would either fail
// to construct an inner_handle to pass, or worse, accept a
// shadow handle whose protocol is structurally dead).
//
// Distinct from neg_session_empty_select_nested_loop.cpp
// (Session.h Loop+Recv spec) and neg_session_empty_offer_nested_send.cpp
// (Session.h Send spec) — this fixture exercises the
// SessionDelegate.h `is_empty_choice<Delegate<T, K>>` spec's
// LEFT (T) arm of the OR-fold.
//
// Expected diagnostic: [Empty_Choice_Combinator].

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionDelegate.h>

using namespace crucible::safety::proto;

struct R { int sentinel = 3; };

void compile_time_reject() {
    using EmptyDelegated = Delegate<Select<>, End>;
    auto h = mint_session_handle<EmptyDelegated>(R{});
    (void)h;
}

int main() { return 0; }
