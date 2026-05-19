// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-01 negative fixture #1 (HS14 ≥2 floor):
// mint_recording_session first-parameter SFINAE route via the
// `fixy::bridge::` re-export.
//
// `mint_recording_session(handle, log, self, peer)` is the §XXI
// canonical mint factory for wrapping an existing SessionHandle in a
// RecordingSessionHandle (which emits structured events to a
// SessionEventLog for replay / debugging / Cipher cold-tier
// persistence).  The substrate ships THREE overloads — bare
// SessionHandle, CrashWatchedHandle, and PermissionedSessionHandle
// — each with an explicit `requires IsSessionHandle<H>` clause per
// fixy-A2-026.  The first-parameter type is the §XXI authorization
// proof: pass anything that is NOT a session handle and substitution
// fails for ALL THREE overloads.
//
// This fixture exercises the §XXI mint factory call site through the
// `fixy::bridge::mint_recording_session` re-export (Bridge.h:117).
// Routing through the fixy:: layer (not directly through
// `safety::proto::`) witnesses that the using-decl preserves the
// requires-clause AND the parameter-type SFINAE.  A regression that
// silently re-introduced the bare ctor as a fallback (or that broke
// the using-decl import) would leave the substrate-side fixtures
// green while THIS fixture would unexpectedly compile — that gap is
// what the HS14 floor closes.
//
// Reject sequence: caller passes `int` as the first argument →
// each overload's `requires IsSessionHandle<H>` evaluates → all
// three constraint-fail (SessionHandle / CrashWatchedHandle / PSH
// are mutually exclusive; `int` matches none).  Overload resolution
// finds no viable candidate.
//
// Distinct from fixture #2 (wrong_log_type): #1 exercises the
// first-parameter (handle) gate; #2 exercises the second-parameter
// (log) gate.  Different parameter slots, different failure
// mechanisms (overload-set rejection vs reference-binding failure),
// different diagnostic shapes.
//
// Expected diagnostic: "no matching function for call to
// 'mint_recording_session'" / "constraints not satisfied" /
// "IsSessionHandle".

#include <crucible/fixy/Bridge.h>
#include <crucible/sessions/SessionEventLog.h>

namespace fbridge = ::crucible::fixy::bridge;
namespace proto   = ::crucible::safety::proto;

int main() {
    proto::SessionEventLog log{};
    int                    not_a_handle = 42;
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // First argument is `int` — fails IsSessionHandle<int> AND fails
    // every overload's parameter-type pattern-match.  fixy::bridge::
    // re-export must reject identically — the using-decl preserves
    // the substrate gate.
    [[maybe_unused]] auto bad =
        fbridge::mint_recording_session(not_a_handle, log, self, peer);

    return 0;
}
