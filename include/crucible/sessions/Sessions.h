#pragma once

// ── crucible::sessions umbrella ─────────────────────────────────────
//
// Protocol-level session-type stack — 12-layer MPST + SafeMPST + crash-
// stop primitives.  See session_types.md for the formal framework;
// 24_04_2026_safety_integration.md for the integration roadmap.
//
// Layers covered:
//   Session.h               — combinators (Send/Recv/Offer/Select/Loop/...)
//   SessionContext.h        — typing context Γ (L2)
//   SessionQueue.h          — queue types σ + en-route counts (L3)
//   SessionGlobal.h         — global types G + projection (L4)
//   SessionAssoc.h          — Δ ⊑_s G association invariant (L5)
//   SessionSubtype.h        — Gay-Hole 2005 subtyping (L6)
//   SessionSubtypeReason.h  — subtype_rejection_reason_t diagnostics
//   SessionCrash.h          — crash-stop primitives (L8)
//   SessionDelegate.h       — Honda 1998 throw/catch
//   SessionCheckpoint.h     — savepoint + rollback combinator
//   SessionContentAddressed.h — quotient combinator (Appendix D.5)
//   SessionDeclassify.h     — Secret payload + DeclassifyOnSend
//   SessionCT.h             — ConstantTime crypto-payload sessions
//   SessionEventLog.h       — AppendOnly typed event log
//   FederationProtocol.h    — MPST facade for Cipher federation
//   SessionPayloadSubsort.h — Tagged/Refined subsort axioms
//   SessionPermPayloads.h   — Transferable/Borrowed/Returned markers
//   SessionPatterns.h       — RequestResponse, Pipeline, FanOut/FanIn, ...
//   SessionDiagnostic.h     — manifest-bug classification tags
//   SessionView.h           — non-consuming protocol introspection
//   SpscSession.h           — typed-session wrapper for
//                              PermissionedSpscChannel (FOUND-C v1's
//                              first production-shape wiring; covers
//                              TraceRing / MetaLog / CNTP-style
//                              streaming SPSC channels).
//   MetaLogSession.h        — typed-session wrapper for
//                              PermissionedMetaLog over the production
//                              TensorMeta side-channel.
//   ChainEdgeSession.h      — one-shot semaphore signal/wait session
//                              facade over PermissionedChainEdge.
//   SwmrSession.h           — typed-session facade for SWMR latest-
//                              value publication over AtomicSnapshot
//                              + SharedPermissionPool.
//   ChaseLevDequeSession.h  — typed-session facade for
//                              PermissionedChaseLevDeque owner/thief
//                              work-stealing roles.
//
// Hot-path TUs needing a single piece include the targeted header.
// PermissionedSession.h ships per FOUND-C v1
// (`misc/27_04_csl_permission_session_wiring.md`).

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionAssoc.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionCT.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDeclassify.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionDiagnostic.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/FederationProtocol.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/SessionPatterns.h>
#include <crucible/sessions/SessionPayloadSubsort.h>
#include <crucible/sessions/SessionPermPayloads.h>
#include <crucible/sessions/SessionQueue.h>
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/SessionSubtypeReason.h>
#include <crucible/sessions/SessionView.h>
#include <crucible/sessions/SpscSession.h>
#include <crucible/sessions/MetaLogSession.h>
#include <crucible/sessions/ChainEdgeSession.h>
#include <crucible/sessions/SwmrSession.h>
#include <crucible/sessions/ChaseLevDequeSession.h>
