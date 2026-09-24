# Crucible — 27 April 2026 CSL × Permissions × Sessions Wiring Plan

*The integration document for FOUND-C: how `Permission<Tag>`, the bare `SessionHandle<Proto, Resource, LoopCtx>` typestate framework, and the value-invariant Graded substrate are wired into one composed system. Last updated 2026-04-27. Companion to `24_04_2026_safety_integration.md` (which gave the design vocabulary), `25_04_2026.md` (which formalised the Graded foundation), `27_04_2026.md` (which specified the foundation substrate inventory), and `session_types.md` (which described the protocol-level theory). This document supersedes those four where they disagree on FOUND-C; specific staleness called out in §3 below.*

---

## 0. Frontmatter

### 0.1 Voice and audience

Direct, opinionated, paper-citing where applicable, code-citing (`file.h:NNN`) for every load-bearing claim. Audience: anyone implementing FOUND-C tasks #605–#619, anyone reviewing that implementation, anyone updating the four predecessor docs after FOUND-C ships. Pre-existing mental model assumed: familiar with `safety/`, `permissions/`, `concurrent/Permissioned*.h`, `sessions/Session*.h`. Brief refresh of each axis is in §2.

### 0.2 What this document is

A complete implementation specification for FOUND-C: the integration of CSL permissions into the existing session-type framework via the `PermissionedSessionHandle<Proto, PermSet, Resource, LoopCtx>` template. Three new headers, ~1,100 lines new code, ~200 lines tests, zero refactor of existing trees, mandatory doc-update sweep bundled in. Estimated 2-3 weeks of focused work for one engineer.

### 0.3 What this document is not

Not a re-derivation of session-type theory (read `session_types.md` Part II for that). Not a re-derivation of CSL permission theory (read `THREADING.md` §5 and `permissions/Permission.h:32-86` for that). Not a re-derivation of the Graded foundation (read `25_04_2026.md` §2 for that). Not a forward-looking research agenda — every primitive specified here lands in v1; deferred items go to `v2 deferred items` (§17) with concrete criteria for promotion.

### 0.4 Decision log

The user explicitly requested **"full functionality"** on 2026-04-27. This pins the following decisions, which the predecessor docs left open:

- **D1.** All fifteen FOUND-C tasks (#605–#619) ship in v1. No deferrals.
- **D2.** `session_fork<G, Whole, RolePerms…>` ships in v1, even though `SessionGlobal.h`'s `project_t` currently uses plain merging only (#381 unshipped). Diverging-multiparty protocols (Raft, 2PC-with-multi-followers) wait on #381 separately; binary and plain-mergeable multiparty work in v1.
- **D3.** Loop body permission-balance enforcement is **mandatory** at compile time — `static_assert(PS_at_continue == PS_at_loop_entry)` for every `Continue` reachable from the body. (Risk R1.)
- **D4.** Select/Offer cross-branch PermSet convergence is **mandatory** at compile time — every branch's terminal PS must equal every other's. (Risk R2.)
- **D5.** Debug-mode abandonment-tracker is **enriched** to list leaked permission tags when `~PermissionedSessionHandle` runs with a non-empty PS. Zero release-mode cost. (Risk R3.)
- **D6.** `is_permission_balanced_v<Γ, InitialPerms>` ships standalone in v1. The `is_csl_safe_v = is_safe_v ∧ is_permission_balanced_v` conjunction documented in `session_types.md` III.L9 line 1857-1859 is **deliberately not shipped**: `is_safe_v` (Task #346, L7 SessionSafety.h) is unshipped; over-claiming would violate the Part IX honest-assessment discipline. (Risk R4.)
- **D7.** Doc-update sweep is **bundled with the implementation PR**. No "we'll update docs later" — `session_types.md` Part III.L9, Part VII M9 status, Part IX honest-assessment table, and three doc-comment references in `Sessions.h`/`SessionContext.h`/`SessionPayloadSubsort.h` all land in the same commit-sequence as the code.

---

## 1. Thesis and scope

### 1.1 One-paragraph thesis

**Crucible has three orthogonal correctness substrates: (1) Graded value invariants — `Refined<P, T>`, `Tagged<T, V>`, `Linear<T>`, `Secret<T>` — proving per-value predicates at construction; (2) CSL permissions — `Permission<Tag>`, `SharedPermission<Tag>`, `mint_permission_split/combine/fork` — proving regional ownership at every handoff; (3) session types — `Send/Recv/Select/Offer/Loop/End` over `SessionHandle<Proto, Resource, LoopCtx>` — proving event ordering between named participants. Each substrate is internally complete and individually shipping. Their pairwise compositions are partial: payload subsort axioms wire (1)↔(3) (`SessionPayloadSubsort.h`); `concurrent/Permissioned*.h` wire (2)↔(runtime channels). The three-way composition (1)×(2)×(3) — proving that values flowing through a typed protocol carry their permissions correctly — is unshipped. FOUND-C ships it as `PermissionedSessionHandle<Proto, PermSet, Resource, LoopCtx>`: a CRTP-inheriting wrapper over `SessionHandle` that carries a type-level `PermSet<Tags…>` and evolves it on every `send`/`recv`/`pick`/`branch`/`close` per the payload's permission-flow marker.**

### 1.2 What ships

Three new headers, fifteen tasks, two metafunction families, one debug-diagnostic enrichment, four doc rewrites, two existing-doc-comment refreshes, one test target, one bench target.

### 1.3 What does not ship

The L7 φ-family (`is_safe_v`, `is_df_v`, `is_live+_v`, etc.) is explicitly out of scope (Task #346, separate work). Async subtyping ⩽_a is out of scope (Task #348). Coinductive full merging is out of scope (Task #381). Cross-process permission flow over CNTP is out of scope (open question §3 of Appendix C in 24_04 doc; defer to FOUND-C v2). Lean mechanisation of permission-flow correctness (Task #419 SAFEINT-Lean-1) is out of scope; it follows the C++ landing.

### 1.4 Estimate

~1,100 LOC new code + ~200 LOC tests + ~30 LOC bench + 4 doc rewrites + 3 doc-comment refreshes. 2-3 weeks one engineer, or 1-1.5 weeks two engineers working independently on (Phase 1+2) and (Phase 3 stub then 4-5).

---

## 2. The three axes, precisely

### 2.1 Axis 1: Graded value invariants

Per-value predicates parameterised by a Modality (`Comonad`, `RelativeMonad`, `Absolute`, `Relative`) and a Lattice (`QTT`, `RefinementLattice<P>`, `ProvenanceLattice<V>`, `ConfLattice`, `FractionalLattice`, `MonotoneLattice<T, Cmp>`, `SeqPrefixLattice`, `BoolLattice<P>`, `StalenessSemiring`). Substrate: `algebra/Graded.h` per `25_04_2026.md` §2. Wrappers: `Linear<T>`, `Refined<P, T>`, `Tagged<T, V>`, `Secret<T>`, `Monotonic<T, Cmp>`, `AppendOnly<T>`, `Stale<T>`, `WriteOnce<T>` — eleven Tier-1+2 wrappers ship; tracked in `Safety.h:18-200`. The lattice flows through the type system at compile time via reflection P2996 + expansion statements P1306 + `define_static_array` P3491. Runtime cost: `sizeof(Wrapper<T>) == sizeof(T)` under `-O3`.

What this axis proves: per-value predicates checked at construction; lattice strengthening/weakening propagates through subsumption (`SessionPayloadSubsort.h:131-201`).

What this axis does NOT prove: temporal ordering, ownership, multi-thread synchronisation.

### 2.2 Axis 2: CSL permissions

Spatial/ownership invariants. Substrate: `permissions/Permission.h:208-264`. `Permission<Tag>` is an empty class, `sizeof == 1`, EBO-collapsible to 0, move-only, default-ctor private, friended factories only. `splits_into<Parent, L, R>` and `splits_into_pack<Parent, Children…>` traits manifest the region tree at compile time (`Permission.h:182-198`). Factories: `mint_permission_root<Tag>()`, `mint_permission_split<L, R>(Permission<In>&&)`, `mint_permission_combine<In>(L&&, R&&)`, `mint_permission_split_n<Children…>(In&&)` (`Permission.h:288-334`). Fractional family: `SharedPermission<Tag>` (regime-5 façade, NOT a literal `Graded<...>`), `SharedPermissionPool<Tag>` (atomic state machine with `EXCLUSIVE_OUT_BIT`), `SharedPermissionGuard<Tag>` (RAII refcount holder), `mint_permission_share<Tag>`, `with_shared_read<Tag>` (`Permission.h:441-729`). Auto-tree generators: `safety/PermissionTreeGenerator.h` (1D Slice<Parent, I>) and `safety/PermissionGridGenerator.h` (2D Producer/Consumer side-split). Structured-concurrency primitive: `mint_permission_fork<Children…>(parent, callables…)` (`PermissionFork.h:149-186`).

What this axis proves: linearity-by-Tag-identity at every handoff; declared `splits_into` manifest discipline; mode-transition atomicity (lend-vs-upgrade race resolved via single CAS).

What this axis does NOT prove: that a Tag corresponds to the memory the caller claims (no flow-sensitive alias analysis in C++; documented at `Permission.h:97-103`); temporal ordering.

### 2.3 Axis 3: Session types (protocol/ordering)

Temporal/ordering invariants between named participants. Substrate: `sessions/Session.h` (the L1 combinator core) — 2,324 lines (revised count per Agent 1 survey, doc said ~1,185). `Send/Recv/Select/Offer/Loop/Continue/End` plus MPST `Sender<Role>`/`AnonymousPeer`. Type-level metafunctions: `dual_of_t`, `compose_t`, `compose_at_branch_t`, `is_well_formed_v`, `is_empty_choice_v`, `is_terminal_state_v`. CRTP base: `SessionHandleBase<Proto, Derived=void>` with `[[no_unique_address]] consumed_tracker` (zero-byte release / `bool flag_ + std::source_location loc_` debug). Per-protocol-head specialisations: `SessionHandle<End|Send|Recv|Select|Offer|Stop|Delegate|Accept|CheckpointedSession, Resource, LoopCtx>`. Resource concept: `SessionResource` (Pinned discipline). Factories: `mint_session_handle<Proto>(Resource)`, `mint_channel<Proto>(rA, rB)`. Detach reasons: `detach_reason::*` extension-friendly tag namespace. Existing wrapper sub-classes via CRTP: `CrashWatchedHandle<P, R, PeerTag, L>` (`bridges/CrashTransport.h:271`) and `RecordingSessionHandle<P, R, L>` (`bridges/RecordingSessionHandle.h:90`) — proof that the inheritance pattern works.

What this axis proves: peer-pair duality (compile-time at `mint_channel`); state-respecting method dispatch (calling `.send()` in a Recv state is a compile error); the eventual L7 φ-family once shipped.

What this axis does NOT prove (today): φ-properties are aspirational (Task #346, all of `is_safe_v`/`is_df_v`/`is_live+_v` unshipped per `session_types.md` Part IX.1); async ⩽_a is unshipped (Task #348); full coinductive merging is unshipped (Task #381); permission balance is FOUND-C's job; causality ≺_II/≺_IO/≺_OO is unshipped (Task #340).

### 2.4 Why sessions are explicitly NOT graded

Per task #548 (SESSIONS-AUDIT) and the policy block at `Safety.h:42-90`: `Graded<Modality, Lattice, T>` requires a *lattice* — a partially-ordered set with meets and joins. A session protocol's state is a *position* in a typestate machine — `Send<T, K>`, `Recv<T, K>`, `Select<...>`, etc. — and these positions form a *transition graph*, not a lattice. There is no meaningful "join" of `Send<int, End>` and `Recv<bool, End>`; there is no "≤" between `Select<Send<A, End>, Send<B, End>>` and `Send<A, End>` that would make sense as a grade. The right algebraic structure for protocol position is the *coalgebra* of LTS labels (Honda's construction); the right structure for value invariants is the *lattice* (Plotkin-Stark grades). Conflating them would force every transition to claim a grade in a fictional lattice, losing the per-state typing surface that makes typestate work.

The audit decision: sessions stay structural (typestate parameterised by `Proto`); their *payloads* may be Graded (via `SessionPayloadSubsort.h`'s axioms); their *permission set* composes orthogonally (FOUND-C's job); but the protocol head itself is never graded. Codified in `Safety.h:42-44` (`Machine` and `ScopedView` listed as "deliberately not graded" under the same rationale; sessions are the natural extension).

### 2.5 The integration surface (what FOUND-C wires)

`PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>` is a CRTP-inheriting wrapper over `SessionHandle<Proto, Resource, LoopCtx>`. The permission-flow rules dispatch on the payload type's *shape* (Transferable / Borrowed / Returned / plain); the value-subtyping rules dispatch on the payload type's *grade* (Refined predicates, Tagged provenance). Both rule sets compose orthogonally because they operate on disjoint information — confirmed by `SessionPayloadSubsort.h:90-99`'s integration-anticipation comment.

The composition diagram:

```
                          Axis 1 (Graded values)
                        Refined / Tagged / Secret
                                  │
                                  │  payload subsort axioms
                                  │  (SessionPayloadSubsort.h)
                                  ▼
                        ┌─────────────────────┐
   Axis 3              │   PermissionedSession  │              Axis 2
  (Sessions)  ──CRTP──▶│        Handle          │◀──PermSet── (Permissions)
SessionHandle         │  Proto / PS / Res / L  │   evolution    Permission<Tag>
  Proto / R / L       │  Send / Recv / etc.    │              SharedPermission
                     └─────────────────────┘
                                  │
                                  │  payload markers
                                  │  Transferable / Borrowed / Returned
                                  ▼
                            permission flow
                       (sender loses, recipient gains)
```

Concrete: every `send(Transferable<T, X> msg, Transport tx) &&` consumes one `Permission<X>` from the sender's PS; every `recv()` of a `Recv<Transferable<T, X>, K>` produces one `Permission<X>` in the recipient's PS. `Borrowed<T, X>` carries a `ReadView<X>` that is scoped to the next protocol step. `Returned<T, X>` is the inverse of Transferable: sender returns a previously-borrowed permission to its origin. Plain payloads do not affect PS.

---

## 3. Doc staleness inventory (per-document, by line)

This is the **load-bearing section**. Every claim in another doc that disagrees with the FOUND-C v1 design is enumerated here. After FOUND-C ships, these are the lines that get rewritten.

### 3.1 `misc/session_types.md` (3,845 lines) — the worst offender

This is the canonical session-type design doc and the most-cited reference. Several of its FOUND-C-relevant sections predate the 24_04 spec and are stale.

- **Line 893 (Part III.0 architecture table):** `L9  PermissionedSession.h    CSL × session integration` — path is wrong, will be `sessions/PermissionedSession.h` not implied `safety/`. Update.
- **Lines 1822-1870 (Part III.L9 — the WHOLE section):** The single largest staleness in any Crucible doc.
  - L1827 namespace `crucible::safety::session::` — does not exist; correct namespace is `crucible::safety::proto`. Verified: `Session.h:162`, `SessionContext.h:120`, `SessionCrash.h:117`, `SessionPayloadSubsort.h:119`. Replace throughout this section.
  - L1824 path `include/crucible/safety/PermissionedSession.h` — wrong; will be `include/crucible/sessions/PermissionedSession.h`. Sessions tree moved out of `safety/` to dedicated subdir but doc was not updated.
  - L1830 template signature `PermissionedSessionHandle<Proto, Resource, Perms, LoopCtx>` — parameter order wrong; FOUND-C uses `<Proto, PS, Resource, LoopCtx>` (PermSet second to mirror CRTP base inheritance).
  - L1836 `PermissionSet<...PermTags>` — wrong name; should be `PermSet<Tags...>` per 24_04 §5 and FOUND-C tasks #605-#606.
  - L1839-1841 `Transferable<PermTag>` shipped as ONLY marker — wrong; FOUND-C ships three markers per 24_04 §5: `Transferable<T, Tag>`, `Borrowed<T, Tag>`, `Returned<T, Tag>`. The single-arg `Transferable<PermTag>` form does not exist in v1.
  - L1849-1851 `csl_compose(h1, h2)` — invented in this doc only. Not in 24_04, not in 27_04, no FOUND-C task tracks it. **Defer to v2 explicitly**; remove from L9 spec.
  - L1857-1859 `is_csl_safe_v = is_safe_v ∧ is_permission_balanced_v` — over-claim. `is_safe_v` is unshipped (Task #346). Per Decision D6, FOUND-C v1 ships `is_permission_balanced_v` standalone; the conjunction is documented as "future once L7 lands" but does NOT exist as a metafunction in v1. Rewrite to reflect this honestly.
  - **Replacement:** Rewrite the entire III.L9 section against 24_04 §5 as the normative spec. ~50 lines net.
- **Lines 3406-3428 (Part VII milestone status):**
  - M9 ❌ NOT SHIPPED → ✅ SHIPPED after FOUND-C lands. Update status emoji + per-line note.
  - M10 ❌ NOT SHIPPED depends on M9 — promote to ⚠️ PARTIAL (PermissionedMpmcChannel ships type-level PermSet correctness; the `is_safe_v` conjunction still depends on M7).
- **Lines 3491-3498 (Part IX.1 "what framework does NOT have" list):**
  - "❌ CSL × session integration (Task #333). Permission and SessionHandle are parallel systems that don't talk; Transferable<P> doesn't exist as a payload marker." — REMOVE this line; replace with "✅ CSL × session integration shipped as `PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>` per FOUND-C v1; Transferable / Borrowed / Returned payload markers ship as the three-marker spec from 24_04 §5."
  - The other six bullets (φ predicates, async ⩽_a, full merging, causality, reachable-contexts BFS, association preservation) STAY in the unshipped list — FOUND-C does not address them.
- **Line 3502 (Part IX.2 "no production callers"):** Still accurate AFTER FOUND-C v1 — the framework gains its first compile-time integration test, but no production Crucible channel is rewritten to use `PermissionedSessionHandle` in v1. K-series tasks (#355-#358) remain pending. Update to: "Production wiring of the K-series (TraceRing as PermissionedSpscSession, KernelCache as PermissionedSwmrSession, etc.) remains pending; `PermissionedSessionHandle` itself is now exercised by `test_permissioned_session_handle.cpp` and `bench_permissioned_session_handle.cpp` but no production channel uses it yet."
- **Line 3514 (Part IX.3 "claimed φ vs enforced φ" matrix):** Add a row explicitly: "PermissionedSessionHandle<Γ_MPMC, …> | csl_balanced (= is_permission_balanced_v) | enforced (compile-time, via FOUND-C v1)". Continue noting the other φ conjuncts in Part VI's table remain aspirational.
- **Lines 3340-3373 (Part VI per-channel φ-table):** No row updates required for v1 — channels still claim aspirational φ. After FOUND-C, *if* any channel adopts PermissionedSessionHandle for production, *then* its φ_csl conjunct becomes enforced. v1 doesn't trigger this.
- **Lines 1147-1192 (Part III.L1.5 handle mechanics):** No staleness — describes the existing SessionHandle correctly. FOUND-C inherits the same discipline.
- **Multiple references throughout Parts III/IV:** Many `include/crucible/safety/Session*.h` paths should be `include/crucible/sessions/Session*.h`. Sweep with a single doc-wide find/replace. Specifically: lines 933, 1196, 1325, 1390, 1423, 1676, 1762, 1824, 1874, 1941, 1975 — all reference `include/crucible/safety/...` for files that now live in `include/crucible/sessions/` or `include/crucible/permissions/`.

### 3.2 `misc/24_04_2026_safety_integration.md` (1,536 lines) — normative for FOUND-C, refine on landing

This doc is the FOUND-C spec; per Decision D7 it stays normative. Refinements bundled with the implementation PR:

- **§5 lines 82-239 (PermissionedSessionHandle spec):** This section is binding for FOUND-C v1. After landing, add a SHIPPED-AS marker noting any deviations:
  - L184 `template <typename Proto, typename PS, typename Resource, typename LoopCtx = void>` matches v1.
  - L196-201 `SendablePayload<T, PS>` concept matches v1.
  - L205 `requires SendablePayload<T, PS>` matches v1.
  - L213-214 `mint_permissioned_session<Proto, Resource, InitPerms…>` matches v1.
  - L217-222 `establish_n_party_permissioned<G, Whole, RolePerms…>` — RENAMED to `session_fork<G, Whole, RolePerms…>` per 27_04 §5.2 (more idiomatic) and to match the existing `mint_permission_fork` factory. Mark renaming in SHIPPED-AS marker.
  - L225-232 wrapper-class layout — SHIPPED, with one addition: PS is the second template parameter (between Proto and Resource), not nested in `class` body. Doc says "tracks Proto state AND PermSet" — clarify that PS is a template parameter.
- **§6 lines 264-294 (Tagged-provenance subsort axioms):** SHIPPED in `SessionPayloadSubsort.h` (predates FOUND-C). No update.
- **§7 lines 320-378 (Refined subsort axioms):** Same — SHIPPED in `SessionPayloadSubsort.h`. No update.
- **§9 lines 449-538 (session_fork multi-party):** SHIPPED in v1 per Decision D2. Note: works for protocols whose projection is sound under plain merging; diverging multiparty protocols (Raft, 2PC-with-multi-followers) wait on Task #381 (full coinductive merging in `SessionGlobal.h`). Add SHIPPED-AS note: "`session_fork` works for binary and plain-mergeable multiparty in v1; full-merging-dependent protocols return `static_assert` failure with diagnostic naming the responsible projection."
- **§10 lines 540-594 (Machine ↔ Session bridge):** Out of scope for FOUND-C v1. No update; remains pending.
- **§11 lines 595-634 (OneShotFlag crash transport):** Partially shipped via `bridges/CrashTransport.h::CrashWatchedHandle`. FOUND-C v1 wires `OneShotFlag::peek()` into `PermissionedSessionHandle`'s send/recv; the underlying `CrashWatchedHandle` pattern is the model. Add SHIPPED-AS note: "`PermissionedSessionHandle` integrates `OneShotFlag` per-peer crash signals via the existing `CrashWatchedHandle` pattern; `notify_crash(PeerId)` global hook is NOT shipped — callers wire `OneShotFlag::signal()` themselves from their detection layer."
- **§43 lines 1196-1206 (Epoch II "PermissionedSessionHandle thesis weeks 3-5"):** This IS FOUND-C. Update timeline to "weeks 1-3 of FOUND-C series, landed 2026-04-XX" once shipped.
- **§44 line 1216 ("§5 PermissionedSessionHandle | 600 | 0 | Medium-high"):** Line estimate revised to ~1,100 lines for full functionality (three markers, all five protocol-head specialisations, Loop balance + branch convergence enforcement, debug diagnostic enrichment, both `mint_permissioned_session` and `session_fork` factories). Update the Risk column from "Medium-high" to "Medium" — the implementation has fewer unknowns post-Agent surveys.
- **§50 line 1290-1294 (Lean modules):** `PermissionFlow.lean` (#419) follows FOUND-C C++ landing. No update needed; remains tracked as separate Lean phase.

### 3.3 `misc/25_04_2026.md` (1,913 lines) — Graded foundation refactor

Mostly orthogonal to FOUND-C. Two specific items need correction:

- **§2.3 line 150 "`using SharedPermission = Graded<Absolute, FractionalLattice, Tag>`":** Wrong as a literal mapping. The actual code in `permissions/Permission.h:441-502` ships `SharedPermission<Tag>` as an empty phantom token (sizeof 1, EBO-collapsible) with `graded_type` exposed only as a typedef for diagnostic introspection (regime-5 façade per `Permission.h:32-86`'s MIGRATE-7 audit block). The `Graded<...>` form would force a `Rational` into every instance, breaking the proof-token design. Replace L150 with: "`using SharedPermission = /* regime-5 façade — empty phantom token with graded_type typedef for introspection; runtime fractional state lives in SharedPermissionPool<Tag>; see permissions/Permission.h:32-86 MIGRATE-7 audit block */`."
- **§2.5 line 209 "every other primitive in this document either is a `Graded<L, T>` instantiation or composes with one":** Add caveat: "...except sessions, which per task #548 (SESSIONS-AUDIT) are deliberately structural — typestate machines parameterised by Proto, not lattice grades. Sessions compose with Graded values via `SessionPayloadSubsort.h` axioms (payloads may be Graded) and with CSL permissions via `PermissionedSessionHandle` (FOUND-C; permissions are NOT Graded either, they are linear tokens), but the protocol head itself never carries a grade."

### 3.4 `misc/27_04_2026.md` (1,202 lines) — foundation substrate inventory

This doc is the closest to FOUND-C in time and intent.

- **§5.2 lines 283-339 (PermissionedSessionHandle — the unfinished thesis):** SHIPPED in v1 per Decision D1. Update §5.2 status to ✅ SHIPPED, add reference to this document (`misc/27_04_csl_permission_session_wiring.md`) as the implementation plan.
- **§5.5 lines 442-507 (signature dispatcher reading surface):** `is_session_handle_v` and `inferred_permission_tags_t` (#627, #630) become satisfiable for `PermissionedSessionHandle` instantiations after FOUND-C lands. Update §5.5 step 6 to note: "`is_session_handle_v<Sig>` ships in FOUND-D for both `SessionHandle` and `PermissionedSessionHandle`; the latter additionally exposes `inferred_permission_tags_t<Sig>` returning the PermSet."
- **§5.13 lines 817-849 (Cipher integration for compiled-body cache):** Out of scope for FOUND-C. No update.

### 3.5 `misc/THREADING.md` (3,125 lines) — concurrency framework

Mostly current. One clarification:

- **§17.13 lines 2173-2225 (Session type handle machinery for MpmcChannel):** Describes the `mpmc_session::Active/Closed` per-wrapper local typestate. Add a clarifying note: "This is a LOCAL per-wrapper typestate — Honda-Vasconcelos-Kubo 1998 binary session at one specific channel. It is NOT the full Session<Proto, Resource, LoopCtx> framework from `sessions/Session.h`. Production code wanting full session-type semantics over MPMC channels should compose `PermissionedSessionHandle<MpmcProto, PS, Channel, void>` per FOUND-C, NOT extend `mpmc_session::*`."

### 3.6 `CLAUDE.md` (~160 KB) — project guide

- **L0 reference paragraph** mentions `safety/Session*.h` paths. Update to `sessions/Session*.h` and `permissions/Permission.h` to match the post-move file layout. Specifically: every `safety/Session*.h` reference becomes `sessions/Session*.h`; every `safety/Permission.h` reference becomes `permissions/Permission.h`.
- **L0 wrapper catalog** (the table around line 70-100): Add `PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>` after `Session<Proto>`. One-line entry: "Threads CSL permission set through session-protocol position; Send consumes, Recv produces, close surrenders."

### 3.7 Per-header doc-comment refreshes (in-tree)

Three doc-comment updates inside existing headers, bundled with the implementation PR:

- **`include/crucible/sessions/Sessions.h:30`:** Change "PermissionedSession.h ships per #333 (SEPLOG-H2b — pending)." → "PermissionedSession.h ships per FOUND-C v1 (`misc/27_04_csl_permission_session_wiring.md`)."
- **`include/crucible/sessions/SessionContext.h:19`:** Change "L9 CSL × session (PermissionedSession.h, task #333) — permission balance is an invariant on Γ preserved by reductions." → "L9 CSL × session (`PermissionedSession.h`, FOUND-C v1) — `is_permission_balanced_v<Γ, InitialPerms>` ships standalone; conjunction with `is_safe_v` deferred until L7 (Task #346)."
- **`include/crucible/sessions/SessionPayloadSubsort.h:90-99`:** Change "**Integration with PermissionedSessionHandle (task #333)**" header. Specifically rewrite as a SHIPPED-AS reference: "**Integration with PermissionedSessionHandle (FOUND-C v1, shipped 2026-04-XX)** — see `sessions/PermissionedSession.h` for the wrapper. The permission-flow rules dispatch on payload SHAPE (Transferable / Borrowed / Returned / plain) via `sessions/SessionPermPayloads.h` markers; the subsumption rules in this header determine which payload TYPES are interchangeable through Send's covariance / Recv's contravariance. Both layers compose cleanly because they operate on disjoint information."

---

## 4. Component reality survey (verified by Agent 1 + Agent 2)

### 4.1 What ships (no work needed)

- `sessions/Session.h` (2,324 lines) — combinator core, dual_of_t, compose_t, is_well_formed_v, SessionHandleBase CRTP with abandonment-tracker, per-protocol-head SessionHandle specialisations (End/Send/Recv/Select/Offer/Stop), mint_session_handle, mint_channel, SessionResource concept, detach_reason::* tags. Solid; reused as-is.
- `sessions/SessionContext.h` (693), `SessionQueue.h` (558), `SessionGlobal.h` (1,112), `SessionAssoc.h` (551), `SessionSubtype.h` (858), `SessionSubtypeReason.h` (610), `SessionCrash.h` (776), `SessionDelegate.h` (909), `SessionCheckpoint.h` (460), `SessionContentAddressed.h` (562), `SessionDeclassify.h` (382), `SessionCT.h` (395), `SessionEventLog.h` (349), `SessionPayloadSubsort.h` (582), `SessionPatterns.h` (910), `SessionDiagnostic.h` (825), `SessionView.h` (411). Total ~13,316 lines, 24 test files, 102/102 tests green. Reused as-is.
- `bridges/CrashTransport.h::CrashWatchedHandle` and `bridges/RecordingSessionHandle.h::RecordingSessionHandle` — existing wrapper sub-classes via CRTP `SessionHandleBase<P, Self>`. Pattern reused for FOUND-C.
- `permissions/Permission.h:208-264` — `Permission<Tag>` linear token. Reused.
- `permissions/Permission.h:441-664` — `SharedPermission<Tag>`, `SharedPermissionPool<Tag>`, `SharedPermissionGuard<Tag>`. Reused.
- `permissions/Permission.h:288-334` — factories `mint_permission_root`, `mint_permission_split`, `mint_permission_combine`, `mint_permission_split_n`. Reused.
- `permissions/Permission.h:275-278` — `permission_drop<Tag>`. Reused.
- `permissions/PermissionFork.h` — `mint_permission_fork<Children…>`. Reused.
- `permissions/ReadView.h` — `ReadView<Tag>` for scoped read borrow. Reused.
- `safety/PermissionTreeGenerator.h` and `safety/PermissionGridGenerator.h` — auto-tree generators (FOUND-A21/A22). Reused.
- `safety/OneShotFlag.h` — `OneShotFlag` for one-shot crash signal. Reused.

### 4.2 What needs to be added (FOUND-C scope)

- **Header A:** `permissions/PermSet.h` (~120 lines new). Type-list of permission tags + ops.
- **Header B:** `sessions/SessionPermPayloads.h` (~100 lines new). Three markers + traits.
- **Header C:** `sessions/PermissionedSession.h` (~700 lines new). The CRTP wrapper + factories + crash wiring.
- **Helper:** `permissions/Permission.h:334+` extension (~30 lines): `mint_permission_combine_n<Parent, Children…>(perms…)` to invert `mint_permission_split_n`. Currently only the binary `mint_permission_combine` exists; needed for `session_fork`'s rebuild path.
- **Concept gates:** Add to `permissions/Permission.h` (~20 lines): `concept Permission<T>` and `concept SharedPermissionFor<T, Tag>` for FOUND-D consumption later. Bundling these with FOUND-C avoids two passes through the same file.
- **Test target:** `test/test_permissioned_session_handle.cpp` (~150 lines new). Positive runtime + compile-time fixtures.
- **Negative-compile target:** `test/sessions_neg/permission_imbalance.cpp` (~50 lines new). Ten failing-by-design fixtures with classified diagnostics.
- **Bench target:** `bench/bench_permissioned_session_handle.cpp` (~50 lines new). Sizeof-equality + machine-code-parity vs bare `SessionHandle`.

### 4.3 What needs to be DELETED (none)

Verified by Agent 3 grep sweep: zero stub code exists for FOUND-C anywhere in the tree. `grep -rn "PermissionedSession" include/crucible/` returns three references — all forward-references to the unshipped layer (`Sessions.h:30` umbrella reservation, `SessionContext.h:19` doc comment, `SessionPayloadSubsort.h:90-99` integration anticipation). No stub headers, no stub tests, no stub `class PermissionedSessionHandle` declaration anywhere. Clean greenfield landing.

`grep -rn "Graded\|grade_of\|algebra/Graded\|ModalityKind" include/crucible/sessions/` returns no results — sessions/ tree is graded-clean per the SESSIONS-AUDIT decision. No refactor needed.

---

## 5. The architecture stack

```
┌────────────────────────────────────────────────────────────────────┐
│                Production callers (NOT in FOUND-C v1)                │
│   K-series tasks #355-#358: Vessel dispatch / KernelCache / Inference │
└────────────────────────────────────────────────────────────────────┘
                              ▲
                              │
┌────────────────────────────────────────────────────────────────────┐
│  sessions/PermissionedSession.h (Header C, FOUND-C v1, ~700 lines)  │
│                                                                      │
│  PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>             │
│   : public SessionHandleBase<Proto, PermissionedSessionHandle<...>>  │
│                                                                      │
│  - Per-protocol-head specialisations (End/Send/Recv/Select/Offer)    │
│  - send/recv/pick/branch/close evolve PS per payload marker          │
│  - Loop body PS-balance enforcement (R1)                             │
│  - Select/Offer cross-branch PS convergence (R2)                     │
│  - Debug abandonment-tracker enrichment with leaked tags (R3)        │
│  - mint_permissioned_session<Proto, Resource, InitPerms...>             │
│  - session_fork<G, Whole, RolePerms...>                              │
│  - OneShotFlag-based crash transport per-peer                        │
│  - is_permission_balanced_v<Γ, InitialPerms> standalone              │
└────────────────────────────────────────────────────────────────────┘
                  ▲                              ▲
                  │                              │
                  │                              │
┌──────────────────────────────┐   ┌──────────────────────────────────┐
│  sessions/SessionPermPayloads │   │  permissions/PermSet.h           │
│  .h (Header B, ~100 lines)    │   │  (Header A, ~120 lines)          │
│                                │   │                                  │
│  Transferable<T, Tag>          │   │  PermSet<Tags...>                │
│  Borrowed<T, Tag>              │   │  EmptyPermSet                    │
│  Returned<T, Tag>              │   │  perm_set_contains_v             │
│  is_transferable_v             │   │  perm_set_insert_t               │
│  is_borrowed_v                 │   │  perm_set_remove_t               │
│  is_returned_v                 │   │  perm_set_union_t                │
│  is_plain_payload_v            │   │  perm_set_difference_t           │
│  compute_perm_set_after_send_t │   │  perm_set_canonicalize_t         │
│  compute_perm_set_after_recv_t │   │                                  │
└──────────────────────────────┘   └──────────────────────────────────┘
                  ▲                              ▲
                  │                              │
┌────────────────────────────────────────────────────────────────────┐
│   Existing substrates (reused as-is)                                │
│                                                                      │
│   sessions/Session.h ─ SessionHandleBase<Proto, Derived>             │
│                          per-head SessionHandle specialisations       │
│                          mint_session_handle, mint_channel       │
│                          SessionResource concept, detach_reason::*    │
│                                                                      │
│   sessions/SessionGlobal.h ─ project_t<G, Role> for session_fork     │
│   sessions/SessionPayloadSubsort.h ─ Refined/Tagged subsort axioms   │
│   sessions/SessionCrash.h ─ Stop, Crash<P>, ReliableSet              │
│                                                                      │
│   permissions/Permission.h ─ Permission<Tag>, splits_into[_pack]     │
│                              mint_permission_root/split/combine[_n]   │
│                              SharedPermission<Tag>, Pool, Guard       │
│   permissions/PermissionFork.h ─ mint_permission_fork                     │
│   permissions/ReadView.h ─ ReadView<Tag>                             │
│   safety/PermissionTreeGenerator.h ─ 1D Slice                        │
│   safety/PermissionGridGenerator.h ─ 2D side-split                   │
│   safety/OneShotFlag.h ─ one-shot crash signal                       │
│                                                                      │
│   bridges/CrashTransport.h::CrashWatchedHandle ─ pattern model       │
│   bridges/RecordingSessionHandle.h::RecordingSessionHandle ─ ditto   │
└────────────────────────────────────────────────────────────────────┘
```

---

## 6. Spec — `permissions/PermSet.h` (Header A, ~120 lines)

### 6.1 Public surface

```cpp
namespace crucible::safety::proto {

// Type-list of currently-held permission tags.  Empty class; sizeof = 1.
template <typename... Tags>
struct PermSet {
    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyPermSet = PermSet<>;

// Membership predicate.
template <typename PS, typename Tag>
inline constexpr bool perm_set_contains_v = /* fold */;

// Insert (unique-prepend) — no-op if Tag already in PS.
template <typename PS, typename Tag>
struct perm_set_insert;
template <typename PS, typename Tag>
using perm_set_insert_t = typename perm_set_insert<PS, Tag>::type;

// Remove — no-op if Tag not in PS.
template <typename PS, typename Tag>
struct perm_set_remove;
template <typename PS, typename Tag>
using perm_set_remove_t = typename perm_set_remove<PS, Tag>::type;

// Disjoint union — fails (static_assert) if PS1 and PS2 share any tag.
template <typename PS1, typename PS2>
struct perm_set_union;
template <typename PS1, typename PS2>
using perm_set_union_t = typename perm_set_union<PS1, PS2>::type;

// Set difference — PS1 minus PS2's tags.
template <typename PS1, typename PS2>
struct perm_set_difference;
template <typename PS1, typename PS2>
using perm_set_difference_t = typename perm_set_difference<PS1, PS2>::type;

// Set equality (order-insensitive).
template <typename PS1, typename PS2>
inline constexpr bool perm_set_equal_v = /* sort + compare */;

// Canonical sort — for hash equality and diagnostic display.  Uses
// reflect_hash<Tag> for stable ordering across compile invocations.
template <typename PS>
struct perm_set_canonicalize;
template <typename PS>
using perm_set_canonicalize_t = typename perm_set_canonicalize<PS>::type;

// Reflection-driven name for diagnostics.
template <typename PS>
constexpr std::string_view perm_set_name = /* P2996 display */;

}  // namespace crucible::safety::proto
```

### 6.2 Implementation discipline

- **Fold expressions everywhere.** Linear in `sizeof...(Tags)`, no recursive template depth above `O(N)`. C++26 pack-indexing `Ts...[N]` (P2662R3) for direct access.
- **Canonical sort via `reflect_hash<Tag>`.** `define_static_array<canonicalize_pack<Tags…>>` materialises the sorted form for hash equality. Two `PermSet<A, B>` and `PermSet<B, A>` instances have the same `perm_set_canonicalize_t` and the same `perm_set_equal_v == true`.
- **`perm_set_union` is disjoint.** Tag-union is structurally restricted: a permission cannot be held by two participants simultaneously. Static_assert with diagnostic naming the duplicate tag.
- **No SFINAE.** All checks are `static_assert` with classified diagnostics from `SessionDiagnostic.h`. Per the project's zero-SFINAE invariant (Task #136).
- **`runtime_smoke_test()` per the discipline in the global memory:** include a `inline void runtime_smoke_test()` that exercises every metafunction with non-constant args + move-only T witness + concept-based capability checks.

### 6.3 Ancillary additions to `permissions/Permission.h`

- **`mint_permission_combine_n<Parent, Children…>(Permission<Children>&&...)`** — N-ary inverse of `mint_permission_split_n`. Currently only binary `mint_permission_combine` exists. Needed by `session_fork` to rebuild the parent permission after all role bodies join. Same `splits_into_pack_v<Parent, Children…>` static_assert gate. ~20 lines added.
- **`concept Permission`** — holds when `T = Permission<Tag>` for some `Tag` (introspectable via `T::tag_type`). ~5 lines.
- **`concept SharedPermissionFor<T, Tag>`** — holds when `T = SharedPermission<Tag>`. ~5 lines.

These concept gates feed FOUND-D's signature dispatcher (`is_permission_v` / `is_shared_permission_v`, Task #623). Bundling here avoids two passes.

### 6.4 Test coverage (in `test/test_permissioned_session_handle.cpp`)

- Compile-time: every metafunction, every edge case (empty set, single-element, union of overlapping → static_assert fires).
- Runtime smoke: `EmptyPermSet::size == 0`, `perm_set_contains_v<PermSet<A, B>, A> == true`, etc.
- Reflection: `perm_set_canonicalize_t<PermSet<B, A>> == perm_set_canonicalize_t<PermSet<A, B>>`.

---

## 7. Spec — `sessions/SessionPermPayloads.h` (Header B, ~100 lines)

### 7.1 Public surface

```cpp
namespace crucible::safety::proto {

// Sender LOSES Permission<Tag>; recipient GAINS it.  Carries the value
// payload T plus the (zero-byte) Permission token.
template <typename T, typename Tag>
struct Transferable {
    T                                    value;
    [[no_unique_address]] Permission<Tag> perm;
    using payload_type      = T;
    using transferred_perm  = Tag;
};

// Sender LENDS read access scoped to the next protocol step.
template <typename T, typename Tag>
struct Borrowed {
    T                       value;
    ReadView<Tag>           view;
    using payload_type   = T;
    using borrowed_perm  = Tag;
};

// Receiver returns a previously-borrowed permission to its origin.
template <typename T, typename Tag>
struct Returned {
    T                                    value;
    [[no_unique_address]] Permission<Tag> returned_perm;
    using payload_type = T;
    using returned     = Tag;
};

// Type-level recognisers.
template <typename T> inline constexpr bool is_transferable_v = /* match */;
template <typename T> inline constexpr bool is_borrowed_v     = /* match */;
template <typename T> inline constexpr bool is_returned_v     = /* match */;
template <typename T>
inline constexpr bool is_plain_payload_v =
    !is_transferable_v<T> && !is_borrowed_v<T> && !is_returned_v<T>;

// Per-marker permission-flow dispatch.
template <typename PS, typename T> struct compute_perm_set_after_send;
template <typename PS, typename T>
using compute_perm_set_after_send_t = typename compute_perm_set_after_send<PS, T>::type;

template <typename PS, typename T> struct compute_perm_set_after_recv;
template <typename PS, typename T>
using compute_perm_set_after_recv_t = typename compute_perm_set_after_recv<PS, T>::type;

// Concept gate for sendability — sender must hold the transferred permission.
template <typename T, typename PS>
concept SendablePayload =
    is_plain_payload_v<T> ||
    is_borrowed_v<T> ||
    (is_transferable_v<T> && perm_set_contains_v<PS, typename T::transferred_perm>) ||
    (is_returned_v<T>     && perm_set_contains_v<PS, typename T::returned>);

// Recv side has no precondition (always receivable); the PS evolution
// captures the resulting permission acquisition.
template <typename T, typename PS>
concept ReceivablePayload = true;

}  // namespace crucible::safety::proto
```

### 7.2 PermSet evolution rules (the central dispatch table)

| Send/Recv shape                              | PermSet evolution                      |
|----------------------------------------------|----------------------------------------|
| `Send<Plain T, K>`                           | `PS' = PS`                             |
| `Send<Transferable<T, X>, K>`                | `PS' = perm_set_remove_t<PS, X>`       |
| `Send<Borrowed<T, X>, K>`                    | `PS' = PS` (borrow is scoped, not transferred) |
| `Send<Returned<T, X>, K>`                    | `PS' = perm_set_remove_t<PS, X>`       |
| `Recv<Plain T, K>`                           | `PS' = PS`                             |
| `Recv<Transferable<T, X>, K>`                | `PS' = perm_set_insert_t<PS, X>`       |
| `Recv<Borrowed<T, X>, K>`                    | `PS' = PS` (recipient gets ReadView only) |
| `Recv<Returned<T, X>, K>`                    | `PS' = perm_set_insert_t<PS, X>`       |

### 7.3 Why three markers, not just one

`Transferable` covers the common case: producer sends a permission to consumer, consumer gains exclusive access. But two real-world patterns need richer payloads:

- **Borrowed:** Vessel dispatch lends the bg drainer a *read-only view* of TraceEntry data; the borrow is scoped to the recipient's next step. Encoded as `Borrowed<TraceEntry, TraceRingTag>` — the recipient gets `ReadView<TraceRingTag>` for the duration. This composes with the existing `permissions/ReadView.h`.
- **Returned:** Cipher tier promotion — hot-tier delegates the entry's session to warm-tier with `Permission<HotEntry>`. Once warm-tier finishes, it `Returned<DurabilityAck, HotEntry>` the permission back. Without `Returned`, the round-trip would require two separate Transferables and PS bookkeeping at the protocol level rather than the message level.

The three-marker design is specified in 24_04 §5.2 lines 122-150 and is normative for FOUND-C v1.

### 7.4 Test coverage

- Compile-time: every payload-marker shape exercises every PermSet evolution rule.
- Compile-error: `Send<Transferable<T, X>, K>` from a handle whose PS does not contain X — failing static_assert with `PermissionImbalance` diagnostic from `SessionDiagnostic.h:207`.
- Runtime: round-trip a Transferable through an in-memory transport, verify PS evolution at every step.

---

## 8. Spec — `sessions/PermissionedSession.h` core (Header C, ~700 lines)

### 8.1 Template structure

```cpp
namespace crucible::safety::proto {

// Forward declaration.
template <typename Proto, typename PS, typename Resource, typename LoopCtx = void>
class PermissionedSessionHandle;

// Per-protocol-head specialisations follow Session.h's pattern exactly.
// CRTP inheritance from SessionHandleBase<Proto, PSH<...>>.
}
```

### 8.2 Why CRTP inheritance, not composition

This was the key architectural decision settled by Agent 3's triangulation. Rationale:

- **Diagnostic naming.** `SessionHandleBase`'s wrapper-name reflection at `Session.h:1394` reads "Wrapper class: SessionHandle" if Derived isn't passed; "Wrapper class: PermissionedSessionHandle" if it is. Production crash handlers catching abandoned-protocol diagnostics need to know which wrapper aborted (the same Proto can be used by `SessionHandle`, `CrashWatchedHandle`, `RecordingSessionHandle`, and `PermissionedSessionHandle` simultaneously). Composition would force the diagnostic to misreport.
- **Abandonment tracker reuse.** The debug-mode `consumed_tracker` (`Session.h:1149`) at zero release-mode cost (`[[no_unique_address]]` + EBO). Composition would force a second tracker, doubling the cost in debug.
- **Sizeof equality.** `sizeof(PermissionedSessionHandle<P, PS, R, L>) == sizeof(R)` because PS is empty class and SessionHandleBase is empty in release. Composition would add a pointer-to-inner-handle, breaking the zero-overhead claim.
- **Existing precedent.** `CrashWatchedHandle` (`bridges/CrashTransport.h:271`) and `RecordingSessionHandle` (`bridges/RecordingSessionHandle.h:90`) both use CRTP inheritance per-protocol-head. Following the same pattern keeps the framework's mental model consistent.

### 8.3 Per-head specialisations

Mirror `Session.h`'s pattern at lines 1504, 1558, 1614, 1670, 1807. One specialisation per:

#### 8.3.1 `PermissionedSessionHandle<End, PS, Resource, LoopCtx>`

```cpp
template <typename PS, typename R, typename L>
class [[nodiscard]] PermissionedSessionHandle<End, PS, R, L>
    : public SessionHandleBase<End,
        PermissionedSessionHandle<End, PS, R, L>>
{
    R resource_;
    [[no_unique_address]] PS perm_set_;
    // ... ctors, friend factories ...
public:
    using protocol  = End;
    using perm_set  = PS;
    using resource_type = R;
    using loop_ctx  = L;

    // close() requires PS to have been surrendered to the declared
    // exit set.  In v1 we require PS == EmptyPermSet at End — every
    // Transferable that came in must have gone out.  More flexible
    // exit semantics (declared ExitPermSet template parameter)
    // deferred to v2.
    [[nodiscard]] R close() && noexcept
    {
        static_assert(perm_set_equal_v<PS, EmptyPermSet>,
            "[PermissionImbalance] PermissionedSessionHandle reached "
            "End with non-empty PermSet — "
            "every Transferable received must be surrendered before close(). "
            "Surrender remaining permissions via send(Returned<...>) or "
            "explicit permission_drop<Tag>() at close.");
        this->mark_consumed_();
        return std::move(resource_);
    }
};
```

#### 8.3.2 `PermissionedSessionHandle<Send<T, K>, PS, Resource, LoopCtx>`

```cpp
template <typename T, typename K, typename PS, typename R, typename L>
class [[nodiscard]] PermissionedSessionHandle<Send<T, K>, PS, R, L>
    : public SessionHandleBase<Send<T, K>,
        PermissionedSessionHandle<Send<T, K>, PS, R, L>>
{
    R resource_;
    [[no_unique_address]] PS perm_set_;
public:
    template <typename Transport>
        requires SendablePayload<T, PS> &&
                 std::is_invocable_v<Transport, R&, T&&>
    [[nodiscard]] auto send(T value, Transport tx) && noexcept
    {
        using NewPS = compute_perm_set_after_send_t<PS, T>;
        std::invoke(tx, resource_, std::move(value));
        this->mark_consumed_();
        return detail::step_to_next_permissioned<K, NewPS, R, L>(
            std::move(resource_));
    }
};
```

#### 8.3.3 `PermissionedSessionHandle<Recv<T, K>, PS, Resource, LoopCtx>`

Symmetric to Send; PS gains permissions per `compute_perm_set_after_recv_t`.

#### 8.3.4 `PermissionedSessionHandle<Select<Bs…>, PS, Resource, LoopCtx>`

`pick<I>()` and `select<I>(Transport)` consume the Select handle and dispatch into branch I. Per Decision D4, all branches must converge on the same terminal PS — enforced by `static_assert(all_branches_converge_v<Bs..., PS>)`.

#### 8.3.5 `PermissionedSessionHandle<Offer<Bs…>, PS, Resource, LoopCtx>`

`branch(Transport, Visitor)` — Visitor is a callable that takes a `std::variant`-like dispatch over branches. Same convergence requirement.

#### 8.3.6 `PermissionedSessionHandle<Stop, PS, Resource, LoopCtx>`

Same as End; close() returns Resource and surrenders PS.

### 8.4 Loop / Continue resolution (`step_to_next_permissioned`)

```cpp
namespace detail {
template <typename Next, typename PS, typename R, typename L>
[[nodiscard]] constexpr auto step_to_next_permissioned(R r) noexcept
{
    if constexpr (std::is_same_v<Next, Continue>) {
        static_assert(!std::is_void_v<L>,
            "[Continue_Without_Loop] PermissionedSessionHandle: "
            "Continue appears outside a Loop context.");
        // Loop balance enforcement (Decision D3 / Risk R1).
        using LoopBody    = typename L::body;
        using LoopEntryPS = typename L::entry_perm_set;
        static_assert(perm_set_equal_v<PS, LoopEntryPS>,
            "[PermissionImbalance] PermissionedSessionHandle: "
            "Loop body's terminal PermSet differs from entry PermSet — "
            "Loop iteration permission flow must balance per iteration. "
            "Either surrender the leftover Transferables before Continue, "
            "or restructure the protocol to receive matching Returned "
            "permissions on each iteration.");
        return PermissionedSessionHandle<LoopBody, LoopEntryPS, R, L>{std::move(r)};
    } else if constexpr (is_loop_v<Next>) {
        using InnerBody = typename Next::body;
        // Enter inner Loop: shadow LoopCtx with a new context carrying
        // PS as the new entry point.
        using InnerLoopCtx = LoopContext<InnerBody, PS>;
        return PermissionedSessionHandle<InnerBody, PS, R, InnerLoopCtx>{std::move(r)};
    } else {
        return PermissionedSessionHandle<Next, PS, R, L>{std::move(r)};
    }
}
}  // namespace detail
```

The `LoopCtx` parameter for permissioned handles is `LoopContext<Body, EntryPS>` (vs the bare `Body` in Session.h's L parameter). Carrying EntryPS along with the body lets the static_assert at Continue check balance.

### 8.5 Branch convergence enforcement (Decision D4 / Risk R2)

```cpp
namespace detail {
template <typename... Branches, typename EntryPS>
inline constexpr bool all_branches_converge_v = []{
    // Compute terminal PS for each branch by walking its protocol tree.
    // All terminal PS values must be equal (perm_set_equal_v).
    /* metafunction body */
}();
}
```

Static_assert at the Select/Offer constructor with diagnostic naming the divergent branches.

### 8.6 Debug-mode abandonment-tracker enrichment (Decision D5 / Risk R3)

```cpp
template <typename Proto, typename PS, typename R, typename L>
~PermissionedSessionHandle()
{
#ifndef NDEBUG
    if (!this->is_consumed_() && !is_terminal_state_v<Proto>) {
        // Standard SessionHandleBase abandonment diagnostic (existing).
        // PLUS: enumerate the leaked permission tags.
        if constexpr (PS::size > 0) {
            fprintf(stderr,
                "  Leaked permissions: %s\n"
                "  Each Permission<Tag> in the PermSet was acquired via "
                "Recv but never surrendered via Send<Returned<...>> or "
                "explicit permission_drop<Tag>().\n",
                perm_set_name<PS>.data());
        }
        // Continue to base destructor's abort.
    }
#endif
}
```

Zero release-mode cost — `if constexpr (PS::size > 0)` and the `fprintf` are wrapped in `#ifndef NDEBUG`. PS itself is empty class.

### 8.7 `is_permission_balanced_v<Γ, InitialPerms>` standalone (Decision D6)

```cpp
namespace crucible::safety::proto {

// Walks Γ's reduction LTS, verifying that the union of active perm sets
// across all participants is constant modulo mint_permission_split/combine
// at fork points.  Standalone in v1 — does NOT compose with is_safe_v
// (unshipped).  Defer the composition to L7 (Task #346).
template <typename Gamma, typename InitialPerms>
inline constexpr bool is_permission_balanced_v = /* metafunction */;

}  // namespace crucible::safety::proto
```

Per Decision D6, this is shipped as a standalone metafunction. It does NOT define `is_csl_safe_v` (which would conjunct with the unshipped `is_safe_v`).

---

## 9. Spec — mint_permissioned_session + session_fork (~150 lines, in Header C)

### 9.1 `mint_permissioned_session`

```cpp
template <typename Proto, typename Resource, typename... InitPerms>
[[nodiscard]] auto mint_permissioned_session(
    Resource r,
    Permission<InitPerms>&&... perms) noexcept
    -> PermissionedSessionHandle<Proto, PermSet<InitPerms...>, Resource>
{
    // Consume Permission tokens; the PermSet phantom records the held set.
    (void)std::tie(perms...);  // the Permissions are consumed by the move
    return PermissionedSessionHandle<Proto, PermSet<InitPerms...>, Resource>{
        std::move(r)};
}
```

Binary case. Full-functionality v1.

### 9.2 `session_fork` (Decision D2)

```cpp
template <typename G, typename Whole, typename... RolePerms,
          typename SharedChannel, typename... Bodies>
[[nodiscard]] Permission<Whole> session_fork(
    SharedChannel& ch,
    Permission<Whole>&& whole_perm,
    Bodies&&... bodies) noexcept
    requires (sizeof...(RolePerms) == sizeof...(Bodies))
          && (sizeof...(RolePerms) == roles_of_t<G>::size)
{
    // 1. Split Whole into per-role permissions.
    auto perms = mint_permission_split_n<RolePerms...>(std::move(whole_perm));

    // 2. Project G to per-role local types.  WARNING: uses plain merging
    //    only; diverging-multiparty protocols (Raft, 2PC-with-multi-followers)
    //    produce a static_assert from project_t naming the divergent branch
    //    until Task #381 (full coinductive merging) lands.
    //
    // For plain-mergeable protocols (binary, FanOut, FanIn, RequestResponse,
    //    SwimProbe, simple multiparty), session_fork works in v1.

    // 3. Spawn each body via mint_permission_fork.  Each body receives:
    //    - its projected PermissionedSessionHandle<project_t<G, Role_i>,
    //                                              PermSet<RolePerm_i>,
    //                                              SharedChannel&>
    return mint_permission_fork<RolePerms...>(
        mint_permission_combine_n<Whole, RolePerms...>(std::move(perms)),
        [&ch, b = std::forward<Bodies>(bodies)](auto&& role_perm) noexcept {
            using Role = typename std::remove_reference_t<decltype(role_perm)>::tag_type;
            using LocalProto = project_t<G, Role>;
            auto handle = PermissionedSessionHandle<LocalProto,
                                                    PermSet<Role>,
                                                    SharedChannel&>{ch};
            std::move(b)(std::move(handle));
        }...
    );
}
```

Notes on D2 / D5 (session_fork in v1):

- Works for binary sessions and plain-mergeable multiparty (FanOut, FanIn, RequestResponse, SwimProbe). Any G whose `project_t<G, Role>` for non-sender/non-receiver roles converges under plain merge.
- Diverging multiparty (Raft, 2PC-with-multi-followers, MoE all-to-all) produces a `static_assert` failure from the underlying `project_t` — this is `SessionGlobal.h`'s plain-merge-only limitation, not FOUND-C's. Diagnostic message names the divergent branch and points at Task #381 for the full coinductive merging implementation.
- After all bodies join via `mint_permission_fork`'s RAII array-of-jthreads, the parent permission is rebuilt via `mint_permission_combine_n` and returned to the caller for the next session.

### 9.3 Test coverage

- `mint_permissioned_session` round-trip: mint Whole, split, establish, send/recv loop, close, recombine.
- `session_fork` for FanOut: 3 producer roles, 1 consumer role, plain-mergeable protocol.
- `session_fork` static_assert failure: Raft's diverging projection — verify the diagnostic names the divergence.

---

## 10. Spec — Crash transport wiring (~150 lines, in Header C)

### 10.1 The pattern (per 24_04 §11)

Each `PermissionedSessionHandle` that crosses a process or node boundary takes a `safety::OneShotFlag&` (or wrapper thereof) per peer. Producers — CNTP completion-error handlers, SWIM confirmed-dead handlers, kernel socket-close — call `flag.signal()` exactly once on detected peer death.

### 10.2 Integration with existing `CrashWatchedHandle`

The pattern is already implemented in `bridges/CrashTransport.h::CrashWatchedHandle<Proto, Resource, PeerTag, LoopCtx>`. FOUND-C composes by allowing `PermissionedSessionHandle` to wrap a `CrashWatchedHandle` as its inner Resource:

```cpp
// User pattern:
auto crash_flag = OneShotFlag{};
auto resource_with_crash = CrashWatchedResource{my_channel, crash_flag};
auto handle = mint_permissioned_session<MyProto>(
    resource_with_crash,
    std::move(my_perm));

// On send/recv, the inner transport peeks the flag.  On signal:
auto result = std::move(handle).send(msg, transport);
if (!result) {
    auto crash_event = result.error();  // CrashEvent<PeerTag>
    // Drop PS — permissions are NOT recovered from a crashed peer.
    // Return resource via crash_event.recovered_resource for cleanup.
}
```

### 10.3 Crash-mid-protocol policy (per 24_04 §11.3)

When a `PermissionedSessionHandle` transitions to Stop via crash detection:
- PS is dropped (never returned to the parent scope).
- Any OwnedRegions referenced by the protocol are arena-bulk-freed.
- The invariant "no permission outlives its session" holds.
- Cost: a crash wastes one iteration's worth of permissions and arena memory.

This is the right tradeoff for crash-stop; finer recovery requires CheckpointedSession (separate).

### 10.4 Test coverage

- Producer crashes mid-Send: verify PermissionedSessionHandle's send returns `unexpected(CrashEvent)`, PS is dropped, resource is recoverable.
- Consumer never receives: producer's send completes (transport buffers); consumer's recv times out via OneShotFlag from Layer-2 SWIM; PS evolution short-circuits to Stop.

---

## 11. Spec — `is_permission_balanced_v` standalone (~50 lines)

Per Decision D6, ships standalone. Walks the protocol's reachable LTS and verifies permission flow balance.

```cpp
namespace crucible::safety::proto {

// For a given Γ (typing context) and InitialPerms (initial permission
// distribution), verify that at every reachable LTS state the union of
// active permission sets across all participants matches InitialPerms
// (modulo mint_permission_split/combine at fork points).
//
// v1: compile-time bounded BFS, fuel = 1024 states.  Adequate for
// Crucible's bounded protocols.
//
// Does NOT compose with is_safe_v (Task #346, unshipped).  The
// is_csl_safe_v conjunction is documented in session_types.md III.L9
// as future work; v1 ships permission balance independently.
template <typename Gamma, typename InitialPerms>
inline constexpr bool is_permission_balanced_v = /* metafunction */;

}  // namespace crucible::safety::proto
```

Test: a Γ for the TraceRing producer/consumer roles with `InitialPerms = PermSet<TraceRingProducer, TraceRingConsumer>` returns true; a deliberately-broken Γ where one role drains permissions without surrender returns false with diagnostic.

---

## 12. Spec — Negative-compile harness (~50 lines)

`test/sessions_neg/permission_imbalance.cpp` — ten failing-by-design fixtures, each in its own static_assert block guarded by `#ifdef CRUCIBLE_NEG_COMPILE_TEST_<NAME>`. CMake's `WILL_FAIL=TRUE` matrix runs each fixture and pattern-matches the compiler diagnostic.

Fixtures:

1. Send `Transferable<T, X>` from a handle whose PS does not contain X — diagnostic must contain `[PermissionImbalance]`.
2. Receive a `Transferable<T, X>` and reach End without surrendering — diagnostic `[PermissionImbalance]` at `close()`.
3. Send `Returned<T, X>` from a handle whose PS does not contain X — diagnostic.
4. Loop body that drains a permission per iteration — diagnostic `[PermissionImbalance]` at Continue with explicit "Loop iteration permission flow must balance" text.
5. Select with two branches that produce divergent terminal PermSets — diagnostic `[PermissionImbalance]` at the Select handle constructor.
6. Establish a session with init perms that conflict (same Tag twice) — diagnostic from `perm_set_union`'s static_assert.
7. session_fork on a Raft-shaped protocol — diagnostic from `project_t`'s plain-merge-only limitation, suggesting Task #381.
8. Bare `permission_drop` on a tag held by an active PermissionedSessionHandle — diagnostic from `Permission`'s consume-once discipline.
9. Two PermissionedSessionHandles for the same Whole tag established simultaneously — diagnostic from `mint_permission_root`'s once-per-program convention (review-discoverable, not compile-enforced; this fixture is documentation-only).
10. `PermissionedSessionHandle<End, PermSet<X>, R, L>::close()` — diagnostic `[PermissionImbalance]` from the close static_assert.

---

## 13. Spec — Bench harness (~30 lines)

`bench/bench_permissioned_session_handle.cpp` — three checks:

```cpp
// 1. sizeof equality.
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, int>) ==
              sizeof(SessionHandle<End, int>));
static_assert(sizeof(PermissionedSessionHandle<Send<int, End>,
                                                PermSet<MyTag>,
                                                Channel*>) ==
              sizeof(SessionHandle<Send<int, End>, Channel*>));

// 2. Machine-code parity via objdump diff snapshot.
auto run_bare = bench::Run("bare_session_handle.send")
    .hardening(rt::Policy::production())
    .samples(10'000'000)
    .measure([&]{
        auto h2 = std::move(handle).send(msg, transport);
        handle = std::move(h2);
    });

auto run_perm = bench::Run("permissioned_session_handle.send")
    .hardening(rt::Policy::production())
    .samples(10'000'000)
    .measure([&]{
        auto h2 = std::move(perm_handle).send(msg, transport);
        perm_handle = std::move(h2);
    });

run_perm.assert_no_regression_vs(run_bare, within_percent=1);

// 3. Compile-time cost regression bench.
// Track template-instantiation count via -ftime-report.
// Target: PermissionedSessionHandle adds < 10% template instantiations
// vs bare SessionHandle for an equivalent protocol shape.
```

---

## 14. Phase-by-phase implementation order

Each phase is one commit (or a tight sequence). All 210 existing tests must remain green between commits.

**Phase 1 — `permissions/PermSet.h` (~120 lines + ~50 lines of in-header smoke test).**
- New header.
- All metafunctions + concept gates.
- `runtime_smoke_test()` per the discipline.
- Sentinel TU `test/test_permset_compile.cpp` to force the smoke test into a TU under project warning flags.

**Phase 1.5 — `permissions/Permission.h` extensions (~50 lines).**
- Add `mint_permission_combine_n<Parent, Children…>`.
- Add `concept Permission` and `concept SharedPermissionFor<T, Tag>`.
- Bundled because they all touch one file; no separate commit needed.

**Phase 2 — `sessions/SessionPermPayloads.h` (~100 lines + ~80 lines of in-header smoke test).**
- Three markers + traits + concepts.
- `runtime_smoke_test()` exercising every marker shape and PS-evolution rule.
- Sentinel TU.

**Phase 3 — `sessions/PermissionedSession.h` core (~700 lines).**
- All five protocol-head specialisations.
- `step_to_next_permissioned` with Loop/Continue resolution + balance enforcement.
- All branch convergence enforcement.
- Debug-mode abandonment-tracker enrichment.
- `mint_permissioned_session` factory.
- Stub `session_fork` returning `static_assert(false, "Phase 4")` until Phase 4 lands; lets the Phase 3 commit compile cleanly without depending on `SessionGlobal.h::project_t`.

**Phase 4 — `session_fork` implementation (~150 lines, same file).**
- Replace Phase 3's stub with the full session_fork implementation.
- Per Decision D2: works for plain-mergeable protocols; diverging multiparty produces `static_assert` from `project_t`.
- Test fixtures for FanOut, FanIn, SwimProbe (plain-mergeable).
- Test fixture for Raft (diverging — verify diagnostic naming).

**Phase 5 — Crash transport (~150 lines, same file).**
- `OneShotFlag::peek()` integration into Send/Recv methods.
- Compose with existing `CrashWatchedHandle` pattern.
- PS-drop-on-crash policy.
- Test fixtures: producer crash, consumer crash, network partition.

**Phase 6 — `is_permission_balanced_v` standalone (~50 lines).**
- Bounded BFS over Γ's LTS, fuel = 1024.
- Sentinel test `test/test_permission_balanced.cpp` exercises balanced + unbalanced Γ examples.

**Phase 7 — Negative-compile harness (~50 lines tests).**
- `test/sessions_neg/permission_imbalance.cpp` with ten fixtures.
- CMake `WILL_FAIL=TRUE` matrix entries.

**Phase 8 — Bench (~30 lines).**
- `bench/bench_permissioned_session_handle.cpp` with sizeof + machine-code-parity + compile-time-cost checks.

**Phase 9 — Doc-update sweep.**
- Rewrite `session_types.md` Part III.L9 (~50 lines).
- Update `session_types.md` Part VII M9 status (~5 lines).
- Update `session_types.md` Part IX.1 + IX.2 + IX.3 (~10 lines).
- Update `24_04_2026_safety_integration.md` §5 with SHIPPED-AS marker (~15 lines).
- Update `25_04_2026.md` §2.3 + §2.5 corrections (~10 lines).
- Update `27_04_2026.md` §5.2 status to ✅ SHIPPED (~5 lines).
- Update `THREADING.md` §17.13 clarification (~5 lines).
- Update `CLAUDE.md` L0 path references + wrapper catalog (~15 lines).
- Refresh three in-tree doc comments: `Sessions.h:30`, `SessionContext.h:19`, `SessionPayloadSubsort.h:90-99` (~10 lines total).
- All bundled in one commit at end of FOUND-C v1 series.

**Total commits:** 9 (one per phase). Each is reviewable independently. Phase 1+2 are pure type-system primitives (low risk). Phase 3+4 are the load-bearing work. Phase 5+6 add runtime behaviour. Phase 7+8 are tests/bench. Phase 9 is doc-only.

---

## 15. Architectural risk register (decisions made)

Per Decisions D1-D7 + Risks R1-R5 from §0.4 and the Agent 3 triangulation:

- **R1 — Loop balance:** Decision D3, **enforce**. `static_assert(perm_set_equal_v<PS, LoopEntryPS>)` at every Continue. Diagnostic message guides users to surrender or rebalance.
- **R2 — Branch convergence:** Decision D4, **enforce**. `static_assert(all_branches_converge_v<Bs..., PS>)` at Select/Offer constructors. Per-branch divergence is a future research extension.
- **R3 — Abandonment + permission leak:** Decision D5, **enrich**. Debug-mode tracker prints leaked tags. Zero release-mode cost.
- **R4 — `is_csl_safe_v` over-claim:** Decision D6, **ship `is_permission_balanced_v` standalone**. Conjunction with `is_safe_v` documented as future. Per Part IX.5 honest-assessment discipline.
- **R5 — `session_fork` blocked by plain merging:** Decision D2, **ship in v1**. Works for plain-mergeable protocols. Diverging multiparty produces clean diagnostic from `project_t` until Task #381 lands.

**New risks introduced by FOUND-C v1 (mitigated):**

- **R6 — PermSet template instantiation cost.** Deeply nested protocols with large PermSets force the compiler to compute unions/differences at each step. Mitigation: every metafunction is fold-expression-based (linear), `define_static_array` memoizes canonical PermSet forms for hash equality, and the bench harness includes per-channel template-instantiation timing tests. Empirically (per 24_04 §5.4 estimate), protocols of depth ≤ 32 and PermSet size ≤ 16 stay under 30 ms per instantiation.
- **R7 — Negative-compile diagnostic quality.** Routing every permission-flow violation through `CRUCIBLE_SESSION_ASSERT_CLASSIFIED` should produce 3-line diagnostics, but template instantiation context can blow them up. Mitigation: every PermissionedSessionHandle method has an explicit `requires` clause that fires before the inner permission-flow logic, and the `PermissionImbalance` diagnostic from `SessionDiagnostic.h:207` names the failing pair.
- **R8 — `session_fork` projection failures look obscure.** Diverging multiparty protocols fail at `project_t`, not at `session_fork`. Mitigation: wrap `project_t` invocations in a layer that catches the failure and re-emits via `[ProjectionDivergence]` diagnostic naming the divergent branch and Task #381.

---

## 16. Verification & test plan

### 16.1 Per-phase test gates

- **Phase 1 (PermSet):** `test/test_permset_compile.cpp` exercises every metafunction; runtime smoke test passes; project warning flags clean.
- **Phase 2 (Payload markers):** `test/test_session_perm_payloads.cpp` exercises every PS-evolution rule; runtime smoke test passes.
- **Phase 3 (Handle core):** `test/test_permissioned_session_handle.cpp` round-trips every protocol head; sizeof equality static_asserts pass; debug abandonment diagnostic fires correctly with leaked tags.
- **Phase 4 (session_fork):** Tests for FanOut, FanIn, SwimProbe (plain-mergeable) pass; Raft test verifies diagnostic naming.
- **Phase 5 (Crash transport):** Tests for producer crash, consumer crash, network partition pass.
- **Phase 6 (is_permission_balanced_v):** Tests for balanced and unbalanced Γ examples pass.
- **Phase 7 (neg-compile):** Ten fixtures all fail to compile with the expected diagnostic substring.
- **Phase 8 (bench):** Sizeof equality + within-1% machine-code parity + within-10% template-instantiation count.

### 16.2 Cross-phase regression gate

Between every commit: `ctest -j8` returns 100% pass on the existing 210 tests. No regression.

### 16.3 Sanitizer gates

- `tsan` preset: PermissionedSessionHandle's permission flow is type-level only (no runtime atomics on PS), so TSan has nothing to catch beyond the underlying transport's existing state. Verify clean.
- `asan` + `ubsan`: standard project gates. Verify clean.

### 16.4 Doc verification

- `grep -rn "PermissionedSession" misc/` after Phase 9 should show NO references to the old paths/namespaces (`safety/PermissionedSession.h`, `crucible::safety::session::`, `PermissionSet`).
- `git log --oneline include/crucible/sessions/PermissionedSession.h` shows clean shipping history.
- `session_types.md` Part IX.1 no longer lists L9 as unshipped.

---

## 17. v2 deferred items (with promotion criteria)

Items deliberately deferred from v1 with explicit criteria for promotion:

- **`csl_compose(h1, h2)`** — session_types.md III.L9 invents this primitive; no other doc spec, no FOUND-C task. Defer to v2. **Promotion criterion:** an actual production caller demonstrates need.
- **Per-branch PS divergence with join op (PS₁ ∩ PS₂)** — Risk R2 v1 ships single-PS-convergence. **Promotion criterion:** a Crucible protocol genuinely requires divergent branch flows AND a researcher publishes the linear-typing-of-branching paper (active research area).
- **Cross-process PermissionedSessionHandle** — open question §3 of Appendix C in 24_04 doc. Cross-process bytes never carry permissions directly; receiving process mints fresh permissions locally. **Promotion criterion:** CNTP Layer 1 typed-session migration (SEPLOG-K4, Task #358) starts.
- **`is_csl_safe_v = is_safe_v ∧ is_permission_balanced_v` conjunction** — Risk R4 v1 ships permission balance standalone. **Promotion criterion:** L7 SessionSafety.h (Task #346) lands.
- **Lean mechanisation of permission-flow correctness** — Task #419 SAFEINT-Lean-1. **Promotion criterion:** C++ implementation stable for one quarter; no API churn.
- **Diverging multiparty `session_fork`** — Risk R5 v1 ships plain-mergeable only. **Promotion criterion:** Task #381 (full coinductive merging) lands.
- **Effects-row composition with PermSet** — FOUND-B's Met(X) effect rows + FOUND-C's PermSet are independent in v1. **Promotion criterion:** signature dispatcher (FOUND-D) needs both for parameter shape recognition.
- **Cipher integration (federation-shareable proof certificates for permission-balance proofs)** — out of FOUND-C scope. **Promotion criterion:** Cipher certificate machinery (Task #352) lands.

---

## 18. Task ID cross-reference

| Task | What | Phase | Lines |
|------|------|-------|-------|
| #605 FOUND-C01 | PermSet<Tags...> type-list + size traits | Phase 1 | ~30 |
| #606 FOUND-C02 | PermSet operations (contains/insert/remove/union/diff) | Phase 1 | ~70 |
| #607 FOUND-C03 | Transferable<T, Tag> payload marker | Phase 2 | ~30 |
| #608 FOUND-C04 | Borrowed<T, Tag> payload marker | Phase 2 | ~30 |
| #609 FOUND-C05 | Returned<T, Tag> payload marker | Phase 2 | ~30 |
| #610 FOUND-C06 | PermissionedSessionHandle template core | Phase 3 | ~250 |
| #611 FOUND-C07 | Send method with PermSet evolution per payload | Phase 3 | ~100 |
| #612 FOUND-C08 | Recv method with PermSet evolution per payload | Phase 3 | ~100 |
| #613 FOUND-C09 | Select/Offer/branch methods with PermSet | Phase 3 | ~150 |
| #614 FOUND-C10 | close() with permission surrender | Phase 3 | ~30 |
| #615 FOUND-C11 | mint_permissioned_session factory | Phase 3 | ~30 |
| #616 FOUND-C12 | session_fork multi-party establishment | Phase 4 | ~150 |
| #617 FOUND-C13 | OneShotFlag-driven crash transport for sessions | Phase 5 | ~150 |
| #618 FOUND-C14 | Negative-compile tests for permission imbalance | Phase 7 | ~50 |
| #619 FOUND-C15 | Bench: PermissionedSessionHandle vs bare SessionHandle | Phase 8 | ~30 |
| (new) | PermSet.h sentinel TU + smoke test | Phase 1 | ~50 |
| (new) | SessionPermPayloads.h sentinel TU + smoke test | Phase 2 | ~80 |
| (new) | mint_permission_combine_n in Permission.h | Phase 1.5 | ~20 |
| (new) | concept Permission + concept SharedPermissionFor | Phase 1.5 | ~10 |
| (new) | is_permission_balanced_v standalone | Phase 6 | ~50 |
| (new) | Doc updates per §3 | Phase 9 | ~120 (across docs) |

**Total:** 15 FOUND-C tasks + 6 supporting items + 1 doc sweep. ~1,400 LOC total (~1,100 code + ~200 tests + ~30 bench + ~120 doc).

---

## 19. Summary table — what changes for whom

| Audience | What to read | What to do |
|----------|--------------|------------|
| Anyone implementing FOUND-C tasks | This document end-to-end | Follow §14 phase order; cite §3 for doc updates |
| Anyone reviewing FOUND-C PR | §3, §6-§13, §15, §16 | Verify spec matches code; verify doc-updates bundled |
| Anyone updating predecessor docs | §3 per-doc breakdown | Apply listed edits; remove staleness |
| Anyone depending on SessionHandle | §2, §5, §15 | Nothing breaks — SessionHandle untouched; PermissionedSessionHandle is opt-in |
| Anyone designing future Permissioned* primitives | §2.4, §15 | SESSIONS-AUDIT held — sessions remain structural; future primitives follow same discipline |
| Anyone wiring production callers (K-series) | §10, §17 | Use mint_permissioned_session for binary; session_fork for plain-mergeable multiparty; defer diverging until Task #381 |

---

## 20. Closing

FOUND-C ships the framework's central thesis — that CSL permissions and session protocols compose into one type-checked correctness substrate — as ~1,100 lines of new code in three new headers, with zero refactor of the existing sessions/ tree (SESSIONS-AUDIT held), with the four predecessor docs updated bundled in the same PR (no doc rot allowed), with five architectural risks resolved by explicit decisions (D1-D7), and with the remaining unshipped session-type machinery (L7 φ predicates, async ⩽_a, full coinductive merging) cleanly factored into separate tracking tasks.

After FOUND-C lands, the framework is no longer a museum piece — it has its first compile-time integration test exercising the whole stack from `Permission<Tag>` through `PermSet<Tags...>` through `Transferable<T, Tag>` through `PermissionedSessionHandle<Proto, PS, Resource>` through `is_permission_balanced_v`. Production callers (K-series tasks #355-#358) become the next priority, but each can be wired independently using the same primitives that v1 ships.

The next PR after FOUND-C: pick one production channel (TraceRing per Task #384 SEPLOG-INT-1 is the simplest), wire it as `PermissionedSpscChannel<TraceEntry, N, TraceRingTag>` composed with `PermissionedSessionHandle<TraceRingProto, PermSet<TraceRingProducer | TraceRingConsumer>, TraceRingChannel&>`, and demonstrate that the framework's "zero overhead in production" claim survives a real workload bench. That is the moment Part IX.2 of `session_types.md` finally has a non-zero answer to "production callers using the framework."

**✅ SHIPPED as `sessions/SpscSession.h` in commit c2ceb86 (2026-04-27)**, refined in follow-up SPSC-FIX-1..4 commits. The wiring is generic across any `PermissionedSpscChannel<T, N, Tag>` (works for TraceRing, MetaLog, CNTP, bg-pipeline drain — wire once, every SPSC channel benefits) rather than TraceRing-specific. Three deliverables: framework header (`sessions/SpscSession.h`), integration test (`test/test_spsc_session.cpp` — round-trip 1024 ints between two jthreads), bench (`bench/bench_spsc_session.cpp` — 2×2 bare/typed × producer/consumer matrix with pair-compare deltas, two-tier evidence). Honest scope: the wiring uses EmptyPermSet by design (TraceRing-shape SPSC channels don't transfer permissions through the wire); PermSet evolution path is exercised by FOUND-C's own integration test, not duplicated here. Framework now has its first wired-in production-shape exercise; production CALLER switchover (Vigil/BackgroundThread/MerkleDag/vessel_api consuming the typed view) is the still-pending next phase. Asm-identical witness verifiable via `cmake -DCRUCIBLE_DUMP_ASM=ON` (added in SPSC-FIX-2 — generic per-target asm dump capability that any future asm-comparison work can reuse).

---

*End of document. To be updated as FOUND-C phases land. Discrepancies between this document and observed reality are bugs in the document; the next maintainer of `sessions/PermissionedSession.h` should treat this text as design-of-record subject to review and revision. After FOUND-C v1 ships, this document should be promoted to a "shipped-as" reference rather than a forward-looking plan.*

*Authors: Crucible team. Voice: direct, opinionated, paper-citing. Length: ~1,000 lines (target met; revise if overflow). Replaces (in scope of FOUND-C): `session_types.md` Part III.L9, `24_04_2026_safety_integration.md` §5, `27_04_2026.md` §5.2 — when those four docs disagree on FOUND-C, this document wins.*
