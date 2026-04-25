#pragma once

// ── crucible::bridges umbrella ──────────────────────────────────────
//
// Cross-substrate composition primitives:
//   MachineSessionBridge    — Machine<State> ↔ Session<Proto, R>
//   RecordingSessionHandle  — AppendOnly event log on a session handle
//   CrashTransport          — OneShotFlag × runtime crash transport
//
// Bridges depend on multiple substrates (safety/ + sessions/ +
// permissions/ + handles/) — separating them into their own
// directory makes the cross-substrate dependency explicit.

#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/bridges/RecordingSessionHandle.h>
