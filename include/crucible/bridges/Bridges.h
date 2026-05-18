#pragma once

// ── crucible::bridges umbrella ──────────────────────────────────────
//
// Cross-substrate composition primitives:
//   MachineSessionBridge    — Machine<State> ↔ Session<Proto, R>
//   RecordingSessionHandle  — AppendOnly event log on a session handle
//   SessionPersistence      — RecordingSessionHandle → Cipher event store
//   CrashTransport          — OneShotFlag × runtime crash transport
//
// Bridges depend on multiple substrates (safety/ + sessions/ +
// permissions/ + handles/) — separating them into their own
// directory makes the cross-substrate dependency explicit.

// fixy-A2-014: SessionPersistence.h no longer transitively pulls Cipher.h;
// the umbrella restores the convenience pull for consumers that re-export
// the whole bridges/ surface.  Lifting Cipher.h here keeps the umbrella's
// effective surface unchanged while letting SessionPersistence.h's direct
// consumers skip the heavy transitive when they don't need it.
#include <crucible/Cipher.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/bridges/SessionPersistence.h>
