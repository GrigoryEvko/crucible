# Typed Conversations in Crucible

*Design reference for session types, typing contexts, parametric safety, and their application to every communication channel in the Crucible runtime. Written in the voice of CLAUDE.md and CRUCIBLE.md: direct, opinionated, dense. Read alongside those two documents plus THREADING.md (concurrency primitives), FORGE.md (compiler), MIMIC.md (per-vendor backend). Nothing here contradicts them; this document fills the gap between "we have a runtime" and "we have a type system strong enough to prove the runtime correct end-to-end."*

---

## Contents

1. Thesis and reading guide
2. Why — typed conversations as Crucible's correctness spine
3. Part II — The theory, one coherent picture
4. Part III — The C++26 realization, layer by layer
5. Part IV — Every Crucible communication channel as a typed conversation
6. Part V — The MPMC ring as the flagship
7. Part VI — Per-channel φ-level reference table
8. Part VII — Execution plan (twelve milestones)
9. Part VIII — Open questions
10. Appendix A — Paper attributions (eight references, one theorem each)
11. Appendix B — Worked example: RequestResponse from type to generated code
12. Appendix C — Glossary
13. Appendix D — Frontier inventory (what the literature has not solved for us)

---

## 1. Thesis

**Every communication in Crucible is a *typed conversation*: a protocol-typed channel between named participants, checked at compile time against a chosen safety property φ.** Violating the protocol is a compile error, not a runtime fault. Two Keepers running on different hardware (H100, MI300X, v5p, trn2) satisfying the same session types produce bisimilar execution traces; under BITEXACT recipes that bisimilarity is byte-identity. Replay determinism (CRUCIBLE.md §10), the load-bearing invariant of the whole runtime, is a session-type theorem: if per-vendor realizations preserve the protocol, the traces coincide.

This is not a decoration on top of an existing system. It is the structure that lets fifty-plus independent communication channels — TraceRing dispatch, bg-thread pipeline, KernelCache publication, ExecutionPlan submission, ChainEdge semaphores, Cipher three-tier, CNTP's five layers, SWIM gossip, Raft consensus, Canopy membership, DataNode prefetch, InferenceSession prefill/decode, PagedKVCache sharing, FLR recovery, speculative decoding, bucketed async all-reduce, ring attention, expert-parallel routing, pipeline 1F1B scheduling, Keeper startup/shutdown, live binary rolling upgrade — each with its own correctness requirements — compose into one system the compiler can check. Take away the typed conversations and the channels are independent proof obligations, verified pairwise with quadratic effort. Keep them and the whole Crucible runtime becomes one well-typed term.

## 2. Reading guide

This document is structured so the parts can be read in isolation.

- **Read Part I** if you're deciding whether typed conversations are worth the effort. Motivation, concrete benefits, honest costs, where the literature is still catching up.
- **Read Part II** if you want the theory. Five axes (sync/async, binary/multiparty, top-down/bottom-up, reliable/crash-stop, decidable/undecidable), one unification (parametric safety φ), eight papers as attribution rather than structure. No previous session-type exposure assumed.
- **Read Part III** if you want the C++26 mapping. Twelve layers, twelve files under `include/crucible/safety/`, per-layer signatures with rationale. GCC 16 features (contracts, reflection, expansion statements, constexpr exceptions) used deliberately.
- **Read Part IV** if you want concrete: every Crucible communication channel as a typed conversation, with global type, per-role local types, queue types, reliability assumptions, chosen φ, and a pointer into CRUCIBLE.md or THREADING.md for the runtime implementation.
- **Read Part V** if you want the flagship: the MPMC ring (Nikolaev SCQ) as the first industrial typed conversation in the Crucible stack, combining async queue types, precise async subtyping, dynamic membership, and CSL permission balance. Every theoretical layer pays off here.
- **Read Part VI** for the φ-level reference table across all fifty channels. This is the document's operational summary.
- **Read Parts VII–VIII** for the execution plan and open questions.
- **Read Appendices A–D** for attribution, a fully worked example, the glossary, and a frontier inventory of things Crucible needs that the session-type literature has not cleanly answered.

Pre-existing mental model: familiar with CLAUDE.md's seventeen layers, familiar with CRUCIBLE.md's runtime services (Vigil, Vessel, Keeper, Canopy, Cipher, CNTP). Familiarity with classical session types is nice but not required; the theory chapter is self-contained. Familiarity with Concurrent Separation Logic (CSL) helps understand the CSL × session integration in III.L9 but the permission machinery is explained in THREADING.md §5.

## 3. Where this fits in the doc set

| Document | Scope | Relationship to this one |
|---|---|---|
| `CLAUDE.md` | 17-layer runtime overview, the mental model | This document refines L4 Operations + L6 Graphs + L13 Distribution + L15 Meridian+Augur from the session-type angle |
| `CRUCIBLE.md` | Runtime reference: mock-tensor dispatch, Vessel, CNTP, Cipher, Canopy, lifecycle | Part IV of this document maps every communication channel in CRUCIBLE.md to its session type |
| `THREADING.md` | Concurrency primitives, CSL permissions, MpmcRing, ChaseLevDeque, Beyond-Vyukov | This document's L9 layer (Permissioned session × CSL) builds on THREADING.md §5 and §17 |
| `FORGE.md` | Vendor-agnostic compiler, IR001→IR002→IR003*, Phase A–L | The session types on collectives and ExecutionPlan submission constrain Forge's Phase J output |
| `MIMIC.md` | Per-vendor backend framework, CKernel taxonomy | Mimic's `rt::submit_plan` and `comm::all_reduce` entry points sit on the types defined here |

When this document and another disagree on a detail, the other wins. This document's job is to NAME and PROVE protocol properties, not to dictate implementation.

---

# Part I — Why

Session types are not a decoration. They are the mechanism that makes Crucible's other invariants compose.

## I.1 What Crucible communicates

A Crucible training or inference run communicates across roughly fifty channels simultaneously. Exhaustive enumeration, grouped by scope and cadence:

**Intra-process, hot path (< 100 ns per message)**:
TraceRing (Vessel → bg thread, SPSC, ~5 ns/op). MetaLog (Vessel → bg thread, parallel SPSC for TensorMeta). bg-thread internal pipeline stages: drain → build TraceGraph → transform → compile. KernelCache publication (bg compile worker → any Keeper reader, SWMR via atomic pointer swap). MemoryPlan publication (bg planner → any execution consumer, SWMR snapshot). AtomicSnapshot broadcast (1 writer + N readers, seqlock; used for Augur metrics, Meridian calibration data). ExecutionPlan submission (CPU host → GPU doorbell via pre-composed pushbuffer, ~120–200 ns CPU critical path per §14.7). PatchPoint writes (host → GPU pushbuffer WC mapping, ~10 ns per scalar). ChainEdge semaphores (GPU → GPU via pinned sysmem, ~400 ns cross-PCIe signal). Vigil mode transitions (RECORD / REPLAY / SERVING / FLUSH, state-machine type). Transaction begin/commit/abort (task #101). MpmcRing producer / consumer (THREADING.md §17, Nikolaev SCQ, ~15–25 ns uncontended, ~30–60 ns at 16-way contention).

**Inter-process, same host**:
Cipher hot-tier reads (peer-memory RDMA read when entry is warm on another Keeper, ~1 μs). Cipher warm-tier writes (local NVMe via io_uring SQPOLL, ~20 μs acknowledged). DataNode CPU-worker → GPU-worker batch delivery (RDMA put into pre-planned device slot). Self-update binary distribution (Cipher cold-tier gossip + rolling replace).

**Inter-node, CNTP's five layers**:
Layer 1 Transport: `send_eager` (≤ MTU eager sends, ~1–5 μs RDMA / ~100 ns NVLink), `write_rdma` (zero-copy RDMA write, wire-bound), `read_rdma` (zero-copy RDMA read), `atomic_faa` (one-sided fetch-and-add), `register_region` (pre-registered MR).
Layer 2 Gossip: SWIM probe (every 1 s, K=3 random peers, ~64 B per probe, XDP fast path, ~80 ns both sides).
Layer 3 Consensus: Raft AppendEntries, RequestVote, leadership transfer (used only for topology commits, Cipher promotions, recipe registry updates; data-plane does not touch Raft).
Layer 4 Collectives: `all_reduce_sum`, `all_gather`, `reduce_scatter`, `all_to_all`, `broadcast`, `send`/`recv` p2p. Algorithm pinned per recipe (binary tree sorted by UUID under BITEXACT, ring/halving-doubling under ORDERED).
Layer 5 NetworkOffload: SHARP / ICI / XGMI / SwarmX in-fabric reductions, capability-queried, capability-tolerant.

**Workload-specific distributed**:
Bucketed async all-reduce (FSDP-style, 25-50 MB buckets, 70-90% compute overlap). Ring attention K/V rotations (context parallelism). Pipeline parallelism micro-batch exchanges (1F1B / interleaved / zero-bubble DeepSeek V3). Expert-parallel MoE two-step all-to-all (dispatch + combine). Global gradient-norm scalar reduce + broadcast (pinned for BITEXACT). EMA / SWA weight sync (optional, periodic). Checkpoint manifest Raft commit. Weight-shard redistribution on fleet reshard. Cursor-state handoff when a CPU Relay departs.

**User-facing**:
InferenceSession prefill (token batch → batch logits, 50–500 ms). InferenceSession decode (single-Q against KV cache, 5–20 ms/token, continuous batching). PagedKVCache page allocation with cross-session prefix sharing (content-addressed). Beam-search session fork (`session.fork()`). Speculative decoding draft model → target model verify (parallel N-token verify, rollback on reject).

**Lifecycle + fault tolerance**:
Canopy member join (hardware probe → gossip discovery → Raft admission → Meridian probe → READY). Canopy member leave (announce → finish current step → handoff state → exit). Fleet reshard on membership change (Raft epoch bump → Phase K re-run → weight redistribution). FLR recovery (failure detection → FLR → GSP re-upload from cached firmware → replay from last checkpoint). Live binary rolling upgrade (Cipher cold-tier gossip of new hash → half-at-a-time shutdown + reload → Raft verifies → other half).

**Control plane**:
k8s operator watch (`CrucibleCluster` CRD → DaemonSet / ConfigMap reconciliation). SLURM environment discovery (`srun` + `SLURM_NODELIST` seed). Prometheus scrape endpoint (Augur metrics aggregation). Vessel FFI (frontend adapter → C ABI → runtime).

Fifty distinct patterns. Each with its own participants, ordering, crash model, fairness assumption, and liveness requirement. Each must be correct individually; all must compose.

## I.2 What typed conversations give us

Seven concrete benefits, in order of payoff.

**(1) Compile-time protocol checking.** A producer sending the wrong message type at the wrong protocol position is a compile error, not a runtime fault. A consumer calling `try_pop` after the channel has been closed via session's `End` is a compile error. A producer attempting `try_push` after the protocol has transitioned to `End` is a compile error. Run-time protocol validation (label checks, state checks, endpoint-alive probes before each send) is replaced by structure.

**(2) Compositionality.** Each channel is verified independently, then the whole system's correctness follows from per-channel correctness plus a per-pair interaction check. This scales to fifty channels; without compositionality, verifying fifty channels requires reasoning about 2⁵⁰ interaction possibilities. With it, the effort is O(channels) + O(adjacent-channel pairs), closer to O(N²) worst case but usually O(N) in practice because most channels are independent.

**(3) Protocol evolution via subtyping.** Version 2 of the Cipher replication protocol can be a subtype of version 1 (adds more branches in the v1's `Offer`, tightens v1's `Select` to fewer labels). A v1 peer keeps working when a v2 peer replaces it, during a rolling upgrade, without breakage. Under precise async subtyping (GPPSY23), this permits aggressive refactoring of the dependency order of messages (anticipate sends past unrelated recvs) while preserving behavioral safety.

**(4) Crash-handling as type structure.** Under BSYZ22 / BHYZ23, every communication with an unreliable peer MUST include a crash-handling branch. Omitting it is a compile error. Recovery behavior is not "a thing the runtime does behind the scenes" — it is part of the protocol signature. FLR recovery, Canopy member death, CPU Relay eviction: each terminates a surviving peer's session at the `crash` branch, not at an unhandled runtime exception.

**(5) Cross-language boundary soundness.** PyTorch-Vessel, JAX-Vessel, native Python/C++/Rust frontends all produce IR001 ops. If each Vessel's outgoing protocol is a subtype of Vigil's incoming protocol, the FFI boundary is sound by construction; no runtime marshaling guard is needed between frontend and runtime.

**(6) Replay determinism as type theorem.** Two backends (Mimic-NV, Mimic-AM, Mimic-TPU, Mimic-TRN, Mimic-CPU) realizing the same session types produce bisimilar traces. Under BITEXACT_TC / BITEXACT_STRICT recipes with pinned reduction topology (canonical binary tree by UUID, §10.6 of CRUCIBLE.md), bisimilar traces are byte-identical. The `bit_exact_cross_backend` CI test (§10.8) is not a behavioral test of the implementation; it is a verification that session-type preservation holds across backends. If a backend changes a session type projection (adds a transient state, reorders a message), the CI catches it before production.

**(7) Verification tooling.** A typed conversation's LTS is finite-state by construction (our global types are finitely branching; recursion is guarded; participant count is bounded). Modal μ-calculus properties are decidable on finite-state LTSs in PSPACE. Tools like mCRL2 (used by mpstk-crash-stop, BSYZ22's verifier) can discharge safety properties offline, emit a proof certificate, and have that certificate verified by a simple checker. Crucible's verify preset (Z3-based, see `code_guide.md` §I) can incorporate session-type properties into the same proof infrastructure as the allocator proofs.

## I.3 The cost

The cost is four-fold.

**Learning a small vocabulary.** Send / Recv / Select / Offer / Loop / Continue / End. Dual. Compose. Projection. Subtyping. Typing context Γ. Parametric safety φ. Queue types σ. Association. Crash-stop. Balanced+. This is fewer concepts than `std::atomic`'s memory ordering taxonomy, and they compose cleanly.

**Structuring code so protocols are visible.** Instead of "send the message and hope" we write `handle = handle.send(msg).await_response()` with per-step return types. This is how Rust's `async/await` already forces us to write code; session types extend the discipline to cross-thread and cross-node communication.

**Template metaprogramming density.** The type-level machinery for projection, subtyping, context reduction, φ-checking is heavy. We isolate it in `include/crucible/safety/Session*.h` and pay the compile-time cost only where a session-typed handle is instantiated. Release builds see zero runtime overhead; debug builds' extra compile-time is absorbed into the same build cycle as contract and reflection checks.

**A decidability wall.** Precise async subtyping ⩽_a is undecidable in general (Bravetti-Carbone-Zavattaro 2018; Lange-Yoshida 2017). Crucible uses a decidable approximation: bounded-depth SISO refinement (à la Rumpsteak, Cutner-Yoshida-Vassor 2022). When the approximation cannot prove a subtype relation within the depth bound, we reject conservatively. In exchange: every subtype we accept is genuinely sound. A rejected query can be resolved by the engineer widening the depth bound or restructuring the protocol; we have not observed a real Crucible protocol that needs depth > 32, and the default bound is 64.

In exchange for these four costs: every protocol bug becomes a compile error; every refactor preserves protocol correctness mechanically; every new communication channel plugs into the verification infrastructure on day one; cross-backend replay determinism is guaranteed by construction rather than probed by CI after the fact.

The trade is asymmetric in our favor because Crucible already pays the C++26-plus-GCC-16 cost for the broader safety infrastructure (code_guide.md §I). Session types are an incremental layer on top of machinery we already built: `Linear<T>` for move-only resources, `Refined<Pred, T>` for invariants, `Tagged<T, Source>` for provenance, `Pinned<T>` for identity, `ScopedView<T, State>` for typestate, `Permission<Tag>` for CSL ownership. Session types generalize the `ScopedView` pattern from per-object typestate to per-channel protocol state, and they subsume the ad-hoc linearity discipline around `Permission<Tag>` handoffs into a structured framework.

## I.4 Where the literature runs out

The eight papers cited in Appendix A cover synchronous and asynchronous binary and multiparty session types, parametric safety, precise subtyping, and crash-stop. Crucible pushes past them in six places. The full catalog of unresolved frontier items is Appendix D; the short list:

- **Dynamic participant sets.** Canopy's membership changes mid-run (Keepers join, Keepers die, Keepers evict). Classical MPST fixes the participant set at protocol declaration. Dynamic MPST exists (Deniélou-Yoshida 2011) but has not been combined with precise async subtyping, crash-stop, and queue types. We adopt a conservative workaround: participant set ENTRIES are bounded at compile time, the actually-active set is a runtime refinement.

- **Speculative sessions with rollback.** Speculative decoding's draft → target verify → accept-or-reject flow is a session with rollback to a checkpointed position. Transactional session types exist (Vieira et al. 2008) but haven't been layered with MPST + async + crash-stop. We encode rollback via an explicit `Commit` / `Abort` selection point (Appendix D.2).

- **Persistent sessions bridging crashes.** Training runs cross crashes via Cipher checkpoints. The session logically spans the crash; the runtime loses and reloads state from Cipher. Persistent session types are not in mainstream MPST. We model persistence as an implicit "save & replay" clause in the FLR-handling branch.

- **Timed sessions.** HOT-thread dispatch must complete in < 500 ns; ChainEdge signal delivery has PCIe fly-time bounds; collective timeouts are 5 s default. Timed session types (Bocchi-Yang-Yoshida 2014) exist but aren't mainstream. We attach latency-class tags to the session type (HOT / WARM / COLD) as phantom annotations; actual temporal obligations are checked by the bench harness under `Policy::production()` per CRUCIBLE.md §16.9.

- **Content-addressed protocol dedup.** Cipher's L1 / L2 / L3 entries are content-addressed; identical payloads are transmitted once regardless of how many sessions logically require them. This is a form of protocol-level sharing not cleanly modeled in classical session types. We add a `ContentAddressed<Payload>` marker that commutes with Send: `Send<ContentAddressed<T>, R>` means "send this value, but the recipient may already have it and we'll elide the wire bytes if so." Correctness is preserved because the recipient's state transition depends only on the payload's content hash, which arrives unconditionally.

- **Cross-layer protocol nesting.** CNTP Layer 1 transport carries bytes for Layer 2 gossip, Layer 3 Raft, Layer 4 collectives, Layer 5 offload. Each upper layer's payload is itself a session type. Higher-order session types (DGS12 §5 treats this via linear channels carrying channels; Mostrous-Yoshida 2015 for HO-π) give us the mechanism. We declare Layer 1's message payload type as `OpaqueProtocol<HigherLayerSessionType>`; Layer 1 verifies only length + integrity; the higher layer verifies protocol.

Appendix D expands each of these with a concrete specification sketch.

## I.5 What this document does not do

Three explicit non-goals.

**It does not rederive the theory.** Proofs are in the cited papers. This document states what the theorems say, how we apply them, and where the C++ code implements them.

**It does not replace `code_guide.md`.** The guide gives the axioms (InitSafe, TypeSafe, etc.) and compiler flags. This document assumes the guide's conventions and layers session-type reasoning on top.

**It does not promise an immediate complete implementation.** Section VII gives the twelve-phase plan. Phase 1 (binary combinators) already ships. Phases 2–5 are the load-bearing middle. Phases 6–12 are evolution points for the next eighteen months.

---

# Part II — The theory, one coherent picture

Eight papers from the Yoshida school and its neighbors (Honda, Dardha, Scalas, Ghilezan, Barwell, Hou, Pischke, Kuhn, et al. — full attribution in Appendix A) describe overlapping slices of one theory of typed conversations. Read in aggregate they agree; read in isolation they look contradictory because each paper fixes different axes. This part presents the agreed-on picture. Paper citations are attribution, not structure.

The structure of this part: first principles (§II.1); the five design-space axes (§II.2); the binary core (§II.3); multiparty generalizations (§II.4); association as the invariant for the top-down methodology (§II.5); async extensions — queue types, en-route messages, balanced-plus well-formedness (§II.6); subtyping, synchronous and precise-async via SISO decomposition (§II.7); crash-stop and unavailable queues (§II.8); the parametric unification of all the above via Scalas-Yoshida's φ (§II.9); the seven φ levels Crucible chooses from (§II.10); frontier items the literature has not yet reached (§II.11).

## II.1 First principles

A **conversation** is an ordered sequence of message-passing events between a fixed-at-declaration-time set of named participants. Each event is `(sender, receiver, label, payload)`. At any point in a conversation, the set of participants enabled to send or receive is determined by the conversation's history so far. A conversation **ends** when no participant has further obligations.

A **protocol** is a specification of which conversations are allowed. Equivalently, a protocol is a labeled transition system (LTS): states are "positions in the protocol"; transitions are events; `from` states and `to` states encode the ordering constraints; final states mark legal endings.

A **session type** is a syntactic description of a protocol, compact enough for a programmer to write and a type-checker to check. Given a programming language with typed values, the session-type language adds combinators for protocol structure: `Send<T, K>`, `Recv<T, K>`, `Select<B…>`, `Offer<B…>`, `Loop<B>`, `Continue`, `End`, where `T` is a value type, `K` is a continuation session type, and `B…` is a list of labeled branches. A session type describes ONE PARTICIPANT'S view of a protocol; other participants have their own session types.

**Why "conversation" not "protocol".** Protocols exist in the absence of participants (e.g., HTTP is a protocol). Conversations require actual participants exchanging actual messages. A session type is closer to "this conversation, from this participant's seat, must unfold this way" than to "this protocol exists as a Platonic object." The word reminds us that type-checking happens relative to participants.

**The core theorem.** If every participant's implementation is well-typed against its session type, then every complete conversation (from start to end) is in the protocol's allowed set. No other discipline — no locks, no monitors, no atomic-flag heroics — can give this guarantee without a proof, and most production systems do not attempt the proof.

**What counts as a conversation in Crucible.** Every cross-thread and cross-node interaction. The TraceRing between Vessel's dispatch thread and the bg drain thread: a conversation. The ExecutionPlan submission from host CPU to GPU host engine via pushbuffer + doorbell: a conversation. The SWIM probe exchange between two Keepers: a conversation. The all-reduce across N peers: an N-party conversation. The InferenceSession's prefill-then-decode-loop: a conversation between a client session and the Keeper serving it. The FLR recovery handshake between a crashing Keeper, Canopy's Raft leader, and the checkpoint-loading replacement: a three-party conversation.

## II.2 The five-dimensional design space

The theory admits five orthogonal design choices; a specific result fixes values along all five.

**Axis 1 — Binary vs Multiparty (number of participants).** Binary sessions are 2-party; they are the simplest case and the one most mainstream languages support. Multiparty sessions (MPST) have N ≥ 2 participants on one shared session; participant names are part of the protocol. Classic MPST descends from Honda-Yoshida-Carbone 2008; modern MPST is most developed in the Yoshida school.

*Crucible needs multiparty.* MpmcRing has N producers × M consumers; all-reduce has N participants; Canopy has a dynamic participant count; CNTP Layer 4 collectives are inherently multiparty. Binary suffices for the TraceRing and for point-to-point RDMA write/read but not for most of Crucible.

**Axis 2 — Synchronous vs Asynchronous (channel semantics).** Synchronous: send and receive rendezvous; the send is not complete until the receive consumes it. Asynchronous: send enqueues into a buffered channel; receive dequeues; the queue has its own type σ. The buffer may be bounded or unbounded.

*Crucible needs asynchronous with bounded buffers.* Every ring in THREADING.md has a compile-time capacity. The MpmcRing is Nikolaev SCQ: 2N slots for N-capacity logical queue. CNTP's in-transit messages live in network-level queues (RDMA posted-work queues, XDP rings) with bounded depth. Bounded-buffer async is decidable for most properties; unbounded async is generally undecidable.

**Axis 3 — Top-down vs Bottom-up (where protocol specs come from).** Top-down: a global type G describes the whole N-party conversation; per-participant local types are obtained by projection T_p = G↾p. Top-down gives the "one spec, many projections" property useful for choreographic reasoning. Classic MPST is top-down.

Bottom-up: local types are first-class; the typing context Γ associates session-role pairs to local types directly. A global type, if present, is derivable from Γ. Scalas-Yoshida 2019 is the exemplar; their claim is that top-down imposes an over-strong consistency requirement (classical MPST's "coherence") which many real-world protocols — OAuth2 cited famously — fail to satisfy while still being correct.

*Crucible uses both.* Training loops and fleet-coordination protocols (2PC checkpoint, Raft leader election, SWIM membership) are naturally top-down — write G once, project to every role. Lower-level channels (TraceRing, KernelCache publication, PatchPoint writes) are naturally bottom-up — write the two endpoints' local types and let Γ composition verify consistency. The framework admits both by parameterizing on φ; we can choose `φ_associated_with_G` where G is natural and `φ_safe + φ_live+` where it is not.

**Axis 4 — Reliable vs Crash-stop (failure model).** Reliable: every participant runs to the end without crashing. Links never lose messages. This is the easy world. Crash-stop: participants may fail at any point and stop; surviving participants detect the failure and handle it via explicit crash-branches in their protocol. Links between live participants are reliable. Byzantine failures (participants lie) are out of scope.

*Crucible needs crash-stop.* Keepers die (ECC errors, thermal panic, network partition, node eviction by k8s). Cipher persistence and FLR recovery ARE the crash-handling mechanisms at the system level; session types lift these to the protocol level by requiring crash-branches everywhere an unreliable peer is involved. BSYZ22 (sync crash-stop) and BHYZ23 (async crash-stop) are the references.

**Axis 5 — Decidable vs Undecidable (type-checking cost).** Type-checking a session-typed program is decidable for some combinations of the above axes and undecidable for others. Synchronous-bounded-buffer-reliable-single-party subtyping is decidable. Asynchronous-unbounded-buffer precise subtyping is undecidable (Lange-Yoshida 2017; Bravetti-Carbone-Zavattaro 2018). Crash-stop adds decidability questions of its own.

*Crucible lives in a decidable fragment with a fallback.* We restrict to bounded buffers (our rings are bounded), pin a depth bound for precise async subtyping (default 64), and fall back to a weaker subtype relation (classical Gay-Hole 2005, fully decidable) when the precise relation cannot be resolved within the bound. Every subtype relation we accept is SOUND; we may REJECT some sound relations conservatively. An engineer who hits a false rejection can widen the bound, rewrite the protocol, or fall back to the weaker relation via an explicit cast.

The five axes are orthogonal; choosing values along each gives 2⁵ = 32 combinations. Crucible lives at: multiparty + asynchronous-bounded + mixed-top-down-and-bottom-up + crash-stop + decidable-with-depth-bound. The papers in Appendix A cover this cell (PMY25 + BHYZ23 + GPPSY23 together) modulo some frontier items (§II.11).

## II.3 The binary combinator core

The per-endpoint syntax of session types, stripped to essentials:

```
T ::= End                       no more messages
    | Send<P, T>                send value of type P, continue as T
    | Recv<P, T>                receive value of type P, continue as T
    | Select<B_1, …, B_k>       pick one branch; continue as that branch's type
    | Offer<B_1, …, B_k>        offer all branches; peer picks; continue as their pick
    | Loop<T>                   protocol recursion
    | Continue                  re-enter the enclosing Loop
    | Stop                      this endpoint has crashed (post-crash protocol position)

B ::= Branch<Label, T>          a labeled branch's type

// Crash-stop extensions layered atop the base
T' ::= T | Recv<Crash<Peer>, T>    // crash-handling branch; fires when Peer crashes
```

Every session type is FINITE in depth when recursion is guarded (every `Continue` is under a `Loop`). Recursion unfolds lazily: `Loop<T>` is semantically equivalent to its infinite unfolding but the compact form is what we manipulate.

**Duality.** Two participants in a binary conversation have dual session types. The dual of a type is another type, computed structurally:

```
dual(End) = End
dual(Send<P, T>) = Recv<P, dual(T)>
dual(Recv<P, T>) = Send<P, dual(T)>
dual(Select<Branch<L, T>…>) = Offer<Branch<L, dual(T)>…>
dual(Offer<Branch<L, T>…>) = Select<Branch<L, dual(T)>…>
dual(Loop<T>) = Loop<dual(T)>
dual(Continue) = Continue
dual(Stop) = Stop
```

A conversation where the two participants' session types are dual is safe by construction (Honda 1998): every send finds a matching recv, every select's chosen branch has a matching offer branch, no label mismatches.

**Sequential composition (`Compose`).** Two session types can be composed sequentially: `Compose<T, S>` runs `T` to completion, then `S`. This is not quite composition in the DGS12 Kobayashi-encoding sense (which is structural composition of channel usages); it is syntactic continuation-passing, with `T`'s `End` replaced by `S`. Useful for building up multi-phase protocols from pieces.

**Well-formedness (WF).** A session type is well-formed if every `Continue` is syntactically inside a `Loop`, and every `Branch`'s label is distinct within its parent `Select`/`Offer`. WF is decidable at compile time; our `is_well_formed_v<T>` predicate rejects ill-formed types with a diagnostic.

**Subtyping.** Two session types `T ⩽ U` are related by subtyping if a value of type `T` can safely stand where a value of type `U` is expected. Classical (synchronous) subtyping rules (Gay-Hole 2005):

- `End ⩽ End`
- `Send<P₁, T₁> ⩽ Send<P₂, T₂>` when `P₁ ⩽ P₂` and `T₁ ⩽ T₂` (covariant both in payload and continuation — a smaller-payload send can stand where a larger-payload send is expected; subtyping on continuation)
- `Recv<P₁, T₁> ⩽ Recv<P₂, T₂>` when `P₂ ⩽ P₁` (contravariant in payload — accepting a LARGER payload is fine where a smaller is expected) and `T₁ ⩽ T₂`
- `Select<B₁…> ⩽ Select<B₂…>` when every `B₁` has a matching `B₂` with compatible types and payloads (the subtype has FEWER or EQUAL choices — it commits to a subset of the supertype's options)
- `Offer<B₁…> ⩽ Offer<B₂…>` when every `B₂` has a matching `B₁` with compatible types and payloads (the subtype has MORE or EQUAL choices — it handles a superset of the supertype's options)
- `Loop<T₁> ⩽ Loop<T₂>` when `T₁ ⩽ T₂` under the coinductive hypothesis that the outer `Loop`s are related
- `Stop ⩽ T` for any `T` (a crashed endpoint vacuously inhabits anything; nothing will ever receive from Stop)

Async subtyping ⩽_a is stricter — it permits reordering beyond what sync ⩽ allows. §II.7 treats this in detail.

## II.4 Multiparty — global types, projection, typing context

The binary combinator core extends to N-party conversations via a global-type syntax and per-participant projection.

**Global type G**:

```
G ::= end
    | p → q : { m_i(P_i) . G_i  |  i ∈ I }     // p sends to q with one label m_i
    | μt . G                                      // recursion
    | t                                           // recursion variable
    | Stop(p)                                     // p has crashed (runtime-only)
    | p → q : crash . G                           // crash-detection branch
```

The notation `p → q : {m_1(P_1) . G_1, m_2(P_2) . G_2}` reads "p sends to q a message with label `m_1` carrying payload of type `P_1`, then continues as `G_1`; OR p sends `m_2` with `P_2`, continuing as `G_2`". The `I` index set enumerates the choice.

**Projection G ↾ p** extracts participant p's local view:

```
end ↾ p = End

(p → q : { m_i(P_i) . G_i }) ↾ p = Select<Branch<m_i, Send<P_i, G_i ↾ p>>…>
(p → q : { m_i(P_i) . G_i }) ↾ q = Offer<Branch<m_i, Recv<P_i, G_i ↾ q>>…>
(p → q : { m_i(P_i) . G_i }) ↾ r (r ≠ p, q) = merge{G_i ↾ r : i ∈ I}

(μt.G) ↾ p = Loop<G ↾ p>
t ↾ p = Continue

Stop(p) ↾ p = Stop
Stop(p) ↾ q (q ≠ p) = … (continues, possibly with crash-branches)
```

The **merge** of local types (Honda-JACM16 Def 3.3, refined in HYK24 for soundness) combines the projections of branches where the projecting participant is neither sender nor receiver. Two forms exist: *plain merging* (branches must project identically) and *full merging* (branches must be mergeable, allowing some structural divergence if the participant can distinguish them later). PMY25 proves *coinductive full merging* for the async setting.

**Typing context Γ**. Γ is a partial map from `(session_tag, role_tag)` pairs to local session types. The well-formed Γ:

- Domain: finitely many entries
- Each `(session, role)` appears at most once
- Local types are well-formed (every `Continue` under `Loop`, labels distinct in choices)
- Disjoint session tags don't interfere

Γ composes via disjoint union: `Γ₁ ∘ Γ₂` has an entry for every entry in either, requires no overlap, and carries the union of both contexts' invariants. Composition is the multiparty analog of binary duality — if a producer's local type and a consumer's local type are BOTH in Γ and their communication is consistent, the pair is safe.

**Γ reduction.** Given a communication event α (e.g., `p → q : m(v)` for "p sends value v of label m to q"), Γ reduces to Γ' by advancing p's local type past a matching `Send` and q's local type past a matching `Recv`. Formally:

```
Γ, (p : Select<…Branch<m, Send<P, T_p>>…>) →_α Γ, (p : T_p)
Γ, (q : Offer<…Branch<m, Recv<P, T_q>>…>) →_α Γ, (q : T_q)
```

both combined into `Γ →_α Γ'`. Reduction is the semantics of Γ; all safety properties are predicates on the reduction graph.

## II.5 Association — the invariant

Scalas-Yoshida 2019 defined a typing judgment `Θ · Γ ⊢ P with φ(Γ)` parameterized on a user-chosen safety predicate φ; their flagship claim was that many φ choices work uniformly. **A subsequent paper (Hou-Yoshida-Kuhn 2024) proved that the classical subject-reduction proofs used in the "full merging" fragment are actually flawed** — specifically, the "consistency" invariant SY19 used is not preserved by reductions under full merging. The fix is an invariant called **association**.

**Association Δ ⊑_s G** (HYK24 Def 5.2, refined in PMY25 for async):

```
Δ ⊑_s G  iff
    (1) dom(Δ) = { s[p] : p ∈ roles(G) }        -- domain matches roles of G
    (2) ∀ p ∈ roles(G):  Δ(s[p])  ⩽  G ↾ p      -- each entry is a subtype of its projection
```

That is: for a Γ-entry keyed by the pair `(s, p)` — where `s` is a session tag and `p` is a role — the entry's local type must be a SUBTYPE of the projection of G onto p. Subtype, not equal: the local implementation is allowed to refine the protocol (take fewer selects, offer more branches, anticipate async communications via ⩽_a) as long as it remains a sound refinement.

Association generalizes classical "consistency" (which required Γ-entries to be exactly the projections) by allowing subtyping. This wider slot admits real-world protocols — OAuth2, iterative-refinement schemes, many of Crucible's own protocols — that could not be associated with any single global type under classical consistency.

**The association invariant is preserved by reductions.** HYK24 Thm 5.8 proves: if `Δ ⊑_s G` and `Δ →_α Δ'`, then there exists a `G' such that `Δ' ⊑_s G'` and `G →_α G'`. In words: any step of the typing context corresponds to a step of the global type, with association preserved.

**Why association is the right invariant.** Association gives us top-down reasoning: if you write a global type G and your implementation's Γ is associated with G, then the safety/liveness/df properties of G carry over to the implementation. You do not have to re-prove them at the implementation level. This restores the value proposition of top-down MPST: write the protocol once, verify it once, and every associated implementation is correct by construction.

**Association composes with subtyping.** If `Γ₁ ⊑ G` and `Γ₂ ⊑ G'` and the two contexts share no session tags, then `Γ₁ ∘ Γ₂ ⊑ G ⊕ G'` where `G ⊕ G'` is the parallel composition of the two global types. This lets us build a big system's protocol by composing the small ones.

**Association is NOT required.** Bottom-up reasoning proceeds directly: write the Γ, pick a φ, check `φ(Γ)`. No G required. This is Scalas-Yoshida's insight and it is load-bearing for Crucible — many of our protocols (TraceRing's producer/consumer local types, for instance) have no natural global-type expression; we reason bottom-up.

When we DO have a G (Raft, 2PC, SWIM, training step), association lets us reuse G's properties. When we don't, we reason directly on Γ. The framework admits both and tells us, for each channel, which is the cleaner approach.

## II.6 Asynchronous extensions — queue types, en-route, balanced-plus

Synchronous session types rendezvous at each communication event; the sender blocks until the receiver consumes. Crucible's channels are asynchronous: sends enqueue into a buffered channel and the sender continues; receives dequeue and may block only if the buffer is empty. Formalizing async requires three new pieces.

**Queue types σ**. The buffer between two participants is itself typed. A queue type σ is a sequence of pending messages, each tagged by sender / recipient / label / payload type:

```
σ ::= ε                                 empty
    | σ · (p → q, m(P))                append one pending message
```

The `·` operation is right-append. The empty queue `ε` is a sequence with no messages. A queue type is bounded when the sequence length has a compile-time upper bound — matches our MpmcRing capacity.

**Γ with queues** (extended typing context). In async, Γ carries both per-participant local types AND per-channel queue types:

```
Γ ::= Γ, (s[p] : T)              local type T for participant p in session s
    | Γ, σ[p, q]                 queue type σ for the (p→q) channel in session s
    | ∅
```

Async reduction splits send and recv: `Γ →_{p→q send m(v)} Γ'` moves p's local type past `Send<…>` AND appends `(p → q, m(P))` to σ[p, q]. `Γ →_{q←p recv m(v)} Γ''` moves q's local type past `Recv<…>` AND dequeues the head of σ[p, q]. The split is what makes async non-blocking: the send advances without a matching recv.

**En-route transmissions** (PMY25 §3): at the global-type level, the analog of a queued message is an "en-route transmission", syntactically `p ⇝_m q : {m_i(P_i) . G_i}`. This models "p has committed to send but q has not yet received." En-route transmissions appear only in runtime global types (not in static specifications). They are resolved when q receives: the en-route node is eliminated and G_i becomes the new global state. PMY25 Thm 12 proves: if static-G has an async-safe reduction, the runtime-G with en-route transmissions preserves association.

**Balanced+** (PMY25 Def 6.1). A global type G is balanced-plus when, for every reachable runtime state and every pair (p, q), the en-route count is bounded by a finite number. This is stronger than classical balanced (bounded number of active participants per reachable state) and is the well-formedness condition that makes precise async subtyping sound.

*Crucible is balanced-plus by construction.* Every async channel in THREADING.md has a compile-time capacity (MpmcRing has 2N slots for N-capacity logical queue; CNTP posted-work queues have NIC-firmware caps). The en-route count is bounded by the capacity. This is not a theorem we need to prove; it is a property of our bounded-buffer runtime.

**Why bounded buffers matter for decidability.** Lange-Yoshida 2017 showed that async subtyping with UNBOUNDED queues is undecidable in general. Bounded-buffer async is substantially more tractable: subtyping checks can be cast as model-checking queries on finite-state LTSs. Crucible's decidability story relies on bounded buffers across the board; any unbounded channel would be a red flag.

## II.7 Subtyping — synchronous, and precise async via SISO decomposition

Subtyping captures "safe substitution": if `T ⩽ T'`, replacing a T'-typed participant with a T-typed one preserves the protocol's correctness. The relation is the heart of protocol evolution and of the refinement of abstract specifications into concrete implementations.

**Synchronous subtyping ⩽** (Gay-Hole 2005). Six structural rules (§II.3 listed them). Rendezvous semantics means sends and receives cannot be reordered across each other; ⩽ is a coinductive simulation preserving the exact send/recv sequence up to:

- Covariance on `Send<P, T>` both in P and T
- Contravariance on `Recv<P, T>` in P, covariance in T
- Covariance on `Select<…>` (fewer labels is a subtype — narrower choice)
- Contravariance on `Offer<…>` (more labels is a subtype — broader offer)
- Coinduction on `Loop<T>` (unfold and compare bodies under the hypothesis that the outer loops are related)
- Vacuity on `Stop` (Stop ⩽ anything)

⩽ is decidable. An efficient type-checker walks the types in lockstep, emitting a coinductive proof tree that closes at guarded-recursion fixed points. Crucible's synchronous rings (TraceRing SPSC) rely on ⩽ for subsumption checks.

**Precise async subtyping ⩽_a** (GPPSY23). Under asynchrony, sends can be reordered past unrelated receives — a send to participant q can be anticipated past a receive from participant r ≠ q without breaking the protocol, because the send enters q's queue and the receive completes independently. Sync ⩽ is too strict for async because it forbids these reorderings. Precise async ⩽_a admits them, and is the SOUND AND COMPLETE refinement relation for async: any broader relation is unsafe (admits non-live or unsafe substitutions); any narrower relation over-rejects.

GPPSY23 formulates ⩽_a via SISO (Single-Input Single-Output) tree decomposition:

**Step 1 — decompose the full session type into a finite forest of SISO trees.**
An SISO tree has no `Select` (single output per branching) and no `Offer` (single input per branching). It is a linear sequence of sends and receives ending at `End` or at a loop-back. A full session type T is decomposed into the set of SISO trees obtained by taking every possible path through T's choice points.

**Step 2 — define a refinement relation  on SISO trees.** Six rules:

```
[ref-in]   Recv<P, W> ⊑ Recv<P', W'>        when  P ⩽ P' and W ⊑ W'
[ref-out]  Send<P, W> ⊑ Send<P', W'>        when  P ⩽ P' and W ⊑ W'
[ref-A]    Recv<P₁, …>·A ⊑ A·Recv<P₂, …>    // anticipate a recv past an A-sequence of recvs from OTHER participants
[ref-B]    Send<P₁, …>·B ⊑ B·Send<P₂, …>    // anticipate a send past a B-sequence of recvs-from-anyone / sends-to-others
[ref-end]  End ⊑ End
[ref-subsort]  S <: S'                      // basic value-type subsorting
```

The crucial rules are [ref-A] and [ref-B]: they formalize which I/O reorderings preserve async safety. An A-sequence is a finite sequence of recvs from participants different from the one whose recv is being anticipated. A B-sequence is a finite sequence of recvs from ANY participant and sends to participants different from the one whose send is being anticipated. The "different from" side condition is what prevents unsafe reorderings.

**Step 3 — define ⩽_a on full types via SISO decomposition.**
T ⩽_a T' iff for every SISO tree in the decomposition of T there is a matching SISO tree in the decomposition of T' related by ⊑. Equivalently: every concrete path through T refines some path through T'.

GPPSY23 Thm 7.1 proves ⩽_a is PRECISE: sound (T ⩽_a T' implies safe substitution) and complete (any relation broader than ⩽_a is unsound). This is the canonical async refinement relation for bounded-buffer MPST.

**Decidability.** ⩽_a is UNDECIDABLE in the general case. Bravetti-Carbone-Zavattaro 2018 and Lange-Yoshida 2017 both proved it. In practice, the undecidability manifests only on exotic protocols; most real protocols admit bounded-depth decision procedures (Cutner-Yoshida-Vassor 2022, "Rumpsteak"). Crucible's strategy: bound SISO-tree expansion at compile-time depth 64 (user-configurable). Any relation ⩽_a provable within depth 64 is accepted; deeper relations require either depth widening or a structural proof hand-written by the engineer.

**Crucible's subtyping tiers.**

| Tier | Relation | Where used |
|---|---|---|
| Exact match | `T == T'` | Default for initial compile; no subsumption |
| Sync subtyping | `T ⩽ T'` (Gay-Hole) | Refinement of synchronous rings (TraceRing, ChainEdge wait), protocol versioning, CNTP Layer 3 Raft protocol evolution |
| Precise async subtyping bounded | `T ⩽_a T'` to depth 64 | MpmcRing, Cipher hot-tier, collectives, InferenceSession continuous batching |
| Unbounded precise async | — | Not supported; undecidable in general; either rewrite the protocol or use a weaker relation |

The tier is chosen per call site; Part IV lists the choice for every channel.

## II.8 Crash-stop — reliability, stop, unavailable queues, crash branches

Participants die. Keepers crash. Networks partition. A production protocol-typed system must admit failure as a first-class protocol event, not handle it via runtime exceptions.

The crash-stop model (BSYZ22 synchronous, BHYZ23 async) adds:

**Stop type**. `Stop` is a session type representing a crashed endpoint's post-crash position. A participant whose local type is `Stop` has no further protocol obligations; any attempt to send to them fails; any receive-attempt from their queue returns a "crash" indication. `Stop ⩽ T` for any T — Stop vacuously satisfies any protocol, because Stop will never observe T's continuations.

**Crash label**. In an `Offer<…>`, an additional branch `Branch<Crash<p>, T>` handles "participant p has crashed". The recipient enters T when the runtime detects p's crash (via SWIM gossip confirming death, via missed heartbeat, via RDMA completion error carrying `IB_WC_RETRY_EXC_ERR`, etc.). The crash branch is SYNTACTICALLY MANDATORY when receiving from an unreliable participant; the type-checker rejects `Offer<…>` that omits the crash branch for an unreliable peer.

**Unavailable queue ⊘**. An async send to a crashed participant's queue transitions that queue to `⊘` — all subsequent sends are silently dropped (BHYZ23 §4.2). The queue's recipient never receives any message that arrived after the crash. This models TCP's "sent to dead peer" semantics: packets are enqueued, then lost.

**Reliability set R ⊆ Roles**. Some participants are assumed reliable (they won't crash during the protocol); others may crash. R is a design-time choice. Typical choices in Crucible:

- R = {Canopy-leader} for Raft-dependent protocols (the current Raft leader is assumed reliable within a commit; if it dies, Raft re-elects and the protocol rewinds)
- R = ∅ for purely peer-to-peer collectives (any participant may die, any may survive)
- R = {all} for within-Keeper channels (no crashes between co-located threads; the Keeper either lives or dies as a unit)

Participants in R need not appear in crash-branches. Participants NOT in R must appear in crash-branches of every peer that receives from them.

**Crash-aware φ**. A safety property φ is crash-aware when:

- It admits the reliability set R as a parameter
- It treats `Stop`, unavailable queues, and crash labels as first-class events in the LTS
- It preserves under [GR-✂] (the crash-event reduction rule) and [GR-⊘] (the queue-becomes-unavailable reduction)

BSYZ22 and BHYZ23 verify safety, deadlock-freedom, and liveness under crash-aware φ. Crucible adopts their discipline wholesale.

**What crash-stop is NOT.** Byzantine failure, network corruption, link-level message loss between live peers, replay attacks, any form of adversarial behavior. These are out of scope; the runtime's cryptographic layer (mTLS over CNTP, Cipher signature verification) handles them at a lower level and does not interact with session types.

## II.9 The unification — parametric safety φ

The preceding sections presented four distinct kinds of correctness invariant: duality (binary), consistency / association (top-down multiparty), subtyping-preservation (Gay-Hole / GPPSY23), crash-handling (BSYZ22 / BHYZ23). Scalas-Yoshida 2019's key insight is that these are all instances of ONE pattern: a safety property φ on Γ that is preserved by reduction.

**The typing judgment**:

```
Θ · Γ  ⊢  P : T  with φ(Γ ∘ { s[p] : T })
```

In words: under the typing context Γ and the auxiliary context Θ (for process variables, recursion variables, etc.), process P has local session type T in role p of session s, AND the extended Γ satisfies the chosen safety property φ.

**The master theorem** (SY19 Thm 4.8, with HYK24's fix to the proof):

> If Γ ⊢ P with φ(Γ), and P reduces to P', then there exists Γ' such that Γ' ⊢ P' with φ(Γ'). Equivalently: if the initial Γ satisfies φ, all reachable Γ's satisfy φ.

This theorem is φ-parametric. Crucible chooses φ per channel:

| φ | What it preserves | Where used |
|---|---|---|
| φ_safe | No label mismatch, no stuck-outside-End | Anywhere correctness of messages matters; effectively the minimum |
| φ_df | φ_safe + never stuck at non-End | Collectives that must reach completion |
| φ_term | φ_df + terminates in finite steps | One-shot RPCs, Cipher cold-tier writes |
| φ_nterm | φ_safe + does not terminate | SWIM gossip, serving loops |
| φ_live | φ_df + every pending I/O eventually fires | Bucketed async all-reduce (overlap comm with compute) |
| φ_live+ | φ_live under fair scheduling | Raft consensus, InferenceSession |
| φ_live++ | φ_live under any scheduling | Training step's critical path |
| φ_associated_with_G | Γ ⊑_s G for global type G | 2PC, SWIM membership, training-step global protocol |
| φ_precise_a | φ_safe + subtype of G under ⩽_a | MpmcRing, async collectives with producer pipelining |
| φ_crashstop_R | φ above + crash-aware for roles not in R | All distributed protocols with possible Keeper death |
| φ_CSL | φ above + permission balance preserved at every communication | Channels that transfer Permission tokens |

Compound φ's compose: `φ_df ∧ φ_precise_a ∧ φ_crashstop_R ∧ φ_CSL` is the canonical Crucible choice for distributed data-plane collectives.

**Why this is the right unification.** The eight papers cited in Appendix A disagree on details but agree on the skeleton: a typing judgment parameterized by φ, preserved by reduction. Pick the right φ for a channel and every theorem in the literature about THAT φ transfers to Crucible's implementation.

**What the framework gives us operationally.** Compile-time: structural checks (well-formedness, domain of Γ, composition). Compile-time meta-analysis: subtyping to depth bound, association for channels with a global type. Offline verification: μ-calculus model-checking via mCRL2 for any φ expressible in μ-calculus (all seven SY19 levels are). Runtime: atomics and semaphores enforcing the per-participant state transitions; crash detection via CNTP layer 2 (SWIM) and layer 1 (RDMA completion errors).

## II.10 The seven φ levels

All seven of Scalas-Yoshida's decidable safety properties (SY19 Fig 5), expressible as modal μ-calculus formulas on Γ's LTS:

| Level | Formula sketch | Semantics |
|---|---|---|
| **safe** | `νZ. (∀α: [α]Z)` where α ranges over communication events that preserve type-structure | Every communication event is well-typed; no label/payload mismatches ever |
| **df** (deadlock-free) | `νZ. (<>Z ∨ atEnd)` | From every reachable state, either further progress is possible OR the state is at End |
| **term** (terminating) | `μZ. atEnd ∨ (∀α: [α]Z)` | Every fair path terminates at End |
| **nterm** (non-terminating) | `νZ. ¬atEnd ∧ (<>Z)` | Infinite service: never terminates, always has a reduction |
| **live** | `νZ. [pendingIO]⟨α⟩ Z` | Every pending I/O will eventually fire |
| **live+** | `live` under fair scheduling (strong fairness of participants) | Pending I/O fires given fair scheduler |
| **live++** | `live` under any scheduling (every scheduler, including adversarial) | Pending I/O fires regardless of scheduler |

All seven are DECIDABLE on finite-state Γ (our Γ is finite-state by construction: no unbounded participant count, no unbounded loop depth, no unbounded queue length). Model-checking in mCRL2 scales to Γ's with tens of participants and dozens of protocol states — more than Crucible needs for any single channel.

**Why seven and not fewer.** Each level captures a different operational guarantee. Distributed-system engineering needs to distinguish "I need the protocol to never get stuck even under weird scheduling" (live++) from "I'm OK if pending I/O only fires when the scheduler is fair" (live+) from "as long as we don't deadlock" (df). Crucible uses all seven; Part VI's table lists which is chosen for each channel.

**Why more levels don't help.** Levels beyond live++ (e.g., "liveness with bounded response time") are either out of scope (need timed protocols — Bocchi-Yang-Yoshida 2014) or decidably equivalent to live++ in finite-state systems. The SY19 hierarchy is complete for untimed finite-state MPST.

## II.11 Where the literature hasn't reached — frontier

Six open items that Crucible hits and the literature has not cleanly resolved. Each is expanded with a specification sketch in Appendix D.

**F.1 Dynamic participant sets.** Canopy's member count changes at runtime — Keepers join (scale-up, node admission), Keepers leave gracefully (eviction, planned shutdown), Keepers die (FLR, network partition). Classical MPST fixes the participant set at protocol declaration. Dynamic MPST (Deniélou-Yoshida 2011) exists but has not been combined with precise async subtyping, crash-stop, and queue types. Our workaround: a protocol-level "membership" event type `MemberChange<Action, p>` where Action ∈ {Join, Leave, Die}; the protocol has a dedicated dispatch-to-membership-subprotocol branch that updates the active-participant set. Actual cross-combined-theory formalization deferred; we document the invariants and verify them by reflection at compile time.

**F.2 Speculative sessions with rollback.** Speculative decoding's draft → target verify → accept-or-reject is a session with rollback to a saved checkpoint on reject. Transactional session types (Vieira et al. 2008) exist but don't combine with MPST + async + crash-stop. We model rollback via a `CheckpointedSession<ProtoSafe, ProtoRollback>` wrapper that records the session state before entering `ProtoRollback`; on reject, state is restored and the session re-enters `ProtoSafe` at the checkpoint. Soundness argument by reduction to a synchronous two-state protocol and by hand (not mechanized).

**F.3 Persistent sessions across crashes.** A training run spans crashes via Cipher checkpoints. The session is conceptually continuous: `Recv<Batch, Recv<Grad, …>>` is never "broken" by a crash; recovery loads Cipher state and resumes the session. Persistent session types are not in mainstream MPST. Our model: the protocol has an implicit `crash_handler: load(cipher); continue_from_checkpoint` branch; any reduction from crash-state reduces to the same protocol-state as the checkpoint. Soundness by reduction to crash-stop with explicit `Stop(p) . load_replacement(p) . resume` rules.

**F.4 Timed sessions for latency-critical channels.** HOT-thread dispatch (§16 CRUCIBLE.md) must complete in < 500 ns; ChainEdge signaling has PCIe fly-time bounds; collective timeouts are 5 s. Timed session types (Bocchi-Yang-Yoshida 2014) attach clock constraints to types. We attach phantom latency-class tags: `HotSession<P>` / `WarmSession<P>` / `ColdSession<P>` wrap a protocol P with a timing budget. Enforcement: runtime bench harness (Policy::production) measures against the budget; type-level wrapping is documentation plus contract-assert on completion.

**F.5 Content-addressed protocol dedup.** Cipher's L1/L2/L3 entries are content-addressed; two sessions transmitting the same payload hit the same Cipher entry. At the protocol level this is a form of sharing: `Send<ContentAddressed<T>, K>` means "conceptually send T but elide wire bytes if recipient has hash(T)". This is a form of quotient typing — the protocol is invariant under payload-hash-preserving transformations. Not in classical MPST. Correctness argument: the recipient's observed state transition depends only on the payload's content hash; whether the bytes arrived over wire or were found in cache is opaque.

**F.6 Cross-layer nesting.** CNTP Layer 1 bytes carry Layer 2 protocol frames, which carry Layer 3 Raft RPCs, which carry Layer 4 collective announcements. Each outer layer's payload type IS a session type. Higher-order session types (DGS12 via linear channels carrying channels; Mostrous-Yoshida 2015 for HO-π) give the formal machinery. We declare Layer 1's payload type as `OpaqueProtocol<HigherLayerSessionType>` — Layer 1 does length + integrity + encryption, the higher layer does protocol. Soundness: the upper layer's well-typed usage of its channel does not depend on how Layer 1 delivers bytes, provided Layer 1's delivery is reliable-or-reports-failure (yes for RDMA, yes for TCP with checksum, out of scope for Soft-RoCE emulation).

Full specifications for F.1–F.6 in Appendix D.

## II.12 Combination and compositionality

The value proposition in §I.2 was "each channel is verified independently, then the whole system's correctness follows from per-channel correctness plus a per-pair interaction check." This section makes that promise precise. Session types, typing contexts, global types, queue types, safety properties, crash-stop reliabilities, and CSL permissions ALL have compositional structure, and the composition commutes with every theorem in Part II — provided certain compatibility conditions hold.

This is the architectural pay-off: we scale to fifty Crucible channels by composing small proofs, not by re-proving the whole. The compositionality principle is load-bearing; without it the typed-conversations approach would not be worth the effort.

### II.12.1 The four core combinators

Session types combine in four primary ways. Each combinator has a syntactic form, a semantic meaning, a compatibility condition, and a compositionality theorem.

**Sequential (`T ; S`).** Run protocol T to its End, then run S. Syntactically: every End in T is replaced by S's start. Semantically: T's reduction graph is followed by S's, with T's exit state becoming S's entry state.

```
Compatibility: T's exit permissions ⊇ S's entry permissions
Compositionality: Γ_T ⊢ P_T : T with φ_T  AND  Γ_S ⊢ P_S : S with φ_S
                  ⇒  Γ_T ∘ Γ_S ⊢ P_T ; P_S : T ; S  with  φ_T ∧ φ_S
```

Crucible usage: TraceRing → bg pipeline → KernelCache publication is one sequential composition of three binary sessions.

**Parallel (`T ∥ S`)** (CSL frame rule on protocols). Run T and S simultaneously on disjoint resources. Both must terminate for the composition to terminate.

```
Compatibility: dom(Γ_T) ∩ dom(Γ_S) = ∅
               Perm(Γ_T) ∩ Perm(Γ_S) = ∅
               σ-endpoints(Γ_T) ∩ σ-endpoints(Γ_S) = ∅
Compositionality: Γ_T ⊢ P_T : T with φ_T  AND  Γ_S ⊢ P_S : S with φ_S
                  ⇒  Γ_T ∪ Γ_S ⊢ P_T ∥ P_S : T ∥ S  with  φ_T ∧ φ_S
```

This IS the CSL frame rule, lifted from memory regions to protocol-typed channels. Disjoint contexts interleave without interference; each sub-system's invariants survive into the composition.

Crucible usage: multiple independent training-step invocations running on different partitions. Multiple InferenceSessions serving concurrent requests. Gradient compute in parallel with bucketed async all-reduce (the bucketed pattern EXPLICITLY requires parallel composition to hide communication behind compute).

**Choice (`T ⊕ S` at meta-level).** Non-deterministically or deterministically pick either T or S; the choice is external to the protocols themselves. Distinguish from `Select<Branch<L1, T>, Branch<L2, S>>` which is a PROTOCOL-INTERNAL choice at a specific state.

```
Compatibility: both T and S start from the same roles with compatible initial states
Compositionality: Γ ⊢ P_T : T with φ_T  AND  Γ ⊢ P_S : S with φ_S
                  ⇒  Γ ⊢ (if cond then P_T else P_S) : T ⊕ S
                  with  (cond ⇒ φ_T) ∧ (¬cond ⇒ φ_S)
```

Crucible usage: recipe-dispatch branches (BITEXACT vs ORDERED vs UNORDERED pick different collective protocols at compile time), deployment-mode branches (single-node vs distributed), fallback paths (cache hit uses fast protocol, miss uses compile protocol).

**Delegation (`delegate(T) within S`)** (Honda 1998 throw/catch). One session's endpoint is transferred through another session. The owner of a T-endpoint can send T itself as a message on an S-channel; the receiver gains the T-endpoint and may use it.

```
Compatibility: S has a Send<T, K> at the delegation point;
               the sender must own the T-endpoint;
               the receiver's type at the matching Recv<T, K'> must match T's dual
Compositionality: delegation preserves all properties of T
                  (safety, liveness, association, crash-handling, permissions)
                  provided S itself is well-typed for the carrier
```

Crucible usage: Vessel dispatch hands an IR001 capture session to bg thread — classic delegation. Cipher promotion hands the Raft-log endpoint from hot-tier to warm-tier manager. FLR recovery delegates the resume session from the dying Keeper to its replacement.

### II.12.2 Advanced combinators

Beyond the four primary combinators, Crucible needs five more derived forms. Each is syntactic sugar over the four primaries plus additional machinery.

**Higher-order (channel-carrying channels, DGS12 §5).** A session can carry other sessions as payloads: `Send<Channel<T>, K>`. This is delegation made first-class. Enables cross-layer nesting from F.6: CNTP Layer 1's message payload type IS a session type (Layer 2's protocol), and Layer 1 delivers those sessions without reasoning about their internal structure.

```
Compatibility: the carried session's compatibility conditions hold wherever it is deserialized
Compositionality: the outer session's properties compose with the inner's provided
                  the carrier is reliable (Layer 1 RDMA gives us this; Soft-RoCE does not)
```

**Quotient by content address (`T/≈`).** T's messages are compared up to content-hash equivalence. Two sends that carry identical payload bytes are the same message at the quotient level.

```
Compatibility: payload types are content-addressable (trivially copyable)
Compositionality: quotient preserves all properties of T  because the recipient's
                  observable state depends only on payload CONTENT, not delivery details
```

Crucible usage: Cipher three-tier publication (L1 IR002 snapshots, L2 IR003 per-vendor, L3 compiled-binary) at the quotient level deduplicates identical-content across sessions.

**Timed (`T under Budget`).** T carries a latency budget Budget (per message or per protocol). Budget violation is an observable event; the protocol can branch on timeout.

```
Compatibility: Budget values composed via sum (sequential), max (parallel), or max (choice)
Compositionality: (T under B_T) ; (S under B_S) : T;S under B_T + B_S
                  (T under B_T) ∥ (S under B_S) : T∥S under max(B_T, B_S)
                  timeout-on-violation is a Select branch, included by default
```

Crucible usage: HOT-thread dispatch (< 500 ns), ChainEdge signaling (< 400 ns PCIe), collective timeout (5 s default), IntentionSession query (< 100 ms SLA).

**Checkpointed (`T with rollback to CP`).** T at any point can rollback to the checkpoint CP, restoring protocol state.

```
Compatibility: CP is a well-typed prefix of T; state at CP is serializable
Compositionality: (T ckpt CP_T) ; (S ckpt CP_S) has rollback points at either CP
                  parallel composition rolls back both sub-systems if either rolls back
                  (the cross-rollback constraint ensures consistency)
```

Crucible usage: speculative decoding (draft → verify → accept-or-rollback), Transaction (begin → ops → commit-or-abort), FLR recovery (before-crash ↦ checkpoint-replay).

**Symmetric/permutation-quotient (`T under Perm{r1, r2, …}`).** The protocol is invariant under permutations of the named roles. Useful when roles are structurally identical.

```
Compatibility: all permuted roles have identical local types; resource allocation is symmetric
Compositionality: the permuted composition is a subtype of every concrete permutation
```

Crucible usage: N-producer MpmcRing (all producers interchangeable at the type level); all-reduce over DP group (any permutation yields the same global type up to relabeling).

### II.12.3 Compatibility conditions — the full catalog

Compositionality theorems require compatibility. The catalog, organized by which invariant each condition preserves:

**Domain disjointness** (for parallel):
- `dom(Γ₁) ∩ dom(Γ₂) = ∅` — no shared session-role pairs
- Enforced at compile time by `compose_context_t<Γ₁, Γ₂>`'s static_assert

**Permission disjointness** (for parallel, for CSL):
- `Perm(Γ₁) ∩ Perm(Γ₂) = ∅` — disjoint CSL permission sets
- Enforced by `permission_fork` in Permission.h; checked at each split point

**Queue-endpoint disjointness** (for parallel in async):
- σ-endpoints of Γ₁ and Γ₂ refer to different channels
- Enforced by distinct session tags (a session tag IS a channel identity)

**Reliability containment** (for crash-stop composition):
- R₁ ⊆ R, R₂ ⊆ R — sub-system reliability sets are subsets of the composed R
- Crash branches of composition include union of sub-systems' crash branches

**Subtype compatibility** (for refinement and substitution):
- If `P : T` and we want to use `P` where `T'` is expected, need `T ⩽ T'`
- Subtype relation preserved by all four primary combinators (product subtyping)

**End-compatibility** (for sequential):
- T's End permissions ⊇ S's entry permissions
- T's Stop-branches compatible with S's crash-handling (crash-sticky: once a peer is Stop in T, they remain Stop in S)

**Temporal compatibility** (for timed):
- Budget arithmetic closes: sequential adds, parallel maxes, choice maxes
- Timeout on any segment cascades to the composition

**Content-hash compatibility** (for quotient):
- Payload types are trivially copyable and have stable content hash
- Recipient's observable behavior depends only on payload content

### II.12.4 The meta-theorem: compositionality of session theory

The theorem that justifies the whole compositional approach:

**Theorem (Compositionality of typed conversations).** Let:
- `Γ₁ ⊢ P₁ : T₁ with φ₁(Γ₁)` (a well-typed sub-system)
- `Γ₂ ⊢ P₂ : T₂ with φ₂(Γ₂)` (a disjoint second sub-system)
- Compatibility conditions hold for the chosen combinator ⊙

Then:
- `Γ₁ ⊙ Γ₂ ⊢ P₁ ⊙ P₂ : T₁ ⊙ T₂ with (φ₁ ∧ φ₂ ∧ φ_⊙)(Γ₁ ⊙ Γ₂)`

where φ_⊙ is the combinator-specific additional property (usually trivial — disjointness already handles the hard cases).

This theorem is PROVED for each of the four primary combinators under bounded parameters:
- Sequential: trivial by protocol-level continuation
- Parallel: via CSL frame rule, BSYZ22 Thm 5.1 for crash-stop analog
- Choice: by truthful-disjunction preservation
- Delegation: by Honda 1998 throw/catch safety theorem

**Corollary (Subtype compositionality).** If T₁ ⩽ T₁' and T₂ ⩽ T₂', then T₁ ⊙ T₂ ⩽ T₁' ⊙ T₂' for all four primary combinators. This is product subtyping; proof by structural induction on combinator shape.

**Corollary (Association compositionality).** If `Γ₁ ⊑_s G₁` and `Γ₂ ⊑_s G₂` with disjoint session tags, then `Γ₁ ∘ Γ₂ ⊑_s (G₁ ∥ G₂)` for the parallel composition of global types. HYK24's association preservation theorem extends to composed contexts.

**Corollary (Crash compositionality).** If `Γ₁` is crash-safe under R₁ and `Γ₂` is crash-safe under R₂, with disjoint session tags, then `Γ₁ ∘ Γ₂` is crash-safe under R₁ ∩ R₂ (pessimistic: the composition is only as reliable as the intersection; a role reliable in one sub-system must also be reliable in the other to count in the composition).

**Corollary (Permission compositionality for CSL).** If `Γ₁` is permission-balanced over initial perms P₁ and `Γ₂` over P₂, with disjoint perm sets, then `Γ₁ ∘ Γ₂` is permission-balanced over P₁ ⊎ P₂. This is the original CSL frame rule.

### II.12.5 Per-φ-level compositionality

Not every φ composes under every combinator. The table:

| φ | Sequential `;` | Parallel `∥` | Choice `⊕` | Delegation |
|---|---|---|---|---|
| **safe** | ✓ | ✓ | ✓ | ✓ |
| **df** (deadlock-free) | ✓ | ✓* | ✓ | ✓ |
| **term** | ✓ | ✓ | ✓ (if both branches term) | ✓ |
| **nterm** | ✗ (sequential of nterm is nterm only at T) | ✓ (parallel of nterm is nterm) | ✓ (if either branch nterm) | ✓ |
| **live** | ✓* | ✓** | ✓ (if both branches live) | ✓ |
| **live+** | ✓ | ✓ | ✓ | ✓ |
| **live++** | ✓ | ✓ | ✓ | ✓ |
| **associated** | ✓ | ✓ (global-type parallel ∥) | ✓ (requires a global-type choice) | ✓ |
| **precise-async-safe** | ✓ | ✓ (disjoint queues) | ✓ | ✓ |
| **crash-safe under R** | ✓ | ✓ (R intersection) | ✓ | ✓ |
| **permission-balanced** | ✓ (end⊇start perm match) | ✓ (CSL frame rule) | ✓ (conditional branches preserve) | ✓ (delegated perms transfer) |

`*` denotes "compositional under fair scheduling assumption." Live under unfair scheduling composes sequentially because T's liveness guarantees its end happens, which triggers S; but not under parallel unless the scheduler is fair — an unfair scheduler could starve one sub-system forever.

`**` denotes "compositional with independence assumption." Parallel of two live systems is live provided they don't starve each other on shared resources; disjointness conditions ensure this.

The takeaway: **all the practically-useful φ's (safe, df, live+, live++, crash-safe, CSL-balanced, associated) compose under all four primary combinators given disjointness.** This is the strong compositionality result that makes the framework scale.

### II.12.6 Worked example — composing TraceRing and bg pipeline

Concrete instance of the theorem.

**Sub-system 1** — TraceRing:
- Γ₁ = `{ (trace, dispatch) : Loop<Send<TraceEntry, Continue>>, (trace, drain) : Loop<Recv<TraceEntry, Continue>> }`
- T₁ (producer view) = Loop<Send<TraceEntry, Continue>>
- φ₁ = safe ∧ live+
- P₁ (process) = the Vessel's dispatch loop posting to TraceRing

**Sub-system 2** — bg pipeline:
- Γ₂ = `{ (pipeline, builder) : Loop<Recv<TraceEntry, Send<Region, Continue>>>, … }`
- T₂ = Loop<Send<Region, Continue>> (bg's output to next stage)
- φ₂ = safe ∧ df
- P₂ (process) = the bg worker polling TraceRing and producing Regions

**Composition via sequential** (TraceRing delivers to bg pipeline):
- Γ₁ ; Γ₂ has BOTH contexts in the same typing universe, where TraceRing's `(trace, drain)` role IS the first input of bg pipeline's `(pipeline, builder)` role.
- Compatibility: TraceRing's drain-role End permissions match bg pipeline's builder-role entry permissions (both require `Permission<TraceRingConsumer>`).
- Resulting φ = φ₁ ∧ φ₂ = safe ∧ live+ ∧ df, equivalent to safe ∧ live+ (live+ implies df).
- Association: neither sub-system has a natural global type, so we reason bottom-up; the composition also has no global type.

Composition via delegation would be different: Vessel could delegate the TraceRing-drain endpoint to bg pipeline via an out-of-band channel carrying the endpoint. This is higher-order; the receiver gains full access to the drain endpoint and can use it per T₁'s drain side.

The takeaway: the composition of TraceRing + bg pipeline is well-typed and satisfies the intersected safety property; we verified each sub-system once and composed them.

### II.12.7 Cross-layer composition — CNTP's nested protocols

CNTP's five-layer architecture (CRUCIBLE.md §5) is inherently cross-layer: Layer 1 transport carries Layer 2 gossip frames, which carry Layer 3 Raft RPCs, which carry Layer 4 collective announcements, which carry Layer 5 NetworkOffload directives.

Formalization via higher-order session types:

- Layer 1's outgoing session: `Loop<Send<OpaqueProtocol<LayerNProtocol>, Recv<Ack, Continue>>>`
- Each `OpaqueProtocol<P>` wraps an inner session type P; Layer 1 delivers the bytes without inspecting P.
- Layer N (2, 3, 4, 5) is parameterized over its carrier Layer N-1: `LayerN<Carrier>` has its own session type and nested inside Carrier's message payload.

Compatibility condition for the nesting: Carrier's delivery is RELIABLE-OR-REPORTS-FAILURE. RDMA delivers this (completion errors are reported); TCP over io_uring does (kernel reports errors); Soft-RoCE emulation mostly does but has edge cases documented in CRUCIBLE.md §17.6.

Compositionality of the full CNTP stack:
- Layer 1 satisfies safe ∧ live+ under its own Γ_1
- Layer 2 satisfies safe ∧ nterm under Γ_2 (SWIM runs forever)
- Layer 3 satisfies safe ∧ live+ under Γ_3 (Raft commits progress)
- Layer 4 satisfies safe ∧ live+ under Γ_4 (collectives terminate)
- Layer 5 is opt-in; when present, Γ_5 satisfies safe
- Composed: `Γ_1 ∘ Γ_2 ∘ Γ_3 ∘ Γ_4 ∘ Γ_5 ⊢ CNTP-full with safe ∧ live+ ∧ nterm_layer2`

The composition is verified layer-by-layer; the whole stack satisfies the intersected φ, which is what CNTP's correctness depends on.

### II.12.8 What does NOT compose cleanly

Honesty: four things don't compose via the framework.

**Byzantine failures.** The framework assumes crash-stop, not adversarial behavior. If a participant lies about its protocol state (sends wrong label claiming it was the right one), session types don't help. Mitigation: cryptographic authentication of messages (Cipher signature verification, mTLS on CNTP) verifies identities at a lower layer, outside the session-type abstraction.

**Shared mutable state outside Permission discipline.** If two sub-systems touch the same memory without going through Permission-typed channels, disjointness breaks and frame rule fails. Mitigation: code_guide.md §VIII axioms forbid this; all cross-thread / cross-process state is Permission-wrapped; any exception is an axiom violation.

**Unbounded recursion.** A protocol with unbounded recursive depth (not the same as Loop's guarded recursion — unbounded here means the depth of nested Loop<Compose<X, Loop<…>>>) breaks decidability and breaks compositionality proofs that rely on induction. Mitigation: well-formedness check rejects unbounded nesting.

**Temporal composition across disjoint scheduling domains.** Timed sessions compose within one scheduling domain (e.g., one NUMA node's realtime scheduler). Across domains (one node's HOT thread and another node's COLD thread composed with a budget), the budget arithmetic assumes a shared clock; Crucible's replay-determinism story says wall-clock is not reproducible. Mitigation: explicit budgeting per domain, cross-domain timed composition deferred to a future framework extension.

These four are enumerated in Appendix D's frontier items; they're areas where the eight papers do not cleanly cover what Crucible might encounter. For all other combinations, the compositionality theorem holds.

## II.13 The modular proof strategy

Compositionality (§II.12) is the mathematical statement; the modular proof strategy is the operational recipe. For any new Crucible channel, the proof obligation splits into four pieces, each discharged locally.

**Step 1 — State the protocol.** Write the session type (local or global) for the new channel. Well-formedness follows from syntactic checks (L1): every `Continue` under `Loop`, distinct labels in `Select`/`Offer`, guarded recursion.

**Step 2 — Choose φ.** Pick from the seven SY19 levels plus the compound instantiations (`φ_assoc_G`, `φ_crashstop_R`, `φ_CSL`, `φ_precise_async`). The Part VI table gives the default for each channel class; deviations require justification.

**Step 3 — Discharge φ locally.** For the channel's Γ in isolation, prove `φ(Γ)` via:
- Structural reasoning on small Γ (template metafunction at compile time)
- Modal μ-calculus evaluator for medium Γ (~100 LTS states)
- Offline mCRL2 for large Γ (~1000 LTS states), with certificate archived to Cipher
- Manual proof sketch attached as a comment for Γ beyond mCRL2's reach (rare)

**Step 4 — Compose.** Apply the compositionality theorem (§II.12.4):
- Verify disjointness (domain, permission, queue, reliability) with adjacent channels
- Invoke the per-combinator rule (`;` sequential, `∥` parallel, `⊕` choice, delegation)
- Resulting system's φ is the conjunction; preserved by further composition

**What this buys us operationally.** Adding a new Crucible channel requires proving its φ ONCE. Nothing else in the system needs re-verification. The system's total proof is `φ_channel_1 ∧ φ_channel_2 ∧ … ∧ φ_channel_n`, each conjunct verified once, total verification cost O(N) instead of O(2^N).

**What this does NOT buy us.** Inter-channel dependencies that go beyond protocol-level events (e.g., two channels sharing a cache; one channel's output being another's precondition) DO require system-level reasoning. The session-type framework handles inter-channel protocol composition; it does not replace reasoning about shared state. Keep shared state behind `Permission<Tag>` discipline; session types prove the communication is correct; CSL at layer L9 proves the state access is correct.

**Concrete proof obligation template.** When submitting a PR that adds a channel:

```
Channel: Vessel-Keeper-Dispatch  (§IV.1)
Protocol: G_dispatch (global type written in L4 syntax)
φ chosen: is_df_v ∧ is_live++_v ∧ is_crash_safe_v<R={Vigil}>
Discharge:
  is_df_v: verified by template metafunction over Γ_dispatch's LTS (6 states)
  is_live++_v: verified structurally; no Loops without guards; no blocking Recv
  is_crash_safe_v: Vigil in R (same-process); no crash branches required
Composition witnesses:
  with §IV.2 TraceRing via sequential ';': compatible (Vigil's End matches TraceRing's Start)
  with §IV.4 KernelCache via parallel '∥': disjoint session tags, disjoint permissions
```

One template; fill in per channel; the compiler discharges the metafunction claims; the reviewer verifies the witnesses.

## II.14 Proof-carrying protocols and Cipher integration

Crucible already has a content-addressed, proof-carrying architecture: F*X proved allocators produce certificates (CRUCIBLE.md §10); the Merkle DAG content-addresses computations (CLAUDE.md L7); Cipher stores certificates alongside artifacts (CRUCIBLE.md §9). Session types slot into this architecture — they are first-class citizens of the proof ecosystem, not an orthogonal addition.

**Protocols are content-addressed.** Every session type T has a structural content hash `hash(T)` computed over T's type tree. Two session types with identical structure have identical hashes; hash collisions are negligible under SHA-256. Stored: Cipher L1 tier (IR002-neutral) carries protocol-definition artifacts.

**Certificates are content-addressed.** Every proof `φ(Γ)` produced by the L7 safety-layer (either compile-time-constexpr or offline-mCRL2) generates a CERTIFICATE: a structural proof term witnessing the property. Certificate content hash = `hash(Γ_hash, φ_name, proof_term)`. Stored: Cipher L1, federation-shareable across runs.

**Federation: verify once, ship everywhere.** When two Crucible installations share a Cipher cold tier (§CRUCIBLE.md §9.5 federation), protocol certificates propagate naturally. A certificate proving `φ_df(G_raft)` in cluster A is valid in cluster B provided:
- Raft's `G_raft` protocol content hash matches
- The certificate's φ type matches
- The proof term is readable via Cipher's reflection infrastructure

No re-verification needed. Cross-run optimization: a training run that verifies a collective's liveness writes the certificate once; all subsequent runs of the same topology replay-skip the verification.

**Replay determinism as certificate composition.** The CI test `bit_exact_cross_backend` (CRUCIBLE.md §10.8) asserts: two backends implementing session types T_1, …, T_n (the Crucible channel catalog) and satisfying `φ_BITEXACT(Γ)` produce byte-identical traces. Under this framing, the test verifies:

1. Each backend's implementation satisfies `T_i ⩽ T_i_canonical` (session-type subtyping check, at compile time)
2. The combined Γ satisfies `φ_df ∧ φ_live+ ∧ φ_associated_with_G_training ∧ φ_BITEXACT` (multi-level proof)
3. All proofs' certificates are content-addressed; the CI test checks that certificate hashes match across backends (a DEEPER check than just byte-equality of the traces)

Cross-backend bit-exactness is then a corollary: if both implementations satisfy the same session types with identical φ certificates, their observable traces are bisimilar; under the canonical reduction topology they're byte-identical.

**Certificate generation and extraction.**
- Small Γ (< 100 LTS states): metafunction emits the certificate as `consteval` data; Cipher stores it directly
- Medium Γ (~100-1000 states): `tools/mcrl2_export/` emits mCRL2; `tools/verify.sh` invokes mCRL2; the tool writes a certificate to Cipher L1
- Large Γ or offline proof: human-authored proof sketch, signed hash, archived; CI check verifies the signature + hash match the current Γ

**Cipher L1 entry format for a session-type certificate:**
```
{
  "certificate_kind": "SessionSafety",
  "gamma_hash":       "0x9E37...",
  "phi":              "is_df_v ∧ is_live+_v ∧ is_crash_safe_v<R={leader}>",
  "proof_kind":       "mcrl2_machine_verified" | "compile_time_metafunction" | "manual_attested",
  "proof_term":       "<mcrl2 pbes2bool output, or metafunction evaluation trace, or signed sketch>",
  "verifier_version": "mcrl2-202502.0 | crucible-metaproof-1.2.0",
  "dependencies":     ["0x5B1C...", ...],  // content hashes of subsidiary proofs
  "created_at":       "...",
  "tags":             ["channel=CNTP-Layer-3-Raft", "recipe=bitexact_tc"]
}
```

**The architectural punch line.** Session types are not an auxiliary verification tool. They are the SAME KIND of artifact as kernel certificates, memory-plan proofs, and recipe specifications. Every Crucible correctness claim — allocator safety, kernel optimality, reduction topology, collective determinism, Cipher replication, MPMC producer-consumer safety — is a certificate in Cipher. The system as a whole is a graph of content-addressed artifacts linked by content-addressed certificates. Session types contribute protocol-level certificates to this graph. Nothing is verified twice; everything is re-usable.

---

# Part III — The C++26 realization

The theory in Part II is mechanized as a twelve-layer C++26 stack under `include/crucible/safety/`. Every layer is a header with phantom types, template metafunctions, concepts, contracts, and reflection. No runtime costs in release builds: session-type machinery compiles away entirely. The runtime cost of a typed Send is exactly the cost of the underlying transport (RDMA posted-work entry, MpmcRing try_push, doorbell write) — the typing is phantom.

The chapter: architecture overview (§III.0), file layout and dependency graph (§III.1), compile-time vs runtime split (§III.2), GCC 16 features used and why (§III.3), zero-overhead discipline (§III.4), decidability and fallback (§III.5), per-layer specification L0 through L12 (§III.L0–§III.L12).

## III.0 Architecture at a glance

Twelve headers, dependency-ordered:

```
L0  Permission<Tag>, Linear<T>              (safety/Permission.h, safety/Linear.h — exists)
L1  Session.h                                binary combinators — EXISTS, 700 lines, 24 tests green
L2  SessionContext.h                         typing context Γ
L3  SessionQueue.h                           queue types σ, en-route
L4  SessionGlobal.h                          global types G, projection G↾p
L5  SessionAssoc.h                           association Δ ⊑_s G
L6  SessionSubtype.h                         sync ⩽ and bounded-async ⩽_a
L7  SessionSafety.h                          the φ family, all seven SY19 levels
L8  SessionCrash.h                           Stop, Crash<p>, ⊘, reliability R
L9  PermissionedSession.h                    CSL × session integration
L10 SessionPatterns.h                        pattern library (RequestResponse, Pipeline, 2PC, …)
L11 SessionDiagnostic.h                      manifest-bug tags, compiler diagnostic helpers
L12 SessionChoreography.h                    choreographic projection (v2; sketch in this doc)
```

Dependency graph (arrows point to "depends on"):

```
L12 SessionChoreography ───┐
                           │
L10 SessionPatterns ←──────┤
L11 SessionDiagnostic ←────┤
                           │
L9  PermissionedSession ←──┼── L0 Permission
                           │
L7  SessionSafety ←────────┤
L8  SessionCrash ←─────────┤
L6  SessionSubtype ←───────┤
L5  SessionAssoc ←─────────┤
L4  SessionGlobal ←────────┤
L3  SessionQueue ←─────────┤
L2  SessionContext ←───────┤
L1  Session ──────────────→ L0 Permission, Linear
```

Builds bottom-up: L0–L1 done, L2–L5 are next phase, L6–L9 are the flagship phase that enables MpmcRing, L10–L11 are polish, L12 is v2.

## III.1 File layout

Each layer is ONE header in `include/crucible/safety/`. Headers are self-contained (every `#include` is explicit). Total target size across all twelve: ~8–10K lines. Comparison: the existing safety infrastructure (Permission.h + PermissionFork.h + Linear.h + Refined.h + Tagged.h + Pinned.h + ScopedView.h + Session.h + Once.h + Mutation.h + Ordered.h + Secret.h + ConstantTime.h) is ~5K lines total, so we are roughly doubling the safety header surface.

**Namespace discipline.** All public session-type API under `crucible::safety::session`. Internal helpers under `crucible::safety::session::detail`. No leakage into `crucible::` top-level.

**Include graph constraints.** No cycles. Every layer depends only on strictly-lower layers (and the non-session safety primitives). L12 is the highest; it may include anything. L0 is the lowest; it includes only `<type_traits>`, `<concepts>`, `<utility>`, and `Platform.h`.

**Test layout.** `test/session/test_session_<layer>.cpp`, one file per layer. Negative-compile tests (ill-typed programs that MUST NOT compile) go in `test/safety_neg/session_<layer>_neg.cpp` (per task #146). Property-based tests for subtyping and projection go in `test/session/test_session_properties.cpp`. μ-calculus verification harness (mCRL2 driver) is out-of-tree: `tools/mcrl2_export/` emits mCRL2 `.mcrl2` files from C++ Γ's, then `tools/verify.sh` runs `lps2pbes` and `pbes2bool` from mCRL2.

## III.2 Compile-time vs runtime split

The discipline:

| Concern | Where checked | How |
|---|---|---|
| Well-formedness of a session type | Compile time | `is_well_formed_v<T>` template metafunction |
| Duality check for binary composition | Compile time | `static_assert(std::is_same_v<T, dual_of_t<U>>)` |
| Γ composition domain disjointness | Compile time | `compose_t<Γ₁, Γ₂>` fails to instantiate on overlap |
| Projection G↾p | Compile time | `project_t<G, Role>` template metafunction |
| Association Δ ⊑_s G | Compile time (bounded) | `is_associated_v<Δ, G>` via subtyping-check metafunction |
| Sync subtyping | Compile time | `is_subtype_sync_v<T, U>` by structural recursion |
| Precise async subtyping ⩽_a | Compile time (bounded depth) | `is_async_subtype_v<T, U, Depth=64>` via SISO decomposition + 6 rules |
| φ_safe, φ_df, φ_live | Compile time (decidable on bounded Γ) | μ-calculus metafunction on Γ's LTS |
| φ_live+, φ_live++ | Compile time (decidable) | Model-checking on LTS with fairness constraints |
| Crash-branch presence for unreliable peer | Compile time | `requires_crash_handling_v<Γ, R>` on every Offer |
| Permission balance (CSL) | Compile time | `is_permission_balanced_v<Γ, Perms>` metafunction |
| Protocol-adherent send/recv order | Runtime | Handle type-state transitions; wrong-order send is a compile error via different handle type |
| Actual message transmission | Runtime | Underlying transport (MpmcRing, RDMA, doorbell) |
| Crash DETECTION | Runtime | CNTP Layer 1 RDMA completion errors, Layer 2 SWIM confirmed-dead, Raft timeout |
| Queue state σ | Runtime (atomics) | MpmcRing's cell states, per-cell sequence counters |
| Liveness guarantee | Runtime (fair scheduler) | SCHED_DEADLINE on HOT threads, fair round-robin on WARM, io_uring SQPOLL for WARM I/O |

Compile-time checks ENFORCE structural correctness. Runtime enforces operational correctness. The two are complementary: you can't have wrong-ordered sends (structural) but you can have a peer fail to send (operational). The protocol's crash-branch makes the latter a first-class protocol event, not a runtime exception.

## III.3 GCC 16 features used

Session types on GCC 16 leverage:

**Contracts (P2900R14, code_guide.md §II).** Every factory function taking raw pointers or spans has `pre(ptr != nullptr)` or `pre(span.size() > 0)`. The handle constructors' contracts check resource preconditions (e.g., MpmcRing not in drained state). Under `enforce` they abort at contract violation; under `ignore` (hot-path TUs) they compile to `[[assume]]` hints.

**Reflection (P2996R13).** Used for:
- Enumerating members of a session type for structural equality and subtyping checks
- Auto-generating `dual_of_t` via type-level map over the session-type AST
- Emitting mCRL2 specifications from a C++ Γ (via `std::meta::nonstatic_data_members_of` on Entry structs)
- Producing human-readable diagnostics via `std::meta::display_string_of` on failed subtype checks

**Expansion statements (P1306R5).** `template for (constexpr auto entry : members_of<Γ>)` iterates the Γ's entries at compile time. Used in association checks (for each role, verify the projection), crash-branch completeness (for each unreliable peer in Γ, verify every Offer receiving from them has a Crash branch), and permission-balance checks (for each communication event, verify permissions flow correctly).

**`constexpr` exceptions (P3068R5).** The IR verifier throws `constexpr` exceptions on malformed types; these manifest as compile errors with structured diagnostic messages. Zero runtime cost.

**Partial program correctness (P1494R5).** Contract violations call `std::terminate`, not UB. This makes session-type violations observable and testable.

**`define_static_array` (P3491R3).** Used in SISO decomposition: the decomposed SISO trees are a `define_static_array<SISOTree>` at compile time, queryable for subtyping tests.

**`std::meta::reflect_hash`** (used in Crucible's reflection utilities). Each session type has a structural content hash; two types with identical structure hash to the same value. Used for content-addressed protocol caching (Cipher federation of session types across runs).

## III.4 Zero-overhead discipline

Session types must vanish under optimization. Four practices:

**(1) Phantom types.** Every template parameter that doesn't influence layout is phantom. A `SessionHandle<Proto, Resource, LoopCtx>` has exactly the fields of `Resource` plus possibly a small indirection; `Proto` and `LoopCtx` are compile-time only.

**(2) `[[no_unique_address]]` on per-state marker members.** The handle's current-state marker (a unit type) has size 0 in most instantiations; `[[no_unique_address]]` collapses it.

**(3) `gnu::always_inline` on state-transition methods.** `handle.send(msg)` must inline. The transition between handle types is pure type-level; at runtime it's a function call that is always inlined away.

**(4) No virtual functions.** Session-type dispatch is compile-time polymorphism. There is no runtime dispatch table, no vtable, no type-erasure overhead.

Tests: `test/perf/bench_session_dispatch.cpp` measures `handle.send(msg)` cost; the target is 0 ns vs the underlying transport cost. Any non-zero delta is a regression — we have a `bench::Run::hardening(Policy::production())` bound that catches this.

## III.5 Decidability strategy and fallback

Precise async subtyping ⩽_a is undecidable in general. Crucible's strategy:

**Default — per-channel bounded depth.** `is_async_subtype_v<T, U>` bounds SISO-tree expansion at `Depth`, which is a template parameter with a sensible default of 64 but OVERRIDABLE per channel. Each channel can declare its required depth as a trait:

```cpp
template <typename Proto> struct ProtoSubtypeDepth {
    static constexpr std::size_t value = 64;  // default
};
// Specialize for channels with special depth requirements
template <> struct ProtoSubtypeDepth<MpmcProtocol<32, 16, Job>> {
    static constexpr std::size_t value = 128;  // large-N MPMC needs more
};
```

The subtyping check uses this:
```cpp
template <typename T, typename U>
inline constexpr bool is_async_subtype_v =
    is_async_subtype_v<T, U, ProtoSubtypeDepth<T>::value>;
```

If the decision procedure reaches the declared depth without concluding, the query is REJECTED (we conservatively say "not a subtype"). The engineer can:
- Override the depth at the call site: `is_async_subtype_v<T, U, 256>`
- Declare a higher `ProtoSubtypeDepth<MyProto>::value`
- Fall back to synchronous subtyping: `is_subtype_sync_v<T, U>` (always decidable)
- Rewrite the protocol to admit shallower refinement

**Diagnostic on rejection.** When subtyping is rejected, the diagnostic reports which SISO path was the blocker and what depth bound was reached. This lets the engineer choose intelligently between deeper search and restructuring.

**Offline verification.** For protocols beyond the default depth, the `tools/mcrl2_export/` pipeline emits mCRL2 specifications; the engineer runs `verify.sh` to get a full decision (mCRL2 scales past what we can do in-template). The result is persisted as a proof certificate in Cipher; the next compile reads the certificate and `static_assert`s the result.

**What never fails.** Well-formedness, sync subtyping, projection, crash-branch completeness for bounded participant sets — all strictly decidable and complete within template-metaprogramming cost.

## III.L0 — Linear-π foundation

Pre-session-types. Already exists in `include/crucible/permissions/Permission.h`, `Linear.h`, `Pinned.h`, `ScopedView.h`. Summary of what session types assume from below:

- `Permission<Tag>`: unique ownership token; phantom except in the CSL ownership-transfer sense. `split`, `combine`, `fork` operations.
- `Linear<T>`: move-only wrapper ensuring `T`-typed resources are consumed exactly once.
- `Pinned<T>`: non-movable wrapper for identity-bearing resources (channels live here).
- `ScopedView<Carrier, State>`: typestate machine for objects with protocol-like state transitions. Sessions are a generalization to multi-party protocols; ScopedViews are unary.

Session types PROMOTE `ScopedView`'s unary typestate to N-ary protocol state. The same compile-time enforcement machinery (phantom State parameter, state transitions return new-typed handles) scales up.

## III.L1 — Binary combinators (EXISTS)

`include/crucible/sessions/Session.h`. **1185 lines** (as of 2026-04-24, up from the 700-line estimate when this section was first drafted; growth driven by SessionHandleBase CRTP for abandonment detection #349 and release-mode EBO machinery #366), 102/102 tests green on GCC 16 (full suite, excluding pre-existing test_safety legacy). Public surface:

```cpp
namespace crucible::safety::session {

// Combinators (class templates; types are opaque)
template <typename P, typename K> struct Send;
template <typename P, typename K> struct Recv;
template <typename... Bs>          struct Select;   // B is a Branch<Label, T>
template <typename... Bs>          struct Offer;
template <typename K>              struct Loop;
struct Continue;
struct End;

// Labeled branches for Select/Offer
template <typename Label, typename T> struct Branch;

// Duality
template <typename T> struct DualOf;
template <typename T> using dual_of_t = typename DualOf<T>::type;

// Composition
template <typename T, typename S> struct Compose;
template <typename T, typename S> using compose_t = typename Compose<T, S>::type;

// Well-formedness predicate (compile-time)
template <typename T> inline constexpr bool is_well_formed_v = /* structural */;

// Runtime handle
template <typename Proto, typename Resource, typename LoopCtx = EmptyLoopStack>
class SessionHandle;

// Factory
template <typename Proto, typename Resource>
auto make_session_handle(Resource r) -> SessionHandle<Proto, Resource>;

// Establish dual pair
template <typename Proto, typename Resource>
auto establish_channel(Resource r)
    -> std::pair<SessionHandle<Proto, Resource>, SessionHandle<dual_of_t<Proto>, Resource>>;

} // namespace
```

Per-combinator handle specializations live in `Session.h`; each specializes `SessionHandle<Proto, Resource, LoopCtx>` for one Proto shape and exposes the methods appropriate to that state. Sending on a Send-state handle returns a Recv-or-other-state-handle; the compiler forbids wrong-order operations by having no such method on the current type.

This layer is complete. Phase 2 starts at L2.

## III.L1.5 — Handle mechanics: lifetime, moves, destruction, RAII

The SessionHandle is the runtime representative of a typed conversation. Its mechanics are the load-bearing C++ interface for every session-typed channel. Done wrong, they are the pain point that collapses the whole framework in practice. Done right, the compiler enforces protocol adherence and RAII cleans up any premature abandonment safely.

**Copy semantics: always deleted.** A session handle represents a unique position in a protocol; copying would mean two parties believe they own the same position, which violates protocol linearity. Deletion by `= delete` with a reason:

```cpp
template <typename Proto, typename Resource, typename LoopCtx>
class SessionHandle {
    // …
public:
    SessionHandle(SessionHandle const&) = delete;
    SessionHandle& operator=(SessionHandle const&) = delete;
    // reason: "session position is linear; two handles cannot share a position"
};
```

**Move semantics: default, noexcept.** Moving transfers the session position from one handle-value to another. The moved-from handle enters a "consumed" state; subsequent use is a compile-time error (enforced via Linear<T>'s mechanism from the safety/ layer):

```cpp
SessionHandle(SessionHandle&&) noexcept = default;
SessionHandle& operator=(SessionHandle&&) noexcept = default;
```

Moves are the RAII mechanism for handing off a session from one thread/function/scope to another. Example: Vessel dispatch's handle is constructed, used for the duration of one op capture, then moved into the bg thread's drain queue.

**Destruction: requires End or Stop.** A handle whose static type is NOT `SessionHandle<End, …>` or `SessionHandle<Stop, …>` must NOT reach destruction in the current phase of execution. If it does, the protocol is abandoned mid-flight, which:

- Leaks the counterparty's expectations (they may wait forever)
- Violates CSL permission balance (transferred permissions may not arrive)
- Breaks crash-stop: abandonment is NOT a crash; the counterparty has no way to know

The mechanism: **compile-time detection via Linear<T>.** `SessionHandle` is implicitly a `Linear<…>` — its consumption-track is its protocol-advance. If the handle is destroyed without reaching End, the destructor emits a compile-warning (under the `-Werror=session-abandoned` preset) or a runtime abort (under debug builds) or silently leaks (release builds — user-responsibility mode).

```cpp
template <typename Proto, typename Resource, typename LoopCtx>
class SessionHandle {
    // Destructor — checks that Proto is End or Stop
    ~SessionHandle() {
        if constexpr (!is_terminal_state_v<Proto>) {
            #ifndef NDEBUG
                std::terminate();  // abandoned mid-protocol in debug
            #endif
            // release builds: warning emitted at instantiation point, not at runtime
        }
    }
};
```

**Moved-from handles.** After `auto h2 = std::move(h1);`, `h1` is in a moved-from state. Its Proto is NOT `End`; its destructor must not fire the abandoned-protocol check. Solution: use `Linear<T>`'s sentinel pattern — moved-from handles have a `consumed` flag; destructor short-circuits on that flag.

**Crash injection.** If the runtime detects a peer crash (CNTP Layer 1 completion error, SWIM confirmed-dead), ANY handle currently engaged with that peer transitions to `SessionHandle<Stop, …>`. This is a RUNTIME transition, not a compile-time one; the static type is unchanged but the runtime state is "crashed". Subsequent operations on the handle return CrashEvent; the user's code handles them via the protocol's Crash branches (L8).

```cpp
template <typename Proto, typename Res, typename LoopCtx>
auto SessionHandle<Proto, Res, LoopCtx>::send(auto msg)
    -> std::expected<SessionHandle<NextProtoAfterSend, Res, LoopCtx>, CrashEvent>;
```

Return type is `std::expected` — either the next-state handle or a CrashEvent documenting which peer died. The user must either handle the crash branch or propagate via the protocol.

**Lifetime anchoring.** Every SessionHandle has a Resource field (Pinned<Channel> or similar). The handle's lifetime does NOT own the Resource; it BORROWS a slot of the Resource. Resource's destruction must outlive all handles derived from it, enforced by RAII discipline + `lifetime_bound` attribute on handle-producing functions (per CRUCIBLE's code_guide.md).

**Example: correct usage pattern.**
```cpp
void do_query(QueryRequest const& req, Keeper& keeper) {
    // Establish a session. Resource outlives the handle.
    auto [client, server] = establish_channel<QueryProto>(keeper.get_channel());

    // Step 1: send request. client is consumed; new client2 has next-state type.
    auto client2 = std::move(client).send(req);
    if (!client2) {
        // peer crashed before we could send; client2 is unexpected(CrashEvent)
        handle_crash(client2.error());
        return;  // client is moved-from; destructor short-circuits
    }

    // Step 2: receive response. client2 is consumed.
    auto [resp, client3] = std::move(*client2).recv();
    if (!client3) { handle_crash(client3.error()); return; }

    use_response(resp);
    // client3 is SessionHandle<Continue, …>; destructor fires abandoned check
    // — in this case, RequestResponse's loop means we should loop back
    // rather than destruct. Our compile-time check flags this as a bug.
}
```

The abandoned-check catches bugs of the form "forgot to loop back to Continue" or "forgot to call close() before function exit." Release builds either abort (debug) or silently warn (user-trusted).

**Handle size and overhead.** A SessionHandle's sizeof is `sizeof(Resource*) + sizeof(CrashStatus_flag)` — ~9 bytes with alignment. `[[no_unique_address]]` on empty-type phantom fields (Proto, LoopCtx) ensures size-0 phantom. Copy/move/destruction are all trivially generated (no user-defined) aside from the abandoned check. In optimized builds, the abandoned check's compile-time branch is elided if the static type is `End`/`Stop`; for non-terminal types it emits at most one branch to `std::terminate` which is `[[gnu::cold]]` and out of hot-path cache lines.

**Test pattern.**
```cpp
// test/safety_neg/test_handle_abandoned.cpp  (must NOT compile)
void abandoned_protocol() {
    auto [c, s] = establish_channel<RequestResponse<Req, Resp>>(res);
    // Never send. Return. c's destructor must fire abandonment check.
    // Under -Werror=session-abandoned, this is a compile error.
}  // expected: error: 'SessionHandle<…>' abandoned without reaching End or Stop
```

## III.L2 — Typing context Γ (SessionContext.h)

`include/crucible/sessions/SessionContext.h`. Core abstraction: a compile-time set of (session_tag, role_tag) → local_type entries.

```cpp
namespace crucible::safety::session {

// An entry: (session, role, local_type) tagged triple
template <typename SessionTag, typename RoleTag, typename T>
struct Entry {
    using session = SessionTag;
    using role    = RoleTag;
    using type    = T;
};

// Context: a type-level list of Entry's. Enforced disjoint.
template <typename... Entries>
struct Context {
    static_assert(
        detail::all_keys_distinct_v<Entries...>,
        "Γ entries must have pairwise-distinct (session, role) keys"
    );
};

// Empty context
using EmptyContext = Context<>;

// Disjoint-union composition
template <typename Γ1, typename Γ2>
struct ComposeContext;
template <typename Γ1, typename Γ2>
using compose_context_t = typename ComposeContext<Γ1, Γ2>::type;
// Fails (via static_assert) if Γ1 and Γ2 share any (session, role) key

// Lookup
template <typename Γ, typename SessionTag, typename RoleTag>
struct LookupContext;
template <typename Γ, typename S, typename R>
using lookup_context_t = typename LookupContext<Γ, S, R>::type;

// Domain (set of (session, role) pairs)
template <typename Γ>
struct DomainOf;
template <typename Γ>
using domain_of_t = typename DomainOf<Γ>::type;

// Context reduction: Γ advances by one communication event α
template <typename Γ, typename Event>
struct ReduceContext;
template <typename Γ, typename E>
using reduce_context_t = typename ReduceContext<Γ, E>::type;

// Events
template <typename SessionTag, typename FromRole, typename ToRole,
          typename Label, typename Payload>
struct SendEvent;
template <typename SessionTag, typename FromRole, typename ToRole,
          typename Label, typename Payload>
struct RecvEvent;

} // namespace
```

**Enumeration of reachable contexts.** Given an initial Γ_0, the set of reachable Γ's via all possible events is the LTS Crucible uses for φ-checking. Compile-time enumeration via BFS:

```cpp
template <typename Γ0, std::size_t MaxFuel = 1024>
struct ReachableContexts {
    using type = /* BFS closure computed at compile time */;
};
```

Fuel bound keeps the BFS bounded; for Crucible's Γ's (bounded participants, bounded recursion unfolding, bounded queue depth) the reachable set is finite and small (rarely > 1000 states).

**Γ printing (reflection-based).** `display_context<Γ>()` emits a human-readable Γ for diagnostics. Uses `std::meta::display_string_of` on each Entry's components.

## III.L3 — Queue types σ (SessionQueue.h)

`include/crucible/sessions/SessionQueue.h`. Queue types model the in-flight messages between two participants on an async channel.

```cpp
namespace crucible::safety::session {

// A queued message
template <typename From, typename To, typename Label, typename Payload>
struct QueuedMsg;

// Queue: right-append sequence of queued messages
template <typename... Msgs>
struct Queue;

using EmptyQueue = Queue<>;

// Operations
template <typename Q, typename M>
struct EnqueueQueue;
template <typename Q, typename M>
using enqueue_queue_t = typename EnqueueQueue<Q, M>::type;

template <typename Q>
struct HeadQueue;
template <typename Q>
using head_queue_t = typename HeadQueue<Q>::type;

template <typename Q>
struct TailQueue;
template <typename Q>
using tail_queue_t = typename TailQueue<Q>::type;

// Unavailable queue (crashed recipient)
struct UnavailableQueue;

// Context with queues
template <typename Γ, typename... QueueEntries>
struct AsyncContext;

// Queue-aware reduction
template <typename AsyncΓ, typename Event>
struct ReduceAsyncContext;

// Balanced+ check — bounded en-route count
template <typename AsyncΓ, std::size_t Capacity>
inline constexpr bool is_balanced_plus_v = /* bounded queue depth along all LTS paths */;

} // namespace
```

**Queue-type subtyping.** Under asynchrony, sending and receiving can be reordered, which affects the queue type. The subtyping check treats queue states as part of the LTS state; any subtype relation must preserve queue-state relationships across the reorderings admitted by ⩽_a.

**Bounded-queue invariant.** Crucible's runtime has all queues bounded (compile-time capacity). The type-level queue respects this: `is_balanced_plus_v` verifies the capacity is never exceeded along any reachable path. If the capacity would be exceeded, the protocol is ill-typed — the engineer must either increase the capacity or restructure the protocol.

## III.L4 — Global types G, projection (SessionGlobal.h)

`include/crucible/sessions/SessionGlobal.h`. The bird's-eye protocol syntax.

```cpp
namespace crucible::safety::session::global {

// Global types
struct End_G;
template <typename From, typename To, typename Label, typename Payload, typename G>
struct Transmission;                       // p → q : m(P) . G
template <typename From, typename To, typename... LabeledGs>
struct Choice;                             // p → q : {m_i(P_i) . G_i}
template <typename Var, typename Body> struct Rec;    // μt . G
template <typename Var>                struct Var;    // t

// Crash-stop extensions
template <typename P>                  struct StopG;  // p has crashed
template <typename From, typename To, typename G>
struct CrashBranch;                    // "if From crashes, continue as G"

// Projection
template <typename G, typename Role>
struct Project;
template <typename G, typename Role>
using project_t = typename Project<G, Role>::type;

// Merge operation (for projection when neither sender nor receiver is Role)
template <typename... T>
struct MergeLocalTypes;
template <typename... T>
using merge_local_types_t = typename MergeLocalTypes<T...>::type;

// Coinductive full merging (PMY25 Def 4.3)
template <typename... T>
struct CoinductiveFullMerge;

// Roles extraction
template <typename G>
struct RolesOf;
template <typename G>
using roles_of_t = typename RolesOf<G>::type;

// Well-formedness
template <typename G> inline constexpr bool is_global_well_formed_v = /* … */;

// Balanced+ check at global type level (PMY25 Def 6.1)
template <typename G, std::size_t MaxEnRoute>
inline constexpr bool is_global_balanced_plus_v = /* … */;

} // namespace
```

**Projection-by-reflection.** Walking G's type tree at compile time uses expansion statements. For each G variant:
- `End_G` projects to `End`
- `Transmission<P, Q, L, Pld, G>` projects to `Select<Branch<L, Send<Pld, project_t<G, P>>>>` for P, `Offer<Branch<L, Recv<Pld, project_t<G, Q>>>>` for Q, `project_t<G, R>` for other R
- `Choice<P, Q, LabeledGs...>` projects to a compound Select/Offer depending on role; for third-party R uses `merge_local_types_t` over the branch projections
- `Rec<Var, Body>` projects to `Loop<project_t<Body, Role>>`
- `Var<V>` projects to `Continue`
- `StopG<P>` projects to `Stop` for P, continues for others

**Full merging (PMY25 §4).** When projecting a choice G = `Choice<P, Q, LabeledGs...>` onto a role R ≠ P, Q, each branch projects to a local type; the merge combines them. Classical plain merging requires identical projections. Coinductive full merging (PMY25) allows some structural divergence if R can distinguish branches later. Crucible's `CoinductiveFullMerge` implements the sound fragment of full merging.

## III.L5 — Association Δ ⊑_s G (SessionAssoc.h)

`include/crucible/sessions/SessionAssoc.h`. The invariant tying Γ to a global type.

```cpp
namespace crucible::safety::session {

// Association: Δ ⊑_s G
template <typename Δ, typename G, typename SessionTag>
struct IsAssociated {
    static constexpr bool value =
        detail::dom_matches<Δ, G, SessionTag>::value &&
        detail::all_subtypes<Δ, G, SessionTag>::value;
};

template <typename Δ, typename G, typename SessionTag>
inline constexpr bool is_associated_v = IsAssociated<Δ, G, SessionTag>::value;

// Derived: association is preserved under reduction
template <typename Δ, typename G, typename SessionTag, typename Event>
struct AssociatedAfterReduction;

// Association preservation theorem, compile-time check
template <typename Δ, typename G, typename SessionTag>
using associated_preservation_witness = /* … */;

} // namespace
```

**What association buys.** Once established, any property (safety, deadlock-freedom, liveness) that holds for G transfers to Δ. The engineer writes G, proves G satisfies their chosen φ (often by mCRL2 offline check), then verifies association with Δ — a cheaper subtyping check. No need to re-prove φ on Δ itself.

**Bottom-up fallback.** If no G is natural (e.g., TraceRing's local-type pair), we skip association and check φ directly on Δ via the L7 machinery. Both paths are supported; the doc per-channel in Part IV says which to use.

## III.L6 — Subtyping: sync ⩽ and bounded async ⩽_a (SessionSubtype.h)

`include/crucible/sessions/SessionSubtype.h`. Both subtyping relations in one header, with shared infrastructure.

```cpp
namespace crucible::safety::session {

// ── Synchronous Gay-Hole subtyping ─────────────────────────────
template <typename T, typename U> struct IsSubtypeSync;
template <typename T, typename U>
inline constexpr bool is_subtype_sync_v = IsSubtypeSync<T, U>::value;

// Partial specializations for each combinator pair:
//   Send ⩽ Send: covariant in payload (contravariant-rejected), covariant in continuation
//   Recv ⩽ Recv: contravariant in payload, covariant in continuation
//   Select ⩽ Select: subtype has ⊆ labels
//   Offer ⩽ Offer: subtype has ⊇ labels
//   Loop ⩽ Loop: body under coinductive hypothesis
//   End ⩽ End
//   Stop ⩽ anything

// ── Precise async subtyping via SISO decomposition ─────────────
template <typename T>
struct SISODecomposition {
    using so_trees  = /* Single-Output forest */;
    using si_trees  = /* Single-Input forest */;
    using siso_trees = /* SI ∩ SO forest */;
};

template <typename T>
using siso_of_t = typename SISODecomposition<T>::siso_trees;

// Six refinement rules on SISO trees
template <typename W1, typename W2, std::size_t FuelLeft>
struct RefinesWith;  // W1 ⊑ W2 under depth budget FuelLeft

// Async subtyping via SISO forest relation
template <typename T, typename U, std::size_t Fuel = 64>
struct IsSubtypeAsync;
template <typename T, typename U, std::size_t Fuel = 64>
inline constexpr bool is_subtype_async_v = IsSubtypeAsync<T, U, Fuel>::value;

// Subtype-rejection diagnostic: which SISO tree was the blocker
template <typename T, typename U, std::size_t Fuel>
struct SubtypeRejectionReason;

// Unified subtyping: try sync first; async-sync fall back on same-shape; precise async last
template <typename T, typename U, std::size_t Fuel = 64>
struct IsSubtype {
    static constexpr bool value =
        std::is_same_v<T, U>                       ||
        is_subtype_sync_v<T, U>                    ||
        is_subtype_async_v<T, U, Fuel>;
};

// Subsorting on value types (for payload subtyping)
template <typename P1, typename P2>
struct IsSubsort;
template <typename P1, typename P2>
inline constexpr bool is_subsort_v = IsSubsort<P1, P2>::value;

} // namespace
```

**Performance cost.** Sync subtyping is O(|T| + |U|) template-instantiation cost per check. Async at depth D is O((|T| + |U|) × D × branching_factor^D) — exponential in depth, bounded by 64 in practice. For Crucible's protocols (≤ 20 states per local type, branching ≤ 4), the cost is tens of milliseconds per check, absorbed by the ~30 second cold build.

**Failure diagnostics.** `SubtypeRejectionReason<T, U, 64>::message` produces a human-readable string identifying which SISO path blocked the subtype. Example: `"T's SISO tree [Send<Msg1, End>] is not a subtype of any SISO tree of U; closest match 'Send<Msg2, End>' differs in payload type"`. Emitted via `static_assert` when subtyping is required but fails.

## III.L6.5 — SISO decomposition in C++26 templates

The mechanical crux of precise async subtyping. Implementation sketch for the L6 metafunctions, operational enough that a contributor can take this spec and produce working C++ without reading GPPSY23.

**Type-list primitive.** SISO trees are represented as a type-level list of a single-path session type:

```cpp
namespace detail {

// A SISO tree is a list of directed events (Send or Recv) ending in End.
// Concrete type: a compile-time sequence.
template <typename... Events>
struct SISOTree;

template <typename From, typename To, typename Payload>
struct SendEvent;
template <typename From, typename To, typename Payload>
struct RecvEvent;

struct EndMarker;

using ExampleSISO = SISOTree<
    SendEvent<Alice, Bob, Req>,
    RecvEvent<Alice, Bob, Ack>,
    EndMarker
>;

} // namespace detail
```

**Step 1 — SO decomposition (single-output).** Walk T's type tree; at every `Select<…>` node, DUPLICATE the current SISO-tree-in-progress and emit one copy per branch. At every `Offer<…>` node, keep a SINGLE copy (offers don't branch the output tree). At `Send`, `Recv`, `Loop`, `Continue`, `End`, append the appropriate marker.

```cpp
namespace detail {

// Base: End leaf
template <typename T> struct SODecompose;

template <> struct SODecompose<End> {
    using type = std::meta::type_list<SISOTree<EndMarker>>;
};

template <typename P, typename K>
struct SODecompose<Send<P, K>> {
    using sub_trees = typename SODecompose<K>::type;
    using type = prepend_to_each_t<SendEvent<Self, Peer, P>, sub_trees>;
};

template <typename P, typename K>
struct SODecompose<Recv<P, K>> {
    using sub_trees = typename SODecompose<K>::type;
    using type = prepend_to_each_t<RecvEvent<Peer, Self, P>, sub_trees>;
};

// Select: union of decompositions of each branch
template <typename... Bs>
struct SODecompose<Select<Bs...>> {
    using type = concat_t< typename SODecompose<branch_type_t<Bs>>::type... >;
};

// Offer: SINGLE branch (offers don't split output); pick the first branch for SO
// (The SI decomposition splits on offers instead)
template <typename B, typename... Bs>
struct SODecompose<Offer<B, Bs...>> {
    using type = typename SODecompose<branch_type_t<B>>::type;
};

template <typename K>
struct SODecompose<Loop<K>> {
    // Loops represented as a rolled-up marker; unrolled by refinement check
    using type = typename SODecompose<K>::type;
};

// Helpers
template <typename Event, typename List> struct PrependToEach;
template <typename... Lists>            struct Concat;
template <typename List>                using prepend_to_each_t = typename PrependToEach<Event, List>::type;
template <typename... L>                using concat_t = typename Concat<L...>::type;

} // namespace detail
```

**Step 2 — SI decomposition (single-input).** Dual: at every `Offer<…>`, split per branch; at `Select<…>`, keep a single copy.

**Step 3 — The six refinement rules on SISO trees.** A metafunction `RefineStep<T1, T2, Fuel>` checks whether two SISO trees' first events are in the refinement relation, then recurses on the tails.

```cpp
namespace detail {

// Refinement rules: [ref-in], [ref-out], [ref-A], [ref-B], [ref-end], [ref-subsort]

// [ref-end]: matching End
template <>
struct RefineStep<SISOTree<EndMarker>, SISOTree<EndMarker>, std::size_t Fuel> {
    static constexpr bool value = true;
};

// [ref-in]: matching Recv from same source
template <typename P1, typename P2, typename... R1, typename... R2, std::size_t Fuel>
struct RefineStep<
    SISOTree<RecvEvent<X, Y, P1>, R1...>,
    SISOTree<RecvEvent<X, Y, P2>, R2...>,
    Fuel
> {
    static constexpr bool value =
        is_subsort_v<P1, P2> &&
        RefineStep<SISOTree<R1...>, SISOTree<R2...>, Fuel - 1>::value;
};

// [ref-out]: matching Send to same target
template <typename P1, typename P2, typename... R1, typename... R2, std::size_t Fuel>
struct RefineStep<
    SISOTree<SendEvent<X, Y, P1>, R1...>,
    SISOTree<SendEvent<X, Y, P2>, R2...>,
    Fuel
> {
    static constexpr bool value =
        is_subsort_v<P1, P2> &&
        RefineStep<SISOTree<R1...>, SISOTree<R2...>, Fuel - 1>::value;
};

// [ref-A]: anticipate a Recv past an A-sequence (Recvs from OTHER participants)
//         the subtype recvs from X earlier than supertype
template <typename... R1, typename AnticipatedRecv, typename... A, typename... R2,
          std::size_t Fuel>
struct RefineStep<
    SISOTree<AnticipatedRecv, R1...>,
    SISOTree<A..., AnticipatedRecv, R2...>,
    Fuel
> {
    static constexpr bool value =
        (Fuel > 0) &&
        all_disjoint_sources<AnticipatedRecv, A...>::value &&
        RefineStep<SISOTree<R1...>, SISOTree<A..., R2...>, Fuel - 1>::value;
};

// [ref-B]: anticipate a Send past a B-sequence (Recvs + Sends to OTHER participants)
template <typename... R1, typename AnticipatedSend, typename... B, typename... R2,
          std::size_t Fuel>
struct RefineStep<
    SISOTree<AnticipatedSend, R1...>,
    SISOTree<B..., AnticipatedSend, R2...>,
    Fuel
> {
    static constexpr bool value =
        (Fuel > 0) &&
        all_disjoint_targets<AnticipatedSend, B...>::value &&
        RefineStep<SISOTree<R1...>, SISOTree<B..., R2...>, Fuel - 1>::value;
};

// [ref-subsort]: the implicit sort-subtyping is applied at each Recv/Send
// handled inline via is_subsort_v<P1, P2>

// Catch-all: no rule applies → rejection
template <typename T1, typename T2, std::size_t Fuel>
struct RefineStep { static constexpr bool value = false; };

} // namespace detail
```

**Step 4 — Full subtype via SISO decomposition.**

```cpp
template <typename T, typename U, std::size_t Fuel>
struct IsSubtypeAsync {
    using T_SISO = typename SODecompose<T>::type;
    using U_SISO = typename SIDecompose<U>::type;

    static constexpr bool value =
        std::meta::all_of<T_SISO>([]<typename t>() {
            return std::meta::any_of<U_SISO>([]<typename u>() {
                return RefineStep<t, u, Fuel>::value;
            });
        });
};
```

"For every SO path through T, there exists an SI path through U such that the SISO refinement holds." This is the GPPSY23 formulation.

**Step 5 — Fuel exhaustion diagnostic.** When `Fuel` reaches zero mid-check, `RefineStep` returns false. The caller distinguishes "proven false" from "fuel-exhausted" by rerunning with larger fuel; if both return false, it's a genuine non-subtype. If the larger-fuel run proves true, depth was the bottleneck.

**Compile-time cost.** For concrete types of size ~10 combinators each, `IsSubtypeAsync<T, U, 64>` instantiates ~1000-5000 template specializations in ~10-50 ms of compile time. For the MPMC protocol's projections, cost is ~30 ms. Order of magnitude acceptable; caching via reflection+constexpr hashing across compile instantiations keeps repeated checks near-zero.

**Why this is in the document.** The SISO machinery is the single hardest piece of template metaprogramming in the whole stack. Without this concrete shape, a contributor implementing L6 would have to re-derive the mapping from GPPSY23 to C++ templates from scratch. With this, they have a recipe.

## III.L7 — Parametric safety φ (SessionSafety.h)

`include/crucible/safety/SessionSafety.h`. The seven SY19 safety levels as compile-time predicates.

```cpp
namespace crucible::safety::session {

// LTS-level safety predicates on Γ
template <typename Γ>
struct IsSafe {
    static constexpr bool value =
        detail::all_reachable_contexts_have_matching_io<Γ>::value;
};
template <typename Γ> inline constexpr bool is_safe_v = IsSafe<Γ>::value;

template <typename Γ>
struct IsDeadlockFree {
    static constexpr bool value =
        is_safe_v<Γ> &&
        detail::every_reachable_state_can_reduce_or_is_end<Γ>::value;
};
template <typename Γ> inline constexpr bool is_df_v = IsDeadlockFree<Γ>::value;

template <typename Γ>
struct IsLive {
    static constexpr bool value =
        is_df_v<Γ> &&
        detail::every_pending_io_eventually_fires<Γ>::value;
};
template <typename Γ> inline constexpr bool is_live_v = IsLive<Γ>::value;

template <typename Γ> inline constexpr bool is_live_plus_v       = /* under fair sched */;
template <typename Γ> inline constexpr bool is_live_plus_plus_v  = /* under any sched */;

template <typename Γ> inline constexpr bool is_terminating_v     = /* μ-calculus formula */;
template <typename Γ> inline constexpr bool is_nonterminating_v  = /* … */;

// Association-based compound φ
template <typename Γ, typename G, typename SessionTag>
inline constexpr bool is_associated_v = IsAssociated<Γ, G, SessionTag>::value;

// Precise async subtype preservation (PMY25 liveness theorem)
template <typename Γ, std::size_t Fuel = 64>
inline constexpr bool is_precise_async_safe_v = /* … */;

// Composition helpers
template <typename Γ, template <typename> class... Phis>
struct AllOf { static constexpr bool value = (Phis<Γ>::value && ...); };

// μ-calculus AST (for user-defined φ)
struct True_F;
struct False_F;
template <typename F>                      struct Not_F;
template <typename F1, typename F2>        struct And_F;
template <typename F1, typename F2>        struct Or_F;
template <typename Action, typename F>     struct Diamond_F;
template <typename Action, typename F>     struct Box_F;
template <typename Var, typename F>        struct Mu_F;
template <typename Var, typename F>        struct Nu_F;

template <typename Γ, typename Formula>
struct ModelCheck;
template <typename Γ, typename Formula>
inline constexpr bool model_check_v = ModelCheck<Γ, Formula>::value;

} // namespace
```

**The μ-calculus evaluator.** `ModelCheck<Γ, Formula>` implements a straightforward fixed-point evaluator on the LTS derived from Γ. Bounded by the LTS size. For Crucible's Γ's (typically < 100 states), the evaluator terminates in ≤ 100ms template-instantiation time.

**Offline verification fallback.** For large Γ's (training-step protocol with 64 participants, ~1000 LTS states), compile-time evaluation is too slow. The `tools/mcrl2_export/` pipeline emits the Γ as an mCRL2 specification, runs the mCRL2 tools to compute φ, and writes a proof certificate into Cipher. The next compile reads the certificate via `constexpr` file inclusion (`#embed` from P1967R14) and short-circuits the `ModelCheck` query.

**Seven canonical φ's as concepts.** For ergonomic use:

```cpp
template <typename Γ> concept SafeContext           = is_safe_v<Γ>;
template <typename Γ> concept DeadlockFreeContext   = is_df_v<Γ>;
template <typename Γ> concept LiveContext           = is_live_v<Γ>;
template <typename Γ> concept LivePlusContext       = is_live_plus_v<Γ>;
template <typename Γ> concept LivePlusPlusContext   = is_live_plus_plus_v<Γ>;
template <typename Γ> concept TerminatingContext    = is_terminating_v<Γ>;
template <typename Γ> concept NonTerminatingContext = is_nonterminating_v<Γ>;
```

User picks the tightest concept that makes sense for their channel.

## III.L8 — Crash-stop extensions (SessionCrash.h)

`include/crucible/sessions/SessionCrash.h`. Stop, crash-branches, reliability sets, unavailable queues.

```cpp
namespace crucible::safety::session {

// Stop type: local view of a crashed endpoint
struct Stop;

// Crash label: marker for crash-branch in Offer
template <typename CrashedPeer>
struct Crash;

// Unavailable queue marker (BHYZ23 ⊘)
struct UnavailableQueueMarker;

// Reliability set: phantom type-list of roles that never crash
template <typename... Roles>
struct ReliableSet;

// Crash-aware context
template <typename Γ, typename R>
struct CrashAwareContext;

// Well-formedness under crash-stop
template <typename Γ, typename R>
inline constexpr bool is_crash_well_formed_v = /* … */;

// Every Offer from an unreliable peer must have a Crash branch
template <typename Γ, typename R>
inline constexpr bool has_mandatory_crash_branches_v =
    detail::for_every_offer<Γ>::template with_unreliable_sender<R>::has_crash_branch::value;

// Crash-aware safety = regular safety + crash-well-formedness
template <typename Γ, typename R>
inline constexpr bool is_safe_under_crash_v =
    is_safe_v<Γ> &&
    has_mandatory_crash_branches_v<Γ, R> &&
    is_crash_well_formed_v<Γ, R>;

// Derived: the 7 φ levels each lift to crash-aware versions
template <typename Γ, typename R> inline constexpr bool is_df_under_crash_v        = /* … */;
template <typename Γ, typename R> inline constexpr bool is_live_under_crash_v      = /* … */;
// (etc.)

// Runtime API: transition a handle to Stop
template <typename H>
auto set_stop(H&& handle) -> SessionHandle</* Stop ... */>;

// Reduction rules for crash
//   [GR-✂]  a participant's local type becomes Stop on detected crash
//   [GR-⊘]  queue to a crashed recipient becomes UnavailableQueueMarker
//   [GR-✂m] any in-flight message to a crashed recipient is discarded

} // namespace
```

**Runtime crash detection hooks.** The CNTP Layer 1 transport reports failed RDMA completions; the CNTP Layer 2 SWIM reports confirmed-dead peers. Both paths call into `session::detail::notify_crash(PeerId)`; any handle with that peer transitions its protocol state to Stop (or advances to a Crash branch if in an Offer awaiting one).

**Statically-enforced crash-branch discipline.** An `Offer<Branch<L, T>…>` that receives from an unreliable peer (peer ∉ R of the enclosing context) must include a `Branch<Crash<Peer>, CrashBody>` for that peer. Omitting it is a compile error via `static_assert(has_mandatory_crash_branches_v<Γ, R>, "Offer from unreliable peer P missing Crash<P> branch")`.

## III.L9 — CSL × session (PermissionedSession.h)

`include/crucible/safety/PermissionedSession.h`. The integration that ties CSL permissions into session-protocol state.

```cpp
namespace crucible::safety::session {

// Permissioned handle: tracks both protocol state AND active permissions
template <typename Proto, typename Resource, typename Perms,
          typename LoopCtx = EmptyLoopStack>
class PermissionedSessionHandle;

// Permissions are a type-level list of Permission tags
template <typename... PermTags>
struct PermissionSet;

// Transfer a Permission through a Send: sender loses it, recipient gains it
template <typename PermTag>
struct Transferable { using perm_tag = PermTag; };

// Compile-time check: every communication event balances the permission sets
template <typename Γ, typename InitialPermissions>
inline constexpr bool is_permission_balanced_v = /* … */;

// CSL frame rule: parallel composition of two permissioned sessions
//   If P_1 : T_1 uses perms P1 and P_2 : T_2 uses perms P2, disjoint,
//   then P_1 | P_2 : T_1 ⊗ T_2 uses P1 ⊎ P2.
template <typename H1, typename H2>
auto csl_compose(H1&& h1, H2&& h2) -> PermissionedSessionHandle</* composed */>;

// At End, active permission set must match declared exit set
template <typename Proto, typename EntryPerms, typename ExitPerms>
inline constexpr bool exit_permissions_valid_v = /* … */;

// φ_CSL = φ_safe ∧ permission-balanced
template <typename Γ, typename InitialPerms>
inline constexpr bool is_csl_safe_v =
    is_safe_v<Γ> && is_permission_balanced_v<Γ, InitialPerms>;

} // namespace
```

**The permission-transfer invariant.** When a handle with active permission set P sends a message of type `Transferable<X>` (carrying a Permission<X>), the handle's active set becomes P \ {X}; the recipient's active set gains X. Protocol-level invariant: the union of active sets across all participants is constant (modulo splits and combines at CSL-split points).

**Safety theorem.** If every participant's local protocol preserves permission balance, and the global initial permission set is the disjoint union of every participant's initial set, then at every reachable protocol state the union of active permissions is well-defined. Proof: by induction on the LTS reduction, using the per-event transfer rule.

**Compile-time enforcement.** Every Send/Recv of a `Transferable<X>` is checked against the permissioned handle's current perm set. Sending without holding the permission is a compile error. Receiving duplicates the permission on the recipient's side (the type system tracks this). At End, the caller must surrender the permission-set to continue.

**Relation to CRUCIBLE.md §5.1.** CNTP's permission-carrying messages (gradient buckets with Permission<Grad<k>>, KernelCache publications with Permission<CompiledKernel<hash>>) use this layer directly.

## III.L10 — Pattern library (SessionPatterns.h)

`include/crucible/sessions/SessionPatterns.h`. Ready-made one-liners for common communication patterns.

```cpp
namespace crucible::safety::session::pattern {

// Request-response: client sends Req, receives Resp, repeat
template <typename Req, typename Resp>
using RequestResponse = Loop<Send<Req, Recv<Resp, Continue>>>;

// Fan-out from 1 coordinator to N workers
template <std::size_t N, typename Msg>
struct FanOutTag;
template <std::size_t N, typename Msg>
using FanOut = /* N-party Loop<Send<Msg, End>> for coordinator */;

// Fan-in from N workers to 1 collector
template <std::size_t N, typename Msg>
using FanIn = /* N-party Loop<Recv<Msg, End>> for collector */;

// Scatter-gather
template <std::size_t N, typename Task, typename Result>
using ScatterGather = Compose<FanOut<N, Task>, FanIn<N, Result>>;

// Pipeline of stages, each a binary session
template <typename... Stages>
using Pipeline = detail::compose_all_t<Stages...>;

// 2PC (Non-Blocking Atomic Commit, BSYZ22 §6)
template <std::size_t N_followers>
struct NBACProtocol;

// MpmcProtocol: N producers, M consumers
template <std::size_t N, std::size_t M, typename T>
struct MpmcProtocol;

// Raft replication (simplified)
template <std::size_t N_followers, typename Entry>
struct RaftReplication;

// Ring rotation (context parallelism)
template <std::size_t N, typename T>
struct RingRotation;

// Broadcast
template <std::size_t N, typename T>
struct Broadcast;

// SWIM probe (gossip step)
template <typename Probe, typename Ack>
using SWIMProbe = RequestResponse<Probe, Ack>;

// Crash-handled wrapper
template <typename Proto, typename Peer, typename Recovery>
using CrashHandled =
    /* inject Crash<Peer> branches into every Offer that receives from Peer */;

// CheckpointedSession: protocol with rollback point for speculation
template <typename ProtoBase, typename ProtoRollback>
struct CheckpointedSession;

} // namespace
```

Each pattern is a compile-time construction; instantiating `MpmcProtocol<4, 8, Job>` generates the full global type + per-role local types + queue types via template metafunctions. Patterns are the user-facing surface; raw combinators are the foundation.

## III.L11 — Diagnostic tags (SessionDiagnostic.h)

`include/crucible/sessions/SessionDiagnostic.h`. Per task #342. Classification tags attached to compile errors to identify the manifest bug class and suggest remediation.

```cpp
namespace crucible::safety::session::diagnostic {

// Manifest-bug classes (from session-type literature)
struct ProtocolViolation_Label;        // sent wrong label in a Select
struct ProtocolViolation_Payload;      // sent wrong payload type
struct ProtocolViolation_State;        // called an op in the wrong protocol state
struct Deadlock_Detected;              // static analysis found a stuck state
struct Livelock_Detected;              // cycle with no progress
struct StarvationPossible;             // enabled I/O that might never fire
struct CrashBranch_Missing;            // Offer from unreliable peer without Crash branch
struct PermissionImbalance;            // permission set changed unexpectedly
struct SubtypeMismatch;                // requested subtyping not provable
struct DepthBoundReached;              // ⩽_a bound hit; try widening
struct UnboundedQueue;                 // queue type not balanced+

// Diagnostic message emitter
template <typename DiagnosticClass, typename... Ctx>
struct Diagnostic;

// User-visible error via user-defined static_assert message (P2741R3)
#define CRUCIBLE_SESSION_ASSERT(cond, diag_class, msg)                           \
    static_assert(cond,                                                          \
        "crucible::session: " #diag_class ": " msg)

} // namespace
```

**Why this is a separate layer.** Compile errors from session-type violations cascade through many template expansions; the final diagnostic is often a wall of "constraint not satisfied". The Diagnostic layer catches at the earliest failed check and emits a short, classified message naming the manifest-bug class and giving a remediation hint. Cuts diagnostic walls from ~2K lines to ~3 lines.

## III.L12 — Choreographic projection (v2 sketch)

`include/crucible/safety/SessionChoreography.h`. Deferred to v2 but architecturally anticipated.

```cpp
namespace crucible::safety::session::choreo {

// A choreographic program: one expression describing all participants' behavior
template <typename... Steps>
class Choreography;

// Primitive steps
template <typename From, typename To, typename Label, typename Expr>
struct Comm;                          // From sends To a Label-labeled msg computed from Expr

template <typename Role, typename LocalExpr>
struct LocalCompute;                  // Role computes LocalExpr locally

template <typename Condition, typename TrueBranch, typename FalseBranch>
struct Branch;                        // global if/else

template <typename Var, typename Body>
struct ChoreoRec;                     // recursive choreography

// Projection: choreography → per-role program
template <typename Choreo, typename Role>
struct ProjectChoreo;
template <typename Choreo, typename Role>
using project_choreo_t = typename ProjectChoreo<Choreo, Role>::type;

// Well-formedness: all branches projectable, all locally-computable steps local to the right role
template <typename Choreo>
inline constexpr bool is_choreo_well_formed_v = /* … */;

} // namespace
```

**What this buys in v2.** The user writes the training step as one choreography expression; the compiler projects it to per-Keeper code. Forge's Phase K does a similar thing ad-hoc; L12 would formalize it and let us prove correctness of the projection.

**Why v2.** Choreographic programming is a large surface (Carbone-Honda-Yoshida 2012, Montesi 2013, Cruz-Filipe et al. 2021); correct implementation takes substantial design. Phase 1-11 of this document give us what we need without L12; L12 is the ergonomic cherry on top.

---

# Part IV — Every Crucible communication channel as a typed conversation

Crucible communicates on roughly fifty channels simultaneously (enumerated in §I.1). This part gives each channel its typed conversation: global type (where natural), per-participant local types, queue types, reliability assumptions, chosen φ, and a pointer into CRUCIBLE.md or THREADING.md for the runtime implementation.

The organization: intra-process channels (§IV.0-11), CNTP's five layers (§IV.12-16), distributed workload-specific channels (§IV.17-22), user-facing serving channels (§IV.23-28), lifecycle and fault-tolerance channels (§IV.29-33). A complete per-channel φ-table is in Part VI.

## IV.0 Channel catalog overview

The catalog has thirty-four subsections covering every class of communication in Crucible. For each we specify:

- **Scope**: intra-thread / intra-process / inter-process / inter-node / inter-cluster
- **Transport**: which mechanism carries the bytes (TraceRing SPSC, MpmcRing SCQ, RDMA, io_uring, doorbell+pushbuffer, etc.)
- **Participants**: named roles with their counts
- **Global type G** (if natural): the bird's-eye protocol; with a note if we use bottom-up instead
- **Per-role local type(s)**: what each participant's session type is
- **Queue type σ** (if async): pending-messages discipline
- **Reliability R**: which roles are reliable
- **Chosen φ**: the safety level appropriate to this channel
- **Composition**: how this channel composes with adjacent channels
- **CRUCIBLE.md / THREADING.md cross-ref**: pointer to the runtime spec

The catalog is ~35 subsections; for brevity, most are compressed to 6–12 lines. A few (MpmcRing, ExecutionPlan submission, Raft, all_reduce, InferenceSession) get full treatment because they are load-bearing and their types are non-trivial.

## IV.1 Vessel dispatch — frontend → runtime

**Scope**: intra-process (frontend thread → Crucible runtime); also serves as the FFI boundary for cross-language adapters.

**Transport**: function call into `CrucibleDispatchResult crucible_dispatch(…)` in `vessel_api.cpp`, passing `(CKernelId, TensorMeta[], schema_hash)`. Returns mock-tensor handles.

**Participants**: 2 — Frontend (PyTorch-Vessel / JAX-Vessel / native), Vigil (the capture runtime). Binary session.

**Global type G** (natural: binary → can write as a 2-party global):
```
μt . Frontend → Vigil : { dispatch(op(args), shape(meta)) . Vigil → Frontend : result(handle) . t,
                           close()                        . end }
```

**Local types**:
- Frontend view: `Loop<Send<DispatchRequest, Recv<MockHandle, Continue>>>`
- Vigil view: `Loop<Recv<DispatchRequest, Send<MockHandle, Continue>>>`

These are dual; compile-time check `static_assert(std::is_same_v<FrontendT, dual_of_t<VigilT>>)` passes.

**Queue type σ**: synchronous — FFI is a function call, no async buffering.

**Reliability R**: {Vigil} — Vigil is in-process, cannot independently die without taking the Frontend with it. If Vigil crashes, Frontend crashes (both are the same process).

**Chosen φ**: `is_df_v ∧ is_live++_v` — dispatch completes on every call (no deadlock), and always terminates promptly (no indefinite blocking). safe is subsumed.

**Composition**: sequential with the downstream TraceRing publication (the MockHandle carries an implicit commitment that the op has been enqueued into TraceRing before the handle is returned).

**Cross-ref**: CRUCIBLE.md §3 Hollow Vessel (the capture pattern); `vessel_api.cpp` (the C ABI); task #78 (Vigil Mode as Session<…>).

**Refinement story**: cross-language frontends (PyTorch, JAX, native) are EACH subtypes of the canonical Frontend session type. Compile-time static_assert in each Vessel adapter.

## IV.2 TraceRing + MetaLog — dispatch → bg drain

**Scope**: intra-process (Vessel dispatch thread → Crucible bg drain thread).

**Transport**: SPSC ring buffers, 64-byte-aligned cells, acquire/release semantics. TraceRing carries TraceEntry (schema_hash + scalar args); MetaLog carries TensorMeta (shape, stride, dtype, device, data_ptr).

**Participants**: 2 — Producer (dispatch), Consumer (bg drain). Parallel rings; same protocol shape for each.

**Global type G** (for TraceRing specifically):
```
μt . Producer → Consumer : { entry(TraceEntry) . t,
                              flush()           . end }
```

**Local types**:
- Producer: `Loop<Select<Branch<entry, Send<TraceEntry, Continue>>, Branch<flush, End>>>`
- Consumer: `Loop<Offer<Branch<entry, Recv<TraceEntry, Continue>>, Branch<flush, End>>>`

**Queue type σ**: bounded async, capacity 1024 × 64-byte entries. σ = `Queue<QueuedMsg<Producer, Consumer, entry, TraceEntry>…>` with length ≤ 1024.

**Reliability R**: {both} — both roles are threads in the same process; neither can die independently.

**Chosen φ**: `is_safe_v ∧ is_live+_v` — safe by label-matching, live under fair-scheduling guarantee from SCHED_DEADLINE on dispatch + SCHED_OTHER on drain. not live++ because if drain is preempted indefinitely, backpressure eventually blocks dispatch (not a liveness violation of the protocol, just a backpressure event).

**Composition**: sequential into bg pipeline (§IV.4); the drain's flush branch triggers the pipeline's epoch.

**Cross-ref**: CLAUDE.md L4 Operations; THREADING.md §2.2 SPSC; task #37 (TraceRing::drain parallel-output contract).

## IV.3 bg pipeline — drain → build → transform → compile

**Scope**: intra-process (one Keeper's bg threads).

**Transport**: SPSC channels between stages; final output publishes to KernelCache via SWMR.

**Participants**: 5 — Drainer, Builder, Transformer, Compiler, Publisher. Pipeline.

**Global type G** (natural for this pipeline):
```
G_pipe = μt . Drainer     → Builder     : TraceEntry[] .
               Builder     → Transformer : TraceGraph .
               Transformer → Compiler    : RegionNode .
               Compiler    → Publisher   : CompiledKernel .
               t
```

**Local types** (each stage sees only its inputs + outputs):
- Drainer: `Loop<Send<TraceEntry[], Continue>>` (bottom of pipeline — only outputs)
- Builder: `Loop<Recv<TraceEntry[], Send<TraceGraph, Continue>>>`
- Transformer: `Loop<Recv<TraceGraph, Send<RegionNode, Continue>>>`
- Compiler: `Loop<Recv<RegionNode, Send<CompiledKernel, Continue>>>`
- Publisher: `Loop<Recv<CompiledKernel, Continue>>` (top — only inputs)

**Queue types**: each stage-boundary is an SPSC bounded channel with its own σ.

**Reliability R**: {all} — all stages are threads in the same Keeper; same-process reliability.

**Chosen φ**: `is_safe_v ∧ is_live+_v ∧ is_associated_v_with_G_pipe` — safe by label, live under fair scheduling, associated with the natural global type. association gives us: "if G_pipe is deadlock-free, so is the implementation" — useful because G_pipe's deadlock-freedom is easy to prove (no cycles).

**Composition**: sequential with TraceRing (§IV.2) on the input side, parallel with CNTP publication (§IV.16) on the output side.

**Cross-ref**: CRUCIBLE.md §3.5 compile-on-demand; CLAUDE.md L4-L7; task #315 (SEPLOG-D1 staged pipeline); task #288 (PARALLEL-3 4-stage bg pipeline — completed).

## IV.4 KernelCache — SWMR publication

**Scope**: intra-process (bg Publisher → any reader thread).

**Transport**: atomic-pointer-swap seqlock; compiled-kernel pointers are content-addressed; readers grab the current pointer via acquire-load.

**Participants**: 1 Writer (the bg Publisher) + N Readers (dispatch thread, other bg threads, debug tools).

**Global type G**:
```
G_kcache = μt . Writer → { Reader_i : broadcast(KernelEntry) | i ∈ 1..N } . t
```

**Local types**:
- Writer: `Loop<Send<KernelEntry, Continue>>` (broadcasting to all)
- Reader: `Loop<Recv<KernelEntry, Continue>>`

**Queue type σ**: logically a snapshot, not a queue — readers see "latest version" not "all versions". Formally this is an AtomicSnapshot pattern (THREADING.md §11).

**Reliability R**: {Writer, all Readers} — same-process.

**Chosen φ**: `is_safe_v ∧ is_live_v` (live, not live+, because a reader that never polls isn't starving — they've chosen not to read; pending I/O fires iff the reader actually calls `load()`).

**Composition**: receives from bg pipeline (§IV.3); composes with CNTP Layer 4 cross-Keeper publication (collectives carry compiled-kernel hashes in annotations; per §IV.18).

**Cross-ref**: CLAUDE.md L2 Kernels; THREADING.md §11 AtomicSnapshot; task #281 (AtomicSnapshot for Augur; same pattern).

## IV.5 AtomicSnapshot broadcast — MemoryPlan, Augur metrics

**Scope**: intra-process (1 writer + N readers, per snapshot tag).

**Transport**: seqlock pattern, pair of sequence counters bracketing memcpy.

**Participants**: 1 Writer + N Readers (dynamic N, determined at runtime).

**Global type**: isomorphic to KernelCache (§IV.4) with different payload types:
- For MemoryPlan: `MemoryPlan` struct with slot offsets, sizes, alignments.
- For Augur: `MetricsSnapshot` struct with rolling-window residuals, Hessian spectrum samples, per-kernel counters.

**Per-snapshot local types**: same as §IV.4.

**Queue type σ**: snapshot, not queue.

**Reliability R**: {all same-process}.

**Chosen φ**: `is_safe_v ∧ is_live_v`.

**Composition**: parallel with KernelCache (§IV.4) and with user-visible debug tooling (`crucible top`).

**Cross-ref**: THREADING.md §11; task #281 (SEPLOG-QUEUE-6 AtomicSnapshot for Augur metrics).

## IV.6 ExecutionPlan submission — host → GPU

**Scope**: intra-process (host CPU → GPU's host-engine).

**Transport**: pre-composed pushbuffer bytes + PatchPoint writes + GPFIFO advance + doorbell MMIO write (§14.7 CPU critical path).

**Participants**: 2 — HostCPU, GPUHostEngine.

**Global type G**:
```
G_submit = μt .
    HostCPU → GPUHostEngine : { submit(Plan p, PatchValues vs) .
                                 GPUHostEngine → HostCPU : { completion(Result r) . t,
                                                              timeout()            . end,
                                                              fault(Diagnostic d)  . end } ,
                                 close() . end }
```

**Local types**:
- HostCPU: `Loop<Select<Branch<submit, Send<(Plan, PatchValues), Offer<Branch<completion, Recv<Result, Continue>>, Branch<timeout, End>, Branch<fault, Recv<Diagnostic, End>>>>>, Branch<close, End>>>`
- GPUHostEngine (dual): the dual of HostCPU's type.

**Queue type σ**: bounded async, capacity = GPFIFO depth (~2048 entries on Hopper).

**Reliability R**: {HostCPU} — the GPU can fail (ECC uncorrectable, thermal panic, driver crash); GPUHostEngine is NOT in R. The `fault` branch IS the crash-handling branch.

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_crash_safe_v<R={HostCPU}>` — df under scheduler fairness, live+ given the GPU's hardware scheduler, crash-safe via the fault branch handling GPU death.

**Composition**: ChainEdges (§IV.7) compose multiple ExecutionPlan submissions sequentially on the GPU side without host round-trip. Parallel composition of multiple independent submissions uses Host+GPU threads pinned to disjoint plans.

**Cross-ref**: CRUCIBLE.md §11.9 ExecutionPlan, §14.7 latency budget; MIMIC.md §15 pushbuffer.

## IV.7 ChainEdge semaphores — Plan-to-Plan GPU-side chaining

**Scope**: intra-process (one Plan's end → next Plan's start), both running on the GPU's host engine.

**Transport**: pinned-memory semaphores in sysmem (8-byte atomic values); acquire-or-greater wait semantics; GPU-side release via SEM_RELEASE pushbuffer instruction.

**Participants**: 2 — Upstream (Plan N), Downstream (Plan N+1). The semaphore IS the channel.

**Global type G**:
```
G_chain = Upstream → SEM : signal(Value=N+1) .
          Downstream acquire-waits on SEM ≥ N+1 .
          Downstream proceeds.
```

**Local types**:
- Upstream: `Send<Value, End>` (one-shot signal per epoch)
- Downstream: `Recv<Value, End>` (wait until value ≥ expected)

**Queue type σ**: trivially empty — the semaphore state is the σ (a monotonic integer, not a FIFO).

**Reliability R**: {Upstream, Downstream} — both are GPU-side execution; if the GPU fails both die together; handled by ExecutionPlan's fault branch (§IV.6).

**Chosen φ**: `is_live+_v` — under fair scheduling, the Upstream eventually signals and the Downstream eventually observes. Critical: the protocol is SIMPLE and LATENCY-CONSTRAINED (~400 ns PCIe fly time, per CRUCIBLE.md §14.7.4).

**Composition**: ChainEdges compose sequentially into a training step's full plan-chain: forward → backward → allreduce → optimizer, each a separate Plan linked by semaphores.

**Cross-ref**: CRUCIBLE.md §11.9.3 ChainEdge; FORGE.md §J.5.

## IV.8 PatchPoint mutations — per-step hyperparam writes

**Scope**: intra-process (host CPU → GPU pushbuffer WC mapping).

**Transport**: aligned byte-width MMIO write to pushbuffer's WC-mapped region, followed by SFENCE.

**Participants**: 2 — HostCPU, PushbufferAperture (a pseudo-participant; really just memory).

**Global type G**: degenerate (single-direction write, no response).

**Local types**:
- HostCPU: `Loop<Send<PatchValue, Continue>>` (writing scalars into the pushbuffer).
- PushbufferAperture: implicit — side-effect, not protocol-visible.

**Queue type σ**: trivially empty — patches are idempotent writes.

**Reliability R**: {HostCPU}.

**Chosen φ**: `is_safe_v` — label-matching via the PatchPoint name, width-matching as a runtime contract.

**Composition**: parallel with ExecutionPlan submission (§IV.6); patches are written BEFORE the doorbell, so SFENCE ensures ordering.

**Cross-ref**: CRUCIBLE.md §11.9.2 PatchPoint taxonomy; FORGE.md §18.8.

## IV.9 Vigil mode state machine

**Scope**: intra-thread (one Vigil's internal state transitions).

**Transport**: none; state is in-memory.

**Participants**: 1 — Vigil itself. Not a conversation in the session-type sense; but it IS a typestate machine, and the same framework applies.

**Global type**: not applicable (single-party). Local type is a state machine:
```
VigilProto = μt . Select<
    Branch<record,  Send<RecordedTraceEntry, Continue>>,
    Branch<replay,  Send<ReplayedOp,         Continue>>,
    Branch<serving, Send<InferenceResponse,  Continue>>,
    Branch<flush,   Loop<Send<DrainedEntry, Continue>>>
>
```

**Local types**: just VigilProto above; no peer.

**Queue type σ**: not applicable.

**Reliability R**: {Vigil} — same-process.

**Chosen φ**: `is_df_v ∧ is_live+_v` — every reachable mode can either transition or terminate at flush. The state machine is a DAG of modes; progress is guaranteed.

**Composition**: this is a SINGLE-PARTY typed conversation, useful for typestate-enforcing the Vigil's internal mode transitions. It composes with every other Vigil channel (§IV.1 dispatch, §IV.2 TraceRing) in parallel — Vigil's mode is part of the Γ that those other channels' compositions reduce over.

**Cross-ref**: task #34 (Vigil mode_ as safety::Machine); task #78 (Vigil Mode as Session<…>); Vigil.h:47-51.

## IV.10 Transactions — begin / ops / commit-or-abort

**Scope**: intra-process (the code inside a transaction block).

**Transport**: in-memory state machine; no actual inter-thread transport.

**Participants**: 1 — the transaction itself. Same as Vigil mode: single-party typed conversation.

**Local type**:
```
TxProto = Loop<Select<
    Branch<op,      Send<TxOp, Continue>>,
    Branch<commit,  Send<CommitResult, End>>,
    Branch<abort,   Send<AbortReason,  End>>
>>
```

**Chosen φ**: `is_df_v ∧ is_term_v` — every transaction terminates (in commit or abort); no deadlock.

**Composition**: a transaction is a SUB-PROTOCOL that nests inside a larger session. Checkpoint-rollback property: if abort, restore pre-transaction state; if commit, state advances. This is the `CheckpointedSession<ProtoSafe, ProtoRollback>` pattern from II.12.2.

**Cross-ref**: task #101 (Transaction Session<TxStatus, …>); Transaction.h:28-57.

## IV.11 PermissionedMpmcChannel — overview (deep dive in Part V)

**Scope**: intra-process (N producer threads × M consumer threads); crucial channel — the flagship of this whole framework.

**Transport**: Nikolaev Scalable Circular Queue (SCQ); 2N-slot double-buffered ring with cycle-tagged cells; FAA-advance of head/tail via atomic operations.

**Participants**: N producers + M consumers. Fully multiparty.

**Global type G** (sketch — full spec in Part V):
```
G_mpmc = μt . Select<
    Producer_i → Queue : push(T) . t                    (any producer can push)
    Queue      → Consumer_j : deliver(T) . t            (any consumer can consume)
    close()    . end
>
```

**Local types**:
- Producer_i: `Loop<Select<Branch<push, Send<T, Continue>>, Branch<close, End>>>`
- Consumer_j: `Loop<Offer<Branch<deliver, Recv<T, Continue>>, Branch<close, End>>>`

**Queue type σ**: bounded async; `σ = Queue<QueuedMsg<any Producer, any Consumer, push, T>…>` with length ≤ 2N.

**Reliability R**: depends on deployment — typically {all producers ∪ all consumers} when same-process; when CNTP-mediated {none} with crash branches.

**Chosen φ**: `is_safe_v ∧ is_live+_v ∧ is_precise_async_safe_v` — safe by label, live+ under fair scheduler, precise-async-safe meaning the FAA-based producer ordering admits the subtyping required for async reordering of sends.

**Composition**: the MPMC channel is the connective tissue for task #313 (SEPLOG-C3 AdaptiveScheduler), task #314 (SEPLOG-C4 NUMA-aware ThreadPool), task #322 (SEPLOG-F3 WorkloadProfiler). Composition story is: each worker's role projects from the global G_mpmc to a producer-or-consumer local; parallel composition of N+M participants gives the full pool.

**Cross-ref**: THREADING.md §17 MPMC internals; tasks #327-#331 (SEPLOG-H1 through H6 MPMC frontier rollup); §V of this document.

## IV.12 Cipher hot-tier — peer-memory reads

**Scope**: inter-process (same host) and inter-node.

**Transport**: RDMA read from peer's pre-registered memory region; or `cuMemcpyPeerAsync` for intra-node NVLink; bytes content-addressed by `ContentHash`.

**Participants**: 2 — Requester, Responder (a peer Keeper that has the entry hot).

**Global type G**:
```
G_hot = μt . Requester → Responder : { fetch(ContentHash) .
                                         Responder → Requester : { bytes(Span<u8>)    . t,
                                                                    miss()              . t,
                                                                    crash(PeerFailure) . end } }
```

**Local types** (using content-addressed quotient combinator from §II.12.2):
- Requester: `Loop<Send<ContentAddressed<ContentHash>, Offer<Branch<bytes, Recv<Bytes, Continue>>, Branch<miss, Continue>, Branch<crash, End>>>>`
- Responder: dual of Requester's type.

**Queue type σ**: bounded async, capacity = RDMA posted-work queue depth (~256 per QP pair).

**Reliability R**: {} — either Keeper can fail; the crash branch handles it.

**Chosen φ**: `is_df_v ∧ is_crash_safe_v<R={}>` — df under normal operation, crash-aware for peer death. Not live+: a missed-peer may never respond, but the crash branch is invoked via RDMA completion error after ~1s timeout.

**Composition**: downstream of Cipher warm-tier (§IV.13) — on hot-miss, fall back to warm; on warm-miss, fall back to cold (§IV.14). Classical three-tier hierarchy as sequential composition of three subprotocols with fallback.

**Cross-ref**: CRUCIBLE.md §9.1 hot-tier; §9.5 federation; task #160 (thread_local schema_cache WriteOnce).

## IV.13 Cipher warm-tier — local NVMe writes via io_uring

**Scope**: intra-process (background writer → kernel io_uring).

**Transport**: io_uring SQPOLL mode with IORING_REGISTER_BUFFERS; HOT thread fires SQE; COLD thread processes CQEs.

**Participants**: 2 — HotWriter (submits writes), ColdCompleter (processes completions).

**Global type G** (bottom-up — no natural G):
- HotWriter local: `Loop<Send<WriteRequest, Continue>>` (fire-and-forget submissions)
- ColdCompleter local: `Loop<Recv<WriteCompletion, Continue>>`

**Queue type σ**: bounded async, capacity = io_uring ring depth.

**Reliability R**: {HotWriter, ColdCompleter} — same-process.

**Chosen φ**: `is_safe_v ∧ is_live+_v ∧ is_term_v` (per-write terminates when completion arrives).

**Composition**: written-back entries promote via `is_cold_tier_v` check (§IV.14).

**Cross-ref**: CRUCIBLE.md §16.7 I/O; task #102 (Cipher streams as Linear<ScopedFd>).

## IV.14 Cipher cold-tier — S3 object store

**Scope**: inter-process (Keeper → S3-compatible endpoint).

**Transport**: HTTPS PUT; Keeper maintains a small http client pool.

**Participants**: 2 — Keeper (writer), S3-endpoint (storage).

**Global type G** (binary request-response):
```
G_cold = μt . Keeper → S3 : upload(ContentHash, Bytes) .
               S3 → Keeper : { success .
                                 t | failure(HTTPError) .
                                 Keeper retries from scratch (via outer choice composition) }
```

**Local types**:
- Keeper: `Loop<Send<UploadRequest, Offer<Branch<success, Continue>, Branch<failure, Recv<Error, Continue>>>>>`
- S3-endpoint: dual.

**Queue type σ**: bounded by outgoing connection count.

**Reliability R**: {} — S3 can return 503, timeout, network partition. Crash branch = retry + exponential backoff.

**Chosen φ**: `is_safe_v ∧ is_term_v` — each upload terminates eventually (success or max-retries); no liveness promise under sustained S3 outage.

**Composition**: terminal tier — no fallback below cold.

**Cross-ref**: CRUCIBLE.md §9.1 cold-tier; §9.6 Raft replication.

## IV.15 CNTP Layer 1 — Transport

**Scope**: inter-node.

**Transport**: RDMA verbs (RoCE/IB), NVLink (intra-node), or io_uring TCP (fallback/WAN).

**Participants**: 2 per QP pair or NVLink channel; CNTP multiplexes many QP pairs across N nodes.

**Global type G** (per-QP; one global type for each QP pair):
```
G_transport = μt . Sender → Receiver : { eager(Bytes) . t,
                                          rdma_write(Offset, Bytes, Cookie) . t,
                                          rdma_read(Offset, Cookie) .
                                              Receiver → Sender : bytes(Bytes, Cookie) . t,
                                          atomic_faa(Addr, Delta) .
                                              Receiver → Sender : previous(uint64) . t,
                                          close() . end,
                                          crash(PeerFailure) . end }
```

**Local types**:
- Sender: bundled Select with five branches; dual on Receiver.

**Queue type σ**: bounded by NIC's SQ/RQ depths; capacity ~256 per QP on ConnectX-6.

**Reliability R**: {} — any Keeper can fail.

**Chosen φ**: `is_df_v ∧ is_crash_safe_v<R={}>`. Not live+: under severe fabric congestion or DC cabling failure, completion may not arrive within protocol bound; crash branch covers timeout.

**Composition**: Layer 1 is the CARRIER for Layers 2-5 (higher-order session types, §II.12.7). Each upper-layer payload is opaque to Layer 1 except for its length + integrity + encryption.

**Cross-ref**: CRUCIBLE.md §5.1 Transport; MIMIC.md §37.

## IV.16 CNTP Layer 2 — SWIM gossip

**Scope**: inter-node (every Keeper gossips to K=3 random peers every 1s).

**Transport**: XDP-programmed fast path; eBPF handlers at NIC driver level; userspace fallback for complex state.

**Participants**: All Keepers in the Canopy (dynamic N).

**Global type G** (simplified; dynamic participants via §F.1 workaround):
```
G_swim = μt . (∀p ∈ Members) .
    p → peer(K=3 random) : probe(p, timestamp) .
        peer → p : { ack(timestamp) . t,
                     no_response(timeout_event) .
                         p → other_peers(K=3 random) : indirect_probe(peer, timestamp) .
                         ... (multi-hop confirmation) }
```

**Local types** (per Keeper):
```
p local = Loop<Select<Branch<probe, Send<Probe, Recv<Maybe<Ack>, Continue>>>,
                       Branch<respond_probe, Recv<Probe, Send<Ack, Continue>>>,
                       Branch<confirmed_dead, Send<DeathAnnouncement, Continue>>>>
```

**Queue type σ**: trivially small — each probe fits in one packet (~64B), fire-and-forget with bounded retry.

**Reliability R**: {} — SWIM's entire point is to detect peer failure.

**Chosen φ**: `is_safe_v ∧ is_nterm_v` — gossip runs forever; safe by label-matching. Not live+ because individual probes may fail under partition but the macro membership eventually converges.

**Composition**: feeds Layer 3 Raft (§IV.17) with death-confirmation events. Parallel with Layer 1 Transport (§IV.15) — SWIM packets share the fabric with RDMA data.

**Cross-ref**: CRUCIBLE.md §5.2 SWIM + Lifeguard.

## IV.17 CNTP Layer 3 — Raft consensus

**Scope**: inter-node (one Raft group per cluster).

**Transport**: Layer 1 AppendEntries / RequestVote RPCs; io_uring or RDMA payloads.

**Participants**: Dynamic — leader + followers; roles change on leader election.

**Global type G** (simplified):
```
G_raft = μt . (∀p ∈ Members) .
    leader → follower_i : append(LogEntries, term, prevIdx) .
        follower_i → leader : { ack(logIndex) . t,
                                 reject(staleness) . t,
                                 term_out_of_date(new_term) . election_trigger .
                                    (new leader election subprotocol) . t }
                          | crash(follower_i_failure) . (continue without follower_i) . t
```

**Local types**:
- Leader: `Loop<Select<Branch<append, Send<LogEntries, Offer<ack, reject, term_out_of_date, Crash>>>, Branch<step_down, Send<StepDown, End>>>>`
- Follower: dual; plus handling of step_down (become candidate).

**Queue type σ**: bounded async for in-flight AppendEntries; per-follower pipeline.

**Reliability R**: {current Raft leader} during a commit; the protocol assumes the leader doesn't die mid-commit; if it does, a new election happens (protocol rewinds).

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_crash_safe_v<R={leader-for-this-commit}> ∧ is_associated_v_with_G_raft` — df (Raft provably makes progress under majority), live+ (under fair scheduling reaches commit), crash-safe (followers may die, handled via Raft's majority requirement), associated with G_raft.

**Composition**: Raft carries higher-level protocols as its log entries: Cipher promotion commits, topology changes, recipe registry updates, checkpoint manifests. Each is a HIGHER-ORDER session nested inside Raft's log-entry payload (§II.12.7).

**Cross-ref**: CRUCIBLE.md §5.3 Raft scoped; §7.4 fleet reshard.

## IV.18 CNTP Layer 4 — Collectives

**Scope**: inter-node (N-party collectives across DP / TP / PP / EP / CP groups).

**Transport**: Layer 1 RDMA primitives; per-collective algorithm pinned per BITEXACT recipe (canonical binary tree sorted by UUID) or per ORDERED recipe (ring / halving-doubling).

**Participants**: N per group.

Five collective protocols, each its own G and φ. Summarized:

**all_reduce_sum** (N participants):
```
G_allreduce = Let's order peers by UUID as p_1 < p_2 < ... < p_N .
    Build a binary tree. For each tree level:
    siblings → parent : partial_sum . Done.
    Root broadcasts result down the tree.
```
- Local type per leaf: `Send<Partial, Recv<Result, End>>`
- Local type per internal node: `Recv<Partial, Send<Partial, Recv<Result, Send<Result, End>>>>` (or similar, depending on tree position)
- φ = `is_df_v ∧ is_live+_v ∧ is_crash_safe_v<R={}>` — crash-safe via CRUCIBLE.md §12.10 timeout+restart.

**all_gather**: similar tree-based, each peer sends shard to root then root broadcasts full.

**reduce_scatter**: dual of all_gather.

**broadcast**: tree, one-way.

**all_to_all**: N×(N-1) p2p sends; for MoE routing, every peer sends a distinct message to every other peer.

**Queue type σ**: bounded per peer pair by posted-RDMA-work-queue depth.

**Chosen φ**: all have `is_df_v ∧ is_live+_v ∧ is_crash_safe_v<R={}>`; BITEXACT recipes additionally require `is_associated_v_with_G_canonical_tree` where G_canonical_tree is the UUID-sorted tree topology — association gives replay determinism.

**Composition**: bucketed async all-reduce (§IV.23) composes multiple concurrent all_reduce subprotocols over disjoint bucket sets; the parallel composition theorem guarantees the composition is safe provided buckets are disjoint (enforced by Forge's Phase K).

**Cross-ref**: CRUCIBLE.md §5.4 Collectives; §12.7 bucketed async; MIMIC.md §37.

## IV.19 CNTP Layer 5 — NetworkOffload

**Scope**: inter-node; optional layer.

**Transport**: In-fabric hardware (Mellanox SHARP, TPU ICI aggregation, AMD XGMI reductions, Cerebras SwarmX).

**Participants**: N Keepers + SwitchHardware (pseudo-participant).

**Global type G**:
```
G_offload = ∀p_i . p_i → Switch : shard_i .
            Switch computes reduction in fabric .
            Switch → ∀p_i : reduced_result .
```

Equivalent in outcome to all_reduce but routes through hardware.

**Local types**: very similar to all_reduce leaf; per-peer participation.

**Reliability R**: {Switch} — the switch is reliable (part of the fabric spec); peers may die.

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_associated_v_with_G_offload` (identical outcome as G_allreduce under BITEXACT, so associated when in-fabric hardware is available).

**Composition**: optional; CNTP queries capability and routes through offload if eligible, else falls back to Layer 4 software collective (§IV.18). The choice combinator from §II.12.1.

**Cross-ref**: CRUCIBLE.md §5.5 NetworkOffload; MIMIC.md §38.

## IV.20 Ring attention — K/V rotation

**Scope**: inter-node (context-parallel group).

**Transport**: Layer 1 point-to-point sends in a ring topology; N-way parallelism with O(N) rotation steps.

**Participants**: N peers in the context-parallel group, arranged in a ring (by UUID).

**Global type G**:
```
G_ring = for rotation_step in 0..N-1:
    for each peer p_i in parallel:
        p_i → p_{(i+1) % N} : KV_shard_at_step(rotation_step) .
        p_i ← p_{(i-1) % N} : KV_shard_at_step(rotation_step) .
    # each peer computes local attention contribution using current K/V shard
```

**Local types**:
- Per-peer: `Loop<Send<KVShard, Recv<KVShard, Continue>>>` — rotating send-then-recv for N iterations.

**Queue type σ**: double-buffered K/V slots; σ has capacity 2 per peer pair.

**Reliability R**: {all peers} — under BITEXACT, all peers must participate; failure triggers fleet reshard (§IV.31) which restarts the ring at a new N-value.

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_associated_v_with_G_ring` under BITEXACT.

**Composition**: ring attention composes in sequence with per-layer compute (local computations happen during rotation dead time, hiding latency).

**Cross-ref**: CRUCIBLE.md §12.9 ring attention.

## IV.21 Pipeline parallelism — 1F1B / interleaved / zero-bubble

**Scope**: inter-node (pipeline-parallel group).

**Transport**: Layer 1 adjacent-stage sends; per-stage micro-batch queue.

**Participants**: P pipeline stages × N micro-batches in flight.

**Global type G** (GPipe 1F1B — simplified):
```
G_pp = ∀ micro-batch m ∈ 1..N :
    stage_0 → stage_1 : forward_m .
    stage_1 → stage_2 : forward_m .
    ... (forward propagates through stages)
    stage_{P-1} computes loss for m .
    stage_{P-1} → stage_{P-2} : backward_m .
    stage_{P-2} → stage_{P-3} : backward_m .
    ... (backward propagates) .
    stage_0 updates parameters .
```

**Local types per stage**: `Loop<Select<Branch<forward_i, Send<ActIn, Recv<ActOut, Branch<backward_i, Recv<GradOut, Send<GradIn, Continue>>>>>>>>`.

**Queue type σ**: bounded by in-flight micro-batches (1F1B has 1 forward + 1 backward in flight per stage; interleaved has P×V_stages; zero-bubble has additional B/W splitting).

**Reliability R**: {all stages} under BITEXACT.

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_term_v` (per micro-batch terminates).

**Composition**: composes with bucketed async all-reduce (§IV.23) in parallel — gradients bucket-reduced during subsequent forward passes.

**Cross-ref**: CRUCIBLE.md §12.6 pipeline scheduling; FORGE.md §I.LaunchOrder.

## IV.22 Expert parallelism — MoE dispatch + combine

**Scope**: inter-node (expert-parallel group).

**Transport**: Layer 1 all-to-all (N×(N-1) sends), twice per MoE layer (dispatch, combine).

**Participants**: N peers (expert-parallel group).

**Global type G**:
```
G_ep = ∀ token t :
    owner(t) → expert_owner(t) : dispatch(token) .
    expert_owner(t) → owner(t) : combine(expert_result) .
```

Expressed as two all-to-all phases; each peer sends tokens to each other peer's expert, receives results back.

**Local type per peer**:
```
Loop<Send<TokenToDispatch, Send<TokenToDispatch, ..., Recv<ExpertResult, Recv<ExpertResult, ..., Continue>>>>>
```

(N-1 Sends then N-1 Recvs per all-to-all phase, repeated per MoE layer.)

**Queue type σ**: bounded per peer pair by the all-to-all's posted-work-queue depth.

**Reliability R**: {all peers} under BITEXACT.

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_associated_v_with_G_ep`.

**Composition**: composes with per-expert grouped-GEMM compute (§IV.22 runs the all-to-all, Forge+Mimic handle the per-expert kernel).

**Cross-ref**: CRUCIBLE.md §12.8 expert parallelism runtime.

## IV.23 Bucketed async all-reduce

**Scope**: inter-node (DP group, bucket-level dispatch).

**Transport**: Layer 1 RDMA; multiple concurrent all-reduces on independent buckets.

**Participants**: Per-bucket: N peers in DP group. Across buckets: parallel composition.

**Global type G per bucket**: G_allreduce from §IV.18.

**Across-bucket**: parallel composition `G_bucket_1 ∥ G_bucket_2 ∥ …` — disjoint memory regions, disjoint RDMA operations, bucket-level content-addressing ensures no aliasing.

**Local types**: per-bucket identical to all_reduce (§IV.18).

**Queue type σ**: per-bucket independent bounded queues.

**Reliability R**: {all peers}, crash-safe via §IV.18's mechanism.

**Chosen φ**: `is_live+_v ∧ is_crash_safe_v<R={}>` per bucket; parallel composition of buckets gives `is_live+_v ∧ is_crash_safe_v` overall.

**Composition**: bucketed async IS the parallel-composition theorem (§II.12.1) applied to multiple instances of the all_reduce protocol. Hidden fraction of 70-90% comes from overlap with backward compute (§IV.21).

**Cross-ref**: CRUCIBLE.md §12.7 bucketed async all-reduce.

## IV.24 DataNode prefetch — CPU Relay → GPU Relay

**Scope**: inter-process (CPU Relay prefetching → GPU Relay consuming).

**Transport**: RDMA write into pre-planned device slot; bounded prefetch queue per (CPU Relay, GPU Relay) pair.

**Participants**: 2 per pair — CPULoader, GPUConsumer.

**Global type G**:
```
G_data = μt . CPULoader → GPUConsumer : batch(Shard) . t
              | crash(CPULoader) . (failover to another CPU Relay) . t
              | crash(GPUConsumer) . (re-assign shard) . t
```

**Local types**:
- CPULoader: `Loop<Select<Branch<batch, Send<Shard, Continue>>, Branch<close, End>>>`
- GPUConsumer: `Loop<Offer<Branch<batch, Recv<Shard, Continue>>, Branch<close, End>, Branch<Crash<CPULoader>, Recv<FailoverInfo, Continue>>>>` (crash branch per IV.24's reliability set)

**Queue type σ**: bounded by prefetch_depth (default 4 batches per consumer).

**Reliability R**: {} — either side may die; both crash branches present.

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_crash_safe_v<R={}>`.

**Composition**: DataNode feeds into TraceRing's first forward pass (§IV.2) through the training step's Plan submission (§IV.6). Sequential composition with ExecutionPlan; parallel composition across multiple DP replicas' DataNodes.

**Cross-ref**: CRUCIBLE.md §11 data loading; §12.13 shard migration.

## IV.25 InferenceSession prefill

**Scope**: inter-process (client → Keeper serving Inference).

**Transport**: RPC-style over CNTP Layer 1; Keeper dispatches to a compiled prefill Plan.

**Participants**: 2 — Client, Keeper (the serving node).

**Global type G**:
```
G_prefill = Client → Keeper : session_create(ModelRef, SamplingConfig) .
            Keeper → Client : session_handle(SessionId, PagedKVRef) .
            Client → Keeper : prefill(tokens: uint32[]) .
            Keeper → Client : { prefill_logits(BatchLogits) . (continue to decode) ,
                                oom_or_quota_exceeded(Reason) . end } |
            Client → Keeper : cancel() . Keeper releases resources . end
```

**Local types**:
- Client: sequential Send/Recv/Send chain with an Offer for prefill's outcome.
- Keeper: dual; handles session creation, prefill dispatch, cancellation.

**Queue type σ**: trivially small — prefill is single-request.

**Reliability R**: {Keeper} during the request; if Keeper dies the session is gone and the client must re-create on a different Keeper.

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_term_v` (per-request terminates).

**Composition**: sequential into decode (§IV.26); InferenceSession's lifecycle is prefill → decode loop → close.

**Cross-ref**: CRUCIBLE.md §11.6 InferenceSession.

## IV.26 InferenceSession decode — continuous batching

**Scope**: inter-process + intra-process (N clients' sessions batched by Keeper).

**Transport**: per-session async channel for streaming tokens; Keeper's continuous-batching loop aggregates across sessions.

**Participants**: N Clients + 1 Keeper (per-node; multiple Keepers at larger scale).

**Global type G**:
```
G_decode = μt . (∀ client_i) .
    client_i → Keeper : { request_next_token .
                           Keeper → client_i : token(uint32) . t,
                           close() . end } |
    client_i → Keeper : crash(Client_i_disconnected) . (Keeper removes from batch) . t
```

**Local types**:
- Client: `Loop<Select<Branch<request_next, Recv<Token, Continue>>, Branch<close, End>>>`
- Keeper: N parallel dual handles; continuous-batching logic aggregates the N Loop-bodies into batched kernel launches.

**Queue type σ**: one per client × small depth (streaming tokens, bounded prefetch).

**Reliability R**: {Keeper} — clients may disconnect arbitrarily.

**Chosen φ**: `is_live+_v ∧ is_nterm_v` per client (decode may run indefinitely per client); Keeper overall has `is_live+_v ∧ is_df_v`.

**Composition**: continuous batching is PARALLEL composition of N client sessions (§II.12.1). All N compose via the frame rule provided sessions are disjoint — each client's KV-cache and sampling state are separate.

**Cross-ref**: CRUCIBLE.md §11.6 continuous batching.

## IV.27 PagedKVCache — cross-session prefix sharing

**Scope**: intra-process (one Keeper's pool of inference sessions).

**Transport**: refcounted shared pages in HBM; content-addressed via prefix-payload hash.

**Participants**: PoolAllocator + N sessions. Each session's access to a page is through a refcount-incremented handle.

**Global type G**: degenerate at the protocol level — this is a resource-sharing mechanism, not a conversation. But it fits our framework via the content-addressed quotient combinator (§II.12.2).

**Local type per session**: `Send<PrefixHashRequest, Recv<PageHandle, End>>` — session asks for a prefix, pool returns a refcounted page handle.

**Queue type σ**: trivially small.

**Reliability R**: {all sessions same-process}.

**Chosen φ**: `is_safe_v ∧ is_live_v`.

**Composition**: two sessions with identical prefixes share pages via content-addressing. This is the `ContentAddressed<Prefix>` quotient combinator — the protocol is invariant under prefix-content equality. Provably correct because a session's observable decoding depends only on prefix CONTENT, not on whether the page was fetched fresh or found in the pool.

**Cross-ref**: CRUCIBLE.md §11.6 Paged KV cache.

## IV.28 Beam search — session fork

**Scope**: intra-process (one session branches into N copies).

**Transport**: copy-on-write page fork + refcounted KV pages; each branch advances independently.

**Participants**: 1 parent Session + N child Sessions post-fork.

**Global type G**:
```
G_fork = Parent . fork(N) . (∀ child_i ∈ 1..N) Child_i decodes independently .
                             Parent selects best child → re-exposes that child's output
```

**Local types**:
- Parent: `Send<ForkN, End>` (one-shot, parent is consumed)
- Child: `Loop<Send<RequestNext, Recv<Token, Continue>>>` (same as InferenceSession decode)

**Queue type σ**: per-child independent.

**Reliability R**: {all children same-process}.

**Chosen φ**: `is_safe_v ∧ is_term_v` (beam search completes per child).

**Composition**: parallel composition of N child sessions, via §II.12.1 frame rule on disjoint state.

**Cross-ref**: CRUCIBLE.md §11.6 `session.fork()`.

## IV.29 Speculative decoding — draft → target verify

**Scope**: intra-process (draft session + target session, coordinated).

**Transport**: in-process; draft emits N candidate tokens, target verifies in batched dispatch.

**Participants**: 2 — DraftSession, TargetSession.

**Global type G** (uses the Checkpointed combinator from §II.12.2):
```
G_spec = μt . Draft → Target : candidates(Token[N]) .
              Target → Draft : { accept_all . Draft & Target advance N steps . t,
                                  accept_prefix(k < N) .
                                      Draft & Target advance k steps .
                                      Target emits +1 bonus token .
                                      t,
                                  reject .
                                      Draft & Target rollback to last checkpoint . t
                                }
                      | close() . end
```

**Local types**:
- Draft: `Loop<Send<Token[N], Offer<Branch<accept_all, Continue>, Branch<accept_prefix, Recv<Uint32, Continue>>, Branch<reject, (* rollback *)>>>>`
- Target: dual.

Uses the Checkpointed combinator (§II.12.2): every iteration establishes a checkpoint at loop start; the reject branch rolls back.

**Queue type σ**: bounded by speculative depth (typically N=4 to 8).

**Reliability R**: {both same-process}.

**Chosen φ**: `is_safe_v ∧ is_live+_v ∧ is_term_v` per speculation.

**Composition**: speculative decoding is sequentially composed into the InferenceSession decode protocol (§IV.26); each decode step internally runs a speculation.

**Cross-ref**: CRUCIBLE.md §11.6 speculative decoding; task #329 (deferred to future work in some details).

## IV.30 Canopy join/leave lifecycle

**Scope**: inter-node (Keeper's lifecycle from boot to healthy participation).

**Transport**: Layer 2 SWIM gossip + Layer 3 Raft membership commits.

**Participants**: the Joining Keeper + existing Canopy members (dynamic count; see §F.1).

**Global type G** (per Keeper lifecycle):
```
G_lifecycle = Joining → SWIM-network : discovery(seed_peers) .
              Joining → NearestPeer : exchange_state(Membership hash) .
              NearestPeer → Joining : peer_list(Members[]) .
              Joining → RaftLeader : request_admit(caps, UUID) .
              RaftLeader → RaftFollowers : append(MembershipDelta=add(Joining)) .
              (Raft commit protocol from §IV.17)
              RaftLeader → Joining : admitted(epoch=E+1) .
              Joining → ∀Members : advertise_native_recipes(bitmap) .
              Joining enters READY state .
              # Symmetric leave sequence on graceful shutdown.
```

**Local types**: per-role (Joining, Follower, Leader, Peer) — subtyped views of the Raft protocol (§IV.17) plus gossip (§IV.16).

**Queue type σ**: bounded; the lifecycle is one-shot per Keeper.

**Reliability R**: per-role — the Raft Leader is reliable within the admission commit; Joining may fail (dies before commit, Raft rolls back; Joining crashes mid-join, Raft refuses).

**Chosen φ**: `is_df_v ∧ is_live+_v ∧ is_crash_safe_v<R={Leader-for-commit}>`.

**Composition**: lifecycle composes with the fleet-reshard protocol (§IV.31) — admission triggers a reshard if partition shape changes.

**Cross-ref**: CRUCIBLE.md §7.2 membership lifecycle; §8.2 operator implementation.

## IV.31 Fleet reshard — topology change at iteration boundary

**Scope**: inter-node (all DP/TP/PP participants coordinate).

**Transport**: Raft-committed epoch bump + Layer 4 weight-redistribution collectives.

**Participants**: all active Keepers.

**Global type G**:
```
G_reshard = RaftLeader → ∀ Keepers : begin_reshard(new_partition, epoch E+1) .
            Phase K (Forge) on each Keeper computes new shard assignment .
            Keepers exchange weights per §IV.18 all_gather / reduce_scatter variants .
            RaftLeader → ∀ Keepers : reshard_complete(epoch E+1) .
            Keepers resume at next iteration on new topology
```

**Local types**: each Keeper has a phase-structured local type: `Recv<BeginReshard, Send<LocalShard, Recv<RedistributedShards, Recv<Complete, Continue_training>>>>`.

**Queue type σ**: bounded by weight-redistribution RDMA queues.

**Reliability R**: {Raft Leader}.

**Chosen φ**: `is_df_v ∧ is_term_v ∧ is_crash_safe_v<R={Leader}>` — reshard terminates or rolls back if a Keeper dies mid-reshard.

**Composition**: reshard is a PAUSE in the training step's ongoing composition; after reshard, the training-step protocol resumes but with a new Γ (new partition = new per-Keeper local types).

**Cross-ref**: CRUCIBLE.md §7.4 fleet reshard; §12.1 partition lifecycle; §12.4 reshard cost.

## IV.32 Live binary upgrade — rolling replacement

**Scope**: inter-cluster (all Keepers roll to a new binary hash, half-at-a-time).

**Transport**: Cipher cold-tier distributes new binary bytes; Canopy gossip announces hash; Raft orchestrates the rolling schedule.

**Participants**: RaftLeader + all Keepers.

**Global type G**:
```
G_upgrade = RaftLeader → Canopy : announce_new_binary(hash, scheduled_epoch) .
            (first half of Keepers) . graceful_shutdown → reload(new_bytes) → rejoin → verify_healthy .
            RaftLeader → remaining half : proceed .
            (remaining half) . graceful_shutdown → reload → rejoin → verify_healthy .
            training resumes with updated binary.
```

**Local types**: per-Keeper, encoded as a state machine: `Send<AnnounceSeen, Select<Branch<shutdown_now, End→loadNew→rejoin>, Branch<wait, Continue>>>`. The End is followed by the new binary resuming from Cipher.

**Queue type σ**: small; one update per Keeper.

**Reliability R**: {Raft Leader}. Upgrade runs during idle periods preferably; if it must run during training, step-loss from graceful shutdown is bounded.

**Chosen φ**: `is_df_v ∧ is_term_v` (upgrade terminates).

**Composition**: upgrade is a PAUSE analogous to reshard (§IV.31) — training resumes on the new binary with the same Γ structure.

**Cross-ref**: CRUCIBLE.md §14.5 self-updating.

## IV.33 FLR recovery — GPU function-level reset

**Scope**: intra-process (one Keeper's local recovery from GPU failure).

**Transport**: vfio-pci ioctl for FLR initiation; GSP firmware re-upload from cached hugepage sysmem; Cipher checkpoint load for state restoration.

**Participants**: HostCPU + GPUBeingRecovered. Technically a 2-party protocol with one party that has crashed and is being resurrected.

**Global type G**:
```
G_flr = HostCPU detects fault (BAR0=0xFFFFFFFF, MSI-X fatal, watchdog timeout) .
        HostCPU → GPU : flr_initiate (PCI config write) .
        HostCPU waits 100ms mandatory PCIe settle .
        HostCPU → GPU : restore_pci_config .
        HostCPU → GPU : upload_gsp_firmware (from cache) .
        HostCPU → GPU : init_rpc_handshake .
        HostCPU → GPU : re_establish_channel .
        HostCPU → GPU : pin_clocks .
        HostCPU loads cipher checkpoint T_0 .
        HostCPU re-submits plans from T_0 forward (replay determinism §10) .
        Training resumes.
```

**Local type for HostCPU**: a long sequential Send with phase-structured interspersed Recvs for confirmations; ends with returning to normal operation. Uses Checkpointed combinator (§II.12.2) for the replay-from-T_0.

**Queue type σ**: trivially small.

**Reliability R**: {HostCPU}; the GPU is explicitly UNRELIABLE — the whole protocol is the crash-recovery branch.

**Chosen φ**: `is_df_v ∧ is_term_v ∧ is_crash_safe_v<R={HostCPU}>`.

**Composition**: FLR recovery composes with §IV.32 live upgrade (same pause-then-resume pattern) and with §IV.31 fleet reshard (if the GPU death triggers a reshard).

**Cross-ref**: CRUCIBLE.md §17.8 FLR; §14.8 NV-specific init; task #164 (fallback boundary Session type-state).

## IV.34 Keeper startup + shutdown

**Scope**: per-Keeper process lifecycle (systemd / k8s / SLURM to READY to exit).

**Transport**: sequence of initialization calls; ending or interruption handled via signal handlers.

**Participants**: the Keeper itself (plus external triggers: systemd, k8s kubelet).

**Global type G** (startup sequence from CRUCIBLE.md §14.1):
```
G_startup = load_config → init_mimic_rt → alloc_cipher_ram → mmap_cipher_nvme →
            init_cntp → register_mr → compute_UUID →
            start_swim → recv_membership → join_raft → advertise_caps →
            meridian_probe → READY
```

Linear sequence; one party (the Keeper); no dual.

**Local type**: a long `Compose` chain of Recv<ConfigResponse, Send<InitRPC, Recv<…, …>>>> — single-party typestate machine (like §IV.9 Vigil mode and §IV.10 Transaction).

**Chosen φ**: `is_df_v ∧ is_term_v` (startup terminates at READY).

**Composition**: startup is PREFIXED to every other Crucible session. All subsequent protocols assume Keeper-has-initialized.

**Cross-ref**: CRUCIBLE.md §14.1 startup sequence; §14.4 shutdown.

---

# Part V — The MPMC ring as the flagship

The PermissionedMpmcChannel (§IV.11) is the first industrial typed conversation in Crucible's concurrency layer. It is chosen as the flagship because every theoretical layer converges there: bounded async queue types (§II.6), precise async subtyping (§II.7), crash-stop with reliability sets (§II.8), CSL permission balance (§III.L9), content-addressed dedup where applicable (§II.12.2), and — crucially — the compositionality theorem (§II.12.4) which makes it safe to run N producer threads × M consumer threads each with its own typed handle.

This part: global type (§V.1), per-role projections (§V.2), queue type modeling the Nikolaev SCQ (§V.3), precise async subtyping as the mechanism that justifies producer pipelining (§V.4), crash-stop via unavailable queues (§V.5), CSL permission balance at message level (§V.6), dynamic membership (§V.7), content-addressed dedup (§V.8), and the composed φ statement (§V.9).

## V.1 The global type G_MPMC

```
Parameters: N producers, M consumers, capacity C (at the logical level; 2C at SCQ level).
Phantom participants: Queue (the channel), pseudo-role.

G_MPMC<N, M, T, C> =
  μt . Select<
    Producer_i → Queue : push(T)                 . t,        (any of N producers)
    Queue      → Consumer_j : deliver(T)         . t,        (any of M consumers)
    close()                                      . end
  >

With reliability R = (Producers ∪ Consumers) ∖ {failing}.
With queue invariant: length ≤ C at all times (balanced+ at capacity C).
```

The Queue role is virtual; the SCQ cells physically implement the queue. The type system treats Queue as a participant for protocol-analysis purposes; at runtime it has no own thread.

Note: precise async subtyping (§II.7) admits a finer-grained version where Producer_i's sends and Consumer_j's receives are PERMUTABLE at the subtype level. This is what makes lock-free MPMC possible: multiple producers issuing FAA-advance can overlap without each waiting for the other.

## V.2 Per-role projections

Each participant sees only their own local protocol.

**Producer_i view**:
```
T_producer_i = Loop<Select<
    Branch<push, Send<T, Continue>>,
    Branch<close, End>
>>
```

Meaning: "in a loop, choose either to send a value or to close." No visibility of how consumers behave; no dependency on the Queue's internal state (other than respecting capacity via backpressure at runtime).

**Consumer_j view**:
```
T_consumer_j = Loop<Offer<
    Branch<deliver, Recv<T, Continue>>,
    Branch<close, End>,
    Branch<Crash<Producer_i>, (∀ Producer_i ∉ R) . Continue>    // crash branches when unreliable
>>
```

Meaning: "in a loop, offer to receive a value or be closed; also handle crash of any producer that's not in R." If all producers are reliable (R = all), the crash branches collapse away.

**Queue (virtual) view**:
```
T_queue = Loop<Select<
    Branch<push, Recv<T, Select<Branch<deliver, Send<T, Continue>>, Branch<enqueued, Continue>>>>,
    Branch<deliver, Send<T, Continue>>,
    Branch<close, End>
>>
```

The Queue can accept pushes and emit deliveries in any order; its σ tracks pending messages.

Projection from G_MPMC onto Producer_i composed with merge for the Consumer_j and Queue projections yields T_producer_i; symmetric for Consumer_j. Full coinductive merging (PMY25 §4.3) handles the choice-at-Queue-level projection.

## V.3 The queue type σ_MPMC — Nikolaev SCQ as type

The physical implementation uses Nikolaev's Scalable Circular Queue with 2C cells for logical capacity C. Each cell has a cycle counter tracking round-robin reuse. We model this as a queue type at the session-type level:

```
σ_MPMC<T, C> = Queue<QueuedMsg<Producer_i, Consumer_any, push, T>…>  length ≤ C
```

The σ above is consumer-blind — it doesn't know which specific consumer will eventually receive a given message. This is intentional: any consumer may consume any message, matching MPMC semantics.

**Capacity bound.** The runtime's `try_push` returns false when the ring is full; at the type level, this is the backpressure event. The protocol can either block (waiting for σ to have capacity) or retry later. For bounded-buffer decidability, we declare `is_balanced_plus_v<σ_MPMC, C>` as a hard invariant: at every reachable protocol state, `|σ| ≤ C`. Violating this is a type error, not a runtime fault.

**Cell-level state and session-type abstraction.** At the SCQ level, each cell has a cycle counter and an IsSafe bit (THREADING.md §17). These are RUNTIME invariants that implement the type-level queue σ correctly. The session type doesn't need to know about cycles; it abstracts them away. The implementation correctness of the SCQ vis-à-vis the type abstraction is a separate proof obligation (THREADING.md §17.4 covers it).

**Relation to ring state.** At any moment, the MpmcRing's cells are in one of these logical states (§17.2 THREADING.md):
- `(Cycle=k, IsSafe=1, Occupied=0)` → empty, cycle k
- `(Cycle=k, IsSafe=1, Occupied=1)` → message present, producer has committed
- `(Cycle=k, IsSafe=0, Occupied=1)` → consumer has marked unsafe, producer retry later
- `(Cycle=k, IsSafe=1, Occupied=0, dequeued)` → message consumed, cell advanced to cycle k+1

At the σ level, we only care about message presence: the cell sequence is `Queue<(cell_state=Occupied-with-content)…>`. The session type's reduction rules match this abstraction.

## V.4 Precise async subtyping in action

The key question: why can a single producer's `try_push` advance WITHOUT waiting for a consumer's `try_pop`? Session types must justify this, or the abstraction is leaky.

Answer: precise async subtyping ⩽_a (§II.7, GPPSY23 rule [ref-B]).

Consider two producers P_1 and P_2 both pushing. At the naïve synchronous level, the protocol is:
```
P_1 → Queue : push(v_1) . Queue → (some C_j) : deliver(v_1) .
P_2 → Queue : push(v_2) . Queue → (some C_k) : deliver(v_2)
```

This imposes a false sequentialization: P_2 must wait for P_1's delivery. But async allows P_2 to push while v_1 is still queued:
```
P_1 → Queue : push(v_1) .
P_2 → Queue : push(v_2) .
Queue → C_j : deliver(v_1) .
Queue → C_k : deliver(v_2)
```

The subtype relation that admits this reordering is rule [ref-B]: anticipate a Send past a B-sequence, where B-sequence = recvs-from-any + sends-to-others. Here P_2's send can anticipate past P_1's queue-delivery because P_1's delivery is to a DIFFERENT participant (C_j, not the Queue-the-pseudo-role P_2 sends to).

Formally: the producer session type `Send<v_1, Send<v_2, …>>` ⩽_a `Send<v_1, Recv<_, Send<v_2, …>>>` is valid under precise async subtyping. The producer doesn't wait for a consumer receive between their sends.

**This is the mechanism that makes MPMC faster than a CAS-protected FIFO**: producers don't synchronize with consumers at all; they only synchronize with each other via the FAA-advanced ticket order. The session type provably captures this.

**Bounded-depth check.** The precise async subtyping check for MPMC's relevant transformations has depth O(N+M) in the number of participants. With N=64, M=32, the depth bound default 64 is sufficient. Larger deployments (N > 64) may need wider bounds; diagnostic emitted.

## V.5 Crash-stop via unavailable queues

If a consumer crashes mid-session, messages in flight for that consumer are lost. BHYZ23's unavailable-queue marker ⊘ formalizes this.

**Protocol-level crash event**:
```
Consumer_j dies → σ[*, Consumer_j] transitions to ⊘.
Any message with "deliver to Consumer_j" enqueued after the crash is silently dropped.
```

**Type-level check**: every consumer's local type has a `Crash<Consumer_j>` branch (automatically injected by the pattern library for unreliable consumers); this is the type-system-enforced contract.

**Producer-side behavior**: producers are oblivious to individual consumer deaths. They push to the queue; the queue delivers to any available consumer. If ALL consumers die, pushes eventually fail with backpressure; crash branches on the producer side handle "all consumers dead" separately.

**Dynamic-membership integration**: consumer deaths trigger §IV.30's Canopy membership update; the set of live Consumers in G_MPMC shrinks. Per §V.7, G_MPMC is parameterized over a runtime set of participants; the type system treats this as a refinement of the static global type.

## V.6 CSL permission balance

MPMC's message payload can be a Permission<T> in addition to regular data. When a producer sends a Permission<X>, the producer loses X; the consumer that eventually receives it gains X. This is the CSL frame rule at message granularity.

**Permission-annotated local types**:
```
T_producer_i' = Loop<Select<
    Branch<push, Send<Transferable<X>, Continue>>,      // sender loses Permission<X>
    Branch<close, End>
>>

T_consumer_j' = Loop<Offer<
    Branch<deliver, Recv<Transferable<X>, Continue>>,   // receiver gains Permission<X>
    Branch<close, End>
>>
```

**Compile-time invariant**: `is_permission_balanced_v<Γ_MPMC, InitialPerms>` — at every reachable state, the sum of active permissions across all participants matches the initial total. Verified by the L9 layer.

**Runtime guarantee**: since SCQ cells store the data by value, and since Permission<X> is a phantom (zero-byte) marker, the "transfer" is compile-time bookkeeping. The producer's handle no longer has Permission<X> in its static type after the send (the method returning the new handle removes X from the set); the consumer's handle has Permission<X> after the receive. Zero runtime cost for the permission transfer.

**Usage**: BackgroundThread's KernelCache publication via MpmcRing transfers `Permission<CompiledKernel<hash>>` from the compile worker that produced it to the consumer that will register it. Prevents two workers from both believing they own the freshly-compiled kernel.

## V.7 Dynamic membership — N and M change at runtime

Canopy's elasticity (§F.1, §IV.30) means N (producer count) and M (consumer count) are not fixed at compile time. We handle this via:

**Static upper bound**: `MpmcRing<T, N_max, M_max, C>` parameterized on a compile-time bound; dynamic count at runtime is a refinement.

**Refinement predicate**: at any moment, `active_producers ⊆ {Producer_0, …, Producer_{N_max-1}}`; similarly for consumers. The active set is a RUNTIME value; the type system checks the static bound but admits any subset as the actual set.

**Safety under membership changes**: when a producer joins (Canopy admission), their Permission<Producer_slot_k> is minted fresh; when they leave (graceful), the Permission is returned and the slot can be reused. When they die (crash), the crash branch handles their disappearance.

**Type preservation theorem**: as N and M change within their static bounds, the protocol's φ (live+, crash-safe) is preserved. Proof: reduce to G_MPMC parameterized over the current-active set; association extends to the dynamic refinement.

## V.8 Content-addressed dedup

In certain workloads (e.g., Cipher gradient bucket reuse across training iterations, common-prefix KV-cache pages), two messages may have identical payloads. Protocol-level dedup skips the wire bytes.

**ContentAddressed<T> wrapper** (§II.12.2):
```
Send<ContentAddressed<T>, K> semantically means:
    if recipient has hash(T) cached, skip wire transmission;
    else send the bytes.
```

**Quotient semantics**: the protocol is invariant under content-equality. Recipient's observable state depends only on the payload's content hash; whether bytes arrived or were cache-hit is opaque at the protocol level.

**MPMC usage**: rare. Most MPMC payloads are unique (Job entries, task tokens, gradient bucket slices that change each iteration). But for specific use cases — Cipher hot-tier cross-session prefix sharing, KernelCache broadcast of identical kernels to all workers — the quotient combinator applies and the runtime benefits from dedup.

## V.9 The composed φ statement

The MPMC flagship's correctness statement — the single compile-time invariant that encompasses every layer of the theory applied to this one channel:

```
For a PermissionedMpmcChannel<T, N_max, M_max, C>:

  is_well_formed_v<G_MPMC<N_max, M_max, T, C>>                    -- WF from §L1
∧ is_balanced_plus_v<σ_MPMC, C>                                    -- queue bounded
∧ is_associated_v<Γ_MPMC, G_MPMC, MpmcSession>                     -- top-down projection
∧ is_subtype_async_v<T_producer, project_t<G_MPMC, Producer>, 64>  -- producer refinement
∧ is_subtype_async_v<T_consumer, project_t<G_MPMC, Consumer>, 64>  -- consumer refinement
∧ is_df_v<Γ_MPMC>                                                  -- never stuck
∧ is_live_plus_v<Γ_MPMC>                                           -- pending fires under fair
∧ is_crash_safe_v<Γ_MPMC, R>                                       -- crash branches present
∧ is_permission_balanced_v<Γ_MPMC, InitialPerms>                   -- CSL frame rule

⇒ the MpmcRing is a CORRECT, EFFICIENT, CRASH-TOLERANT, PERMISSION-AWARE,
  precisely-async-subtyped typed conversation implementing the Nikolaev SCQ.
```

This is checked at compile time, once, per `PermissionedMpmcChannel<…>` instantiation. If any conjunct fails, the static_assert reports which — with a diagnostic from the SessionDiagnostic layer (§III.L11) identifying the manifest-bug class.

**What this buys us.** The MpmcRing's correctness is no longer a claim supported by the Nikolaev paper + some unit tests + TSan runs. It is a COMPILE-TIME THEOREM with a machine-checkable proof (bounded-depth approximation + offline mCRL2 for the full-depth case). Every time we instantiate a new `PermissionedMpmcChannel<Job, 64, 32, 4096>` in the codebase, the compiler verifies the full stack.

## V.10 Cost budget — compile-time and runtime

> **⚠️ STATUS — PROJECTED, NOT MEASURED.**  All cost numbers in this section are PROJECTIONS extrapolated from underlying-primitive costs (SpscRing, atomic CAS, MPMC SCQ algorithm).  Real-workload measurements depend on Tasks #346 (L7 SessionSafety), #348 (async ⩽_a), #383 (AsyncContext), #381 (full merging) all landing — none are shipped as of 2026-04-24.  bench/bench_session_compile_time.cpp (Task #392) does not exist; the compile-time cost table below is theoretically derived from per-layer template-instantiation complexity, not from `-ftime-report` measurements on the actual MPMC instantiation (which currently doesn't compile because PermissionedMpmcChannel doesn't exist).
>
> The SessionHandle release-mode zero-overhead claim WAS empirically validated for sizeof (#366 commit body, /tmp/check_sizeof.cpp).  The PermissionedSpscChannel zero-overhead claim has compile-time `static_assert(sizeof(handle) == sizeof(channel-pointer))` validation (test_permissioned_spsc_channel.cpp).  No equivalent for the full MPMC stack yet.

Numbers. "Zero runtime cost" without measurements is a claim; with measurements it's a fact.  This table is currently in the FIRST category, not the second.

**Compile-time cost per PermissionedMpmcChannel instantiation (PROJECTED):**

| Check | Template instantiation count | Wall-clock (GCC 16 -O0) | Wall-clock (-O3) |
|---|---|---|---|
| L1 well-formedness | ~100 | 0.5 ms | 0.5 ms |
| L3 queue σ bounded-plus | ~300 | 2 ms | 2 ms |
| L4 projection (Producer, Consumer, Queue) | ~400 | 4 ms | 4 ms |
| L5 association Δ ⊑ G | ~200 | 3 ms | 3 ms |
| L6 sync subtyping of refinements | ~100 | 1 ms | 1 ms |
| L6 async subtyping (depth 64) | ~2000-5000 | 30-80 ms | 30-80 ms |
| L7 φ_df via LTS walk | ~500 | 8 ms | 8 ms |
| L7 φ_live+ via fair-scheduler LTS | ~700 | 12 ms | 12 ms |
| L8 crash-branch completeness | ~200 | 2 ms | 2 ms |
| L9 permission balance | ~300 | 4 ms | 4 ms |
| **Total per MpmcRing instantiation** | **~5000 instantiations** | **~70-120 ms** | **~70-120 ms** |

Absorbed into the ~30s cold build for the Crucible monolith. Per second of build time we verify ~10 full MpmcRing instantiations. Sufficient for Crucible's ~50 channels even with each re-instantiated several times.

**Runtime cost per operation:**

| Operation | Session-type overhead | Underlying transport | Total |
|---|---|---|---|
| `PermissionedMpmcChannel<T>::try_push(msg)` | 0 ns (EBO-collapsed phantom) | ~20-25 ns Nikolaev SCQ try_push | **~20-25 ns** |
| `PermissionedMpmcChannel<T>::try_pop()` | 0 ns | ~20-25 ns try_pop | **~20-25 ns** |
| Handle construction (move) | 0 ns (trivial move) | 0 ns | **0 ns** |
| Handle destruction at End | 0 ns (trivial) | 0 ns | **0 ns** |
| Handle destruction before End (abandoned) | +1 branch to cold path | — | **~2 ns debug / 0 ns release** |

The session-type machinery is DESIGNED to add ZERO ns to the hot path.  This claim is currently verified for sizeof (compile-time `static_assert` in PermissionedSpscChannel and SessionHandle) but UNVERIFIED for actual runtime cost on the MPMC stack — the bench harness below describes the test that would verify it; that test does not yet exist.

**Bench verification** (target: M10 milestone, SEPLOG-E1; bench harness Task #392 — both pending):

```cpp
// bench/bench_permissioned_mpmc.cpp
auto r = bench::Run("permissioned_mpmc.try_push")
    .hardening(crucible::rt::Policy::production())
    .samples(10'000'000)
    .measure([&]{
        auto h2 = std::move(handle).send(msg);
        handle = std::move(h2);
    });
r.assert_no_regression_vs("raw_mpmc_ring.try_push", within_percent=5);
```

The assertion: PermissionedMpmcChannel is within 5% of raw MpmcRing's cost, measured under production hardening. Passing this test IS the proof that the typed wrapper is zero-cost in optimized builds.

**Memory cost:**

- `PermissionedMpmcChannel<T, N, M, C>` sizeof = `sizeof(MpmcRing<T, 2C>)` + `sizeof(SharedPermissionPool<Producer>) + sizeof(SharedPermissionPool<Consumer>)` ≈ 2N cells × 64B + 2 × 48B = ~128KB for C=1024, + 96B overhead. The session-type wrappers add 0 bytes (empty base / EBO).

**Compile-error cost:** if any φ-conjunct fails, the static_assert fires and produces a routed diagnostic (§III.L11). Typical diagnostic length: 3-10 lines (vs 2K lines without routing). Cost to the engineer: seconds to locate + correct.

**Comparison to Vyukov MPSC wrapped with manual invariant checks:**

| Approach | Verification cost | Runtime cost | Crash-handling cost |
|---|---|---|---|
| Raw Vyukov MPSC + comments | 0 (no machine check) | baseline | manual per-crash | 
| Raw Nikolaev SCQ + unit tests | ~30 min CI per commit | baseline | manual per-crash |
| PermissionedMpmcChannel (typed) | ~100 ms compile-time | identical to baseline | type-enforced crash branches |

The typed approach trades 100ms of compile-time (amortized into 30s builds) for machine-checked safety, crash-safety, CSL permission balance, and precise async subtyping — all guarantees the raw ring cannot provide.

---

# Part VI — Per-channel φ-level reference table

A single table covering all thirty-four channels from Part IV, with their chosen φ conjuncts. This is the document's operational summary.

| # | Channel | Scope | φ choice | R | Top-down G? |
|---|---|---|---|---|---|
| IV.1 | Vessel dispatch | intra-process | `df ∧ live++` | {Vigil} | yes |
| IV.2 | TraceRing/MetaLog SPSC | intra-process | `safe ∧ live+` | {both} | yes |
| IV.3 | bg pipeline (4-stage) | intra-process | `safe ∧ live+ ∧ associated(G_pipe)` | {all stages} | yes |
| IV.4 | KernelCache SWMR | intra-process | `safe ∧ live` | {writer+readers} | yes |
| IV.5 | AtomicSnapshot (Augur etc.) | intra-process | `safe ∧ live` | {all} | yes |
| IV.6 | ExecutionPlan submission | intra-process | `df ∧ live+ ∧ crash-safe` | {HostCPU} | yes |
| IV.7 | ChainEdge semaphores | intra-process | `live+` | {both} | yes |
| IV.8 | PatchPoint writes | intra-process | `safe` | {HostCPU} | degenerate |
| IV.9 | Vigil mode state machine | intra-thread | `df ∧ live+` | {self} | single-party |
| IV.10 | Transactions | intra-process | `df ∧ term` | {self} | single-party |
| IV.11 | PermissionedMpmcChannel | intra-process | `safe ∧ live+ ∧ precise-async ∧ csl ∧ crash-safe` | deployment-specific | yes (§V) |
| IV.12 | Cipher hot-tier | inter-process | `df ∧ crash-safe` | {} | yes |
| IV.13 | Cipher warm-tier | intra-process | `safe ∧ live+ ∧ term` | {both} | bottom-up |
| IV.14 | Cipher cold-tier | inter-process | `safe ∧ term` | {} | yes |
| IV.15 | CNTP Layer 1 Transport | inter-node | `df ∧ crash-safe` | {} | yes |
| IV.16 | CNTP Layer 2 SWIM | inter-node | `safe ∧ nterm` | {} | approximate (§F.1) |
| IV.17 | CNTP Layer 3 Raft | inter-node | `df ∧ live+ ∧ crash-safe ∧ associated(G_raft)` | {current leader} | yes |
| IV.18 | CNTP Layer 4 collectives | inter-node | `df ∧ live+ ∧ crash-safe ∧ [under BITEXACT:] associated` | {} | yes |
| IV.19 | CNTP Layer 5 NetworkOffload | inter-node | `df ∧ live+ ∧ associated` | {Switch} | yes |
| IV.20 | Ring attention | inter-node | `df ∧ live+ ∧ associated` | {all peers} | yes |
| IV.21 | Pipeline parallelism | inter-node | `df ∧ live+ ∧ term` | {all stages} | yes |
| IV.22 | Expert parallelism MoE | inter-node | `df ∧ live+ ∧ associated` | {all peers} | yes |
| IV.23 | Bucketed async all-reduce | inter-node | `live+ ∧ crash-safe` (per bucket, parallel-composed) | {} | yes |
| IV.24 | DataNode prefetch | inter-process | `df ∧ live+ ∧ crash-safe` | {} | yes |
| IV.25 | InferenceSession prefill | inter-process | `df ∧ live+ ∧ term` | {Keeper} | yes |
| IV.26 | InferenceSession decode | inter-process | `live+ ∧ nterm` (per client) | {Keeper} | yes |
| IV.27 | PagedKVCache sharing | intra-process | `safe ∧ live` | {all} | degenerate |
| IV.28 | Beam search fork | intra-process | `safe ∧ term` | {all} | yes |
| IV.29 | Speculative decoding | intra-process | `safe ∧ live+ ∧ term` (rollback-aware) | {both} | yes |
| IV.30 | Canopy join/leave | inter-node | `df ∧ live+ ∧ crash-safe` | {Leader-for-commit} | yes |
| IV.31 | Fleet reshard | inter-node | `df ∧ term ∧ crash-safe` | {Leader} | yes |
| IV.32 | Live binary upgrade | inter-cluster | `df ∧ term` | {Leader} | yes |
| IV.33 | FLR recovery | intra-process | `df ∧ term ∧ crash-safe` | {HostCPU} | yes |
| IV.34 | Keeper startup/shutdown | per-process | `df ∧ term` | {self} | single-party |

**Legend**:
- **safe**: communication-safe; no label mismatches
- **df**: deadlock-free
- **term**: terminates in finite steps
- **nterm**: non-terminating service (never reaches End)
- **live**: pending I/O fires
- **live+**: live under fair scheduling
- **live++**: live under any scheduling
- **crash-safe**: crash-aware, with crash branches; R parameter specifies reliable roles
- **associated(G)**: Δ ⊑_s G for the named global type
- **precise-async**: subtyping admits bounded-depth ⩽_a
- **csl**: CSL permission-balance preserved

Most channels use `df ∧ live+ ∧ crash-safe`. The deeper cases (Raft, collectives, MPMC) additionally require association or precise-async.

**Reading the table.** This is the AUTHORITATIVE per-channel φ specification. When implementing a channel or reviewing an implementation, check this table for the chosen φ, then verify that the type-level machinery enforces each conjunct. Additions or changes to φ per channel require updating this table.

---

# Part VII — Execution plan — twelve milestones

> **Status note (last updated 2026-04-24).**  This section was first written when only L1 was shipped.  Reality has moved.  The status legend is updated honestly here; the per-milestone descriptions below are kept for historical reference, with each prefixed by a status tag.  See **Part IX — Honest Assessment** for the full per-conjunct gap analysis.
>
> Legend:
> - **✅ SHIPPED** — header + tests in tree, runtime semantics implemented
> - **⚠️ PARTIAL** — header in tree, but a critical sub-property is deferred (see notes)
> - **❌ NOT SHIPPED** — task pending, header does not exist
> - **🔌 NO CALLERS** — header exists but no production code uses it

Dependency-ordered delivery of the twelve-layer stack.

**M1 — L1 binary combinators.**  ✅ SHIPPED.  Session.h ships Send/Recv/Select/Offer/Loop/Continue/End + dual_of + compose + is_well_formed + SessionHandleBase CRTP (#349) + release-mode zero-cost EBO (#366).  Every Crucible protocol writable as a Session type today.  Tasks: #332 (completed).  Current line count: **1185** (was estimated as 700 in the original plan).

**M2 — L2 Typing context Γ.**  ✅ SHIPPED.  SessionContext.h ships Entry, Context<...>, compose_context_t, lookup_context_t, contains_key_v, domain_of_t, update_entry_t, remove_entry_t.  Task #343 (pending — actually completed; task list out-of-date).  Current line count: **640**.

**M3 — L3 Queue types σ.**  ⚠️ PARTIAL.  SessionQueue.h ships QueuedMsg, Queue<...>, EmptyQueue, enqueue/head/tail, queue_contains_v, count_matching_v, is_bounded_queue_v, is_unavailable_queue_v.  **DEFERRED**: AsyncContext<Γ, Queues...> + reduce_async_context_t + is_balanced_plus_v over reachable states (Task SEPLOG-STRUCT-9 / #383).  Task #344 (pending — partial).  Current line count: **556**.

**M4 — L4 Global types + projection.**  ⚠️ PARTIAL.  SessionGlobal.h ships End_G, Transmission, Choice, BranchG, Rec_G, Var_G, StopG, RoleList, RolesOf, is_global_well_formed_v, project_t with **PLAIN MERGE only**.  **DEFERRED**: coinductive full merging (PMY25 §4.3, Task SEPLOG-STRUCT-7 / #381) — required to project Raft, 2PC-with-multi-followers, or any DIVERGING multiparty protocol onto third-party roles.  Task #339 (pending — partial).  Current line count: **846**.

**M5 — L5 Association.**  ⚠️ PARTIAL.  SessionAssoc.h ships is_associated_v + projected_context_t + assert_associated.  **DEFERRED**: association preservation theorem (HYK24 Thm 5.8) — currently checks association at one point in time; preservation across runtime reductions requires L7's reduction semantics.  Task #345 (pending — partial).  Current line count: **549**.

**M6 — L6 Subtyping.**  ⚠️ PARTIAL — sync only.  SessionSubtype.h ships Gay-Hole 2005 sync subtype rules (Send/Recv/Select/Offer/Loop, payload subsort).  **NOT SHIPPED**: precise async subtyping ⩽_a via SISO decomposition (GPPSY23, Task #348).  Without ⩽_a, every async channel can only have sync subtyping — every Send must rendezvous with a Recv before another Send.  Task #338 (completed — sync subtyping shipped).  Current line count: **854**.

**M7 — L7 Safety φ family.**  ❌ NOT SHIPPED.  No SessionSafety.h header exists.  All seven SY19 φ predicates (safe, df, term, nterm, live, live+, live++) are documented but not implemented.  **Consequence**: every channel's φ choice in Part VI is currently aspirational.  Blocked on Tasks #382 (reachable_contexts BFS) + #383 (AsyncContext queue reduction).  Task #346 (pending).

**M8 — L8 Crash-stop.**  ⚠️ PARTIAL.  SessionCrash.h ships Stop, Crash<p>, UnavailableQueue, ReliableSet, has_crash_branch_for_peer_v + assert_has_crash_branch_for.  **DEFERRED**: Γ-level aggregate check that walks every Offer in every entry's local type (Task SEPLOG-BUG-4 / #368) and the prerequisite Sender-role-on-Offer (Task SEPLOG-STRUCT-1 / #367).  Per-Offer trait shipped is essentially useless for nested Offers (which is most real protocols).  Task #347 (pending — partial).  Current line count: **627**.

**M9 — L9 CSL × session.**  ❌ NOT SHIPPED.  No PermissionedSession.h header exists.  Transferable<P> payload markers, permission balance preservation across reductions, exit-permission validation — all missing.  **Consequence**: sessions and Permission<Tag> are two parallel systems that don't talk.  The framework's central thesis ("session types make CSL permissions compose") is unimplemented.  Task #333 (pending).

**M10 — The MPMC flagship.**  ❌ NOT SHIPPED.  PermissionedMpmcChannel<T, N, M, C> doesn't exist.  Depends on M6 (async ⩽_a), M9 (CSL × session), Tasks #381 (full merging), #382/383 (L7 prereqs).  Tasks #327, #328, #331, #326 (all pending).  See **Part V** for the projected design — currently descriptive, not implemented.

**M11 — L10 Pattern library + L11 Diagnostics.**  ✅ SHIPPED.  SessionPatterns.h ships RequestResponse, FanOut/FanIn, Broadcast, ScatterGather, MpmcProducer/Consumer (proto-only, no runtime), TwoPhaseCommit_Coord/Follower, SwimProbe, Handshake, Transaction_Client/Server, PipelineSource/Sink/Stage.  SessionDiagnostic.h ships 18 manifest-bug tags + CRUCIBLE_SESSION_ASSERT_CLASSIFIED macro + 26 retrofit sites across 10 headers (#388).  Tasks #341, #342, #337 (all completed).  Current line counts: SessionPatterns.h **793**, SessionDiagnostic.h **633**.

**M12 — Crucible channel adoption.**  🔌 NO CALLERS.  Zero production Crucible channels currently use SessionHandle.  The framework sits on a shelf.  Tasks #355-#358 (K-series) are pending; Task SEPLOG-INT-1..4 (sub-tasks #384-#387) added during this status pass.  PermissionedSpscChannel.h ships as the missing primitive — TraceRing, MetaLog, ChainEdge, Augur all need to be wired to use it (or analog wrappers).  This is **WHERE THE VALUE LANDS** and where the framework is currently weakest.

**Appendix-D combinators shipped (orthogonal to L-numbering)**:
- ✅ SessionDelegate.h — Honda 1998 throw/catch (#337) — **835 lines**
- ✅ SessionCheckpoint.h — Appendix D.2 (#362) — **451 lines**
- ✅ SessionContentAddressed.h — Appendix D.5 (#361) — **428 lines**

**v2 candidates (deferred)**:
- L12 Choreographic projection (M13+)
- Full-depth async subtyping via complete Rumpsteak-style decision procedure (M14+)
- Timed sessions (F.4) formalized (M15+)
- Content-addressed quotient sessions (F.5) generalized beyond Cipher (M16+)
- Dynamic participants (F.1) formally integrated with precise async + crash-stop (M17+)

**Headline shipping ratio**: 8/12 milestones at SHIPPED or PARTIAL; 4/12 at NOT SHIPPED.  Lines of code in tree: **8,397 across 12 headers** (about 70% of the projected 12K).  Production callers using the framework: **0**.

The 8,397/12,000 lines completion ratio is misleading without context.  The shipped portion is the STRUCTURAL skeleton (combinators, contexts, queues, global types, association check, sync subtyping, patterns, diagnostics).  The NOT-SHIPPED portion is the SEMANTIC core: φ predicates that actually verify safety/liveness, CSL × session integration, async subtyping, full merging.  Lines-per-feature is heavily back-loaded — see Part IX for the per-conjunct breakdown.

---

# Part VIII — Open questions

Seven open items. Each is tagged with a suggested owner phase.

**Q1 — How much of SY19's seven liveness levels does Crucible actually need?** Reading Part VI, the channels mostly use `safe`, `df`, `live+`, and `nterm`. The `live++`, `term` variants are used by only a few channels. Answer deferred to M7: the L7 Safety layer implements all seven, we observe which are used after M12 adoption, retire unused ones.

**Q2 — Bounded-queue decidability margin.** Crucible's rings are bounded, so bounded-buffer async subtyping is decidable. But what if a future channel wants unbounded? Policy decision deferred: Crucible does NOT support unbounded async channels; any future proposal for one requires architectural review + this document's update to cover undecidability fallbacks.

**Q3 — Dynamic participants — formal combination.** §F.1 gives a workaround but not a full formalization. A dedicated paper's worth of research. Phase 2 beyond M12 — collaborate with Yoshida group or Deniélou group to combine dynamic MPST with precise async + crash-stop + queue types.

**Q4 — Cross-language boundary subtyping.** Vessel adapters (PyTorch, JAX, Python, C++, Rust) each emit IR001 ops; we claim each Vessel's session type is a subtype of the canonical Frontend type (§IV.1). The formal mechanism: each Vessel FFI's outgoing session is compared to Vigil's incoming session via `is_subtype_sync_v` at the FFI generation step. Codegen validates this. Unresolved: what happens if a language has a STRICTLY WEAKER type system that can't prove the subtype? Proposed: require the FFI to pass a proof token generated by the `verify.sh` tool offline.

**Q5 — μ-calculus verifier in compile time vs offline.** The L7 layer supports both. For small Γ's (< 100 LTS states), compile-time is fine (< 100 ms template cost). For large Γ's (full training-step with 64 participants and queue types), compile-time model-checking is too slow; we offload to mCRL2 offline and cache proof certificates in Cipher. Unresolved: the caching mechanism. Proposed: Cipher cold-tier federation-shareable certificates keyed by Γ's structural hash.

**Q6 — Merge operator (coinductive full merging).** PMY25 §4 presents coinductive full merging as the right merge for projection onto non-sender / non-receiver roles. Implementation complexity is high; for v1 we use plain merging (all branches project identically) and document the limitation. Unresolved: the cost of the weaker merging in real protocols. Measurement deferred to after M12 adoption.

**Q7 — Runtime vs compile-time φ verification trade-off.** Some protocols have runtime-determined participant counts. For these, we can't compile-time-verify φ; we can only verify it at run-config-time. Proposed: a runtime verifier matching SY19's mCRL2-based MPSTK; we invoke it when the Keeper boots and the actual N participants is known. Adopts the same μ-calculus formulas as compile-time. Cost: ~seconds at Keeper startup (negligible).

---

# Part IX — Honest assessment vs current shipping state

This section was added 2026-04-24 as part of the doc-refresh pass (#389) to capture the gap between the framework's CLAIMS (Parts I–VI) and its CURRENT SHIPPING STATE.  Previous versions of this document presented the design as if the entire vocabulary were enforced; in practice, the type-level skeleton ships but the semantic core is significantly deferred.  This section is the project's brutal-honesty checkpoint — kept current as M7/M9/M10 progress.

## IX.1 The structural-vs-semantic gap

What the framework has at the **structural** level (8,397 lines across 12 headers, 102/102 tests passing in debug):
- Combinator grammar (Send/Recv/Select/Offer/Loop/Continue/End)
- Duality computation + involution check
- Sequential composition
- Well-formedness predicate
- Typing context Γ (composition, lookup, mutation, domain)
- Queue type σ (enqueue/dequeue/contains/count, single-state bounded check)
- Global type G + projection (with PLAIN merge only)
- Association Δ ⊑_s G (single-point check; preservation theorem deferred)
- Synchronous subtyping (Gay-Hole 2005)
- Crash-stop primitives (Stop, Crash<p>, ⊘, ReliableSet)
- Pattern library (10+ ready-made shapes)
- Diagnostic vocabulary (18 tags, retrofit through 26 user-facing assertions)
- SessionHandle CRTP base with abandoned-protocol detection (debug) + zero-cost release (sizeof equals Resource)
- Three Appendix-D combinators (Delegate, Checkpointed, ContentAddressed)
- Negative-compile harness (26 tests verifying the framework REJECTS bad code)

What the framework does NOT have at the **semantic** level:
- ❌ φ predicates that actually verify properties.  Every is_safe_v / is_df_v / is_live+_v claim in Part VI is currently aspirational — those metafunctions don't exist (Task #346 / SessionSafety.h).  The Part VI table reads as "channel X requires φ(N)" but no compile-time witness can be produced for any channel today.
- ❌ Async subtyping ⩽_a (Task #348).  Sync-only is too strict for MPMC (Section §V.4 documents the async-pipelining requirement; the type system can't currently certify it).
- ❌ Coinductive full merging (Task #381).  Plain merge cannot project Raft / 2PC-with-multi-followers / any DIVERGING multiparty global type onto third-party roles.
- ❌ CSL × session integration (Task #333).  Permission and SessionHandle are parallel systems that don't talk; Transferable<P> doesn't exist as a payload marker.
- ❌ Causality analysis ≺_II / ≺_IO / ≺_OO (Task #340).  Without these, deadlock-freedom claims have no foundation.
- ❌ Reachable-contexts BFS (Task #382).  L7's φ predicates need this; without it, L7 can't be implemented.
- ❌ Association preservation theorem (HYK24 Thm 5.8).  Single-point association check shipped; preservation across reductions needs reduction semantics from L7.

## IX.2 The "no production callers" critique

**Headline gap**: ~8,400 lines of header pile, **zero** Crucible production channels currently use SessionHandle or PermissionedSpscChannel.

The K-series tasks (#355 Vessel dispatch, #356 KernelCache, #357 InferenceSession, #358 CNTP Raft) are all pending.  Sub-tasks #384-#387 (TraceRing/MetaLog/ChainEdge/Augur wiring, added during the gap-analysis pass) are all pending.  PermissionedSpscChannel ships as the missing primitive — but until at least one production channel uses it, the framework's "zero-cost in production" claim is verified only by synthetic micro-benchmarks (sizeof checks via /tmp/check_sizeof.cpp, not real workloads).

The framework is currently a **museum piece**: well-documented, internally-consistent, exhaustively self-tested at the type level, with zero industrial use.

## IX.3 The "claimed φ vs enforced φ" matrix

For every channel listed in **Part VI**, the chosen φ is currently UNVERIFIED.  Replication of Part VI's table with an honest enforcement column:

| Channel | Claimed φ | Currently enforced |
|---|---|---|
| IV.1–IV.34 (all 34 channels) | per Part VI table | none (Task #346 not shipped) |

Until L7 SessionSafety.h lands, the φ-table in Part VI documents intent, not protection.

## IX.4 The "claimed costs vs measured costs" gap

**Part V §V.10** cost budget:
- Compile-time cost claims (~70-120ms per MpmcRing instantiation, full breakdown table) — **PROJECTED, NOT MEASURED**.  bench/bench_session_compile_time.cpp doesn't exist (Task #392).
- Runtime cost claims (zero-overhead release-mode handles via EBO) — **PARTIALLY VERIFIED**.  SessionHandle's zero-overhead claim was empirically validated for sizeof (#366 commit body).  PermissionedSpscChannel's zero-overhead has compile-time static_asserts on sizeof but no runtime micro-bench yet.

The MPMC flagship's cost-budget table is reading as if measured; it is in fact projected from underlying-primitive costs.  Real-workload bench landing depends on at least M9 (CSL × session) being shipped, which depends on M7 prerequisites.

## IX.5 The shipping-direction warning

If you read this document end-to-end **before reading the headers in the tree**, you would conclude the framework is more complete than it is.  The mitigation:
1. Part VII's status-tagged milestones (above, just refreshed)
2. This Part IX
3. The doc-comment headers of each shipped file say what's IN that file
4. `git log --oneline include/crucible/safety/Session*.h include/crucible/concurrent/Permissioned*.h` shows actual shipping history

If you came here to validate a "session types are shipping in Crucible" claim before bringing the framework into a downstream system: **don't yet**.  Wait for K1 (TraceRing or Vessel as typed session), L7 (φ predicates actually verifying), and L9 (CSL × session) to ship.  All three are in the queue with tasks; ETAs are 4–16 weeks aggregate of focused work.

## IX.6 Suggested reading order for new contributors

Given the gap-state above, the right onboarding flow:
1. Read this Part IX first.
2. Read **Parts II + III** for theory + intended C++ realization.
3. Read **headers in the tree** — Session.h (1185 lines), then SessionContext.h, SessionQueue.h, SessionGlobal.h, SessionAssoc.h, SessionSubtype.h, SessionCrash.h, SessionPatterns.h, SessionDiagnostic.h, plus Delegate/Checkpoint/ContentAddressed combinators.  These are what's REAL.
4. Read **Part IV channel catalog** with the understanding that none of the per-channel φ choices are currently enforced.
5. Read **Part V MPMC flagship** as a forward-looking design document — reading the source-code MpmcRing in concurrent/ is what's real today.
6. Read **Parts VI + Appendices** for reference.

If you find a discrepancy between this document and the headers (or between this section and the per-milestone status in Part VII): trust the headers, then update this document.  Discrepancies are bugs in the doc, not in the code.

---

# Appendix A — Paper attributions

Eight papers; each contributes one or more named theorems directly cited in this document.

**Honda 1998** — "An interaction-based language and its typing system" (PARLE). Origin of binary session types: Send/Recv/Select/Offer/Loop/End combinators; duality theorem; delegation. All of §II.3 and the L1 layer descend from this paper.

**Dardha-Giachino-Sangiorgi 2012** — "Session types revisited" (POPL). Encoding of session types into linear π-calculus with variant types; the Kobayashi encoding. Justifies why Crucible's existing Permission<Tag> + Linear<T> primitives ARE session types under the hood. §II.1, §III.L0.

**Honda-Yoshida-Carbone 2008** / **JACM16** — "Multiparty asynchronous session types" (POPL 2008; JACM 2016). The classical definition of multiparty session types: global type G, projection G↾p, queue types σ, three causality relations (≺_II, ≺_IO, ≺_OO), coherence, consistency, subject reduction + session fidelity + progress theorems. §II.4, §II.6, §III.L4, §III.L7.

**Gay-Hole 2005** — "Subtyping for session types in the pi calculus" (Acta Informatica). Sync subtyping rules (Send covariant, Recv contravariant, Select subset, Offer superset, Loop coinductive). §II.7, §III.L6.

**Scalas-Yoshida 2019** — "Less is more: multiparty session types revisited" (POPL 2019). Parametric safety φ over typing context Γ; seven canonical φ levels (safe, df, term, nterm, live, live+, live++); decidability on finite-state Γ; mCRL2 verification. The framework that unifies all the other papers. §II.9, §II.10, §III.L7.

**Ghilezan-Pantović-Prokić-Scalas-Yoshida 2023** — "Precise subtyping for asynchronous multiparty sessions" (ACM TCL). SISO tree decomposition; six refinement rules ([ref-in], [ref-out], [ref-A], [ref-B], [ref-end], + sort-subtyping); precise async subtyping ⩽_a (sound + complete). §II.7, §III.L6.

**Barwell-Scalas-Yoshida-Zhou 2022** — "Generalised multiparty session types with crash-stop failures" (CONCUR 2022). Synchronous crash-stop: Stop type, crash label, reliability R ⊆ Roles, crash-aware φ; mpstk-crash-stop verification tool. §II.8, §III.L8.

**Barwell-Hou-Yoshida-Zhou 2023** — "Designing asynchronous multiparty protocols with crash-stop failures" (LMCS). Async crash-stop journal version: unavailable queue ⊘, runtime global-type syntax with crash annotation p† and en-route p†⇝q; association Γ;Δ ⊑_R ⟨C;G⟩; NBAC case study. §II.8, §IV crash channels.

**Pischke-Masters-Yoshida 2025** — "Asynchronous global protocols, precisely" (arXiv 2505.17676). First sound top-down async MPST with precise async subtyping ⩽_a; en-route transmissions as runtime global-type primitives; queue types σ in local contexts; balanced+ well-formedness (PMY25 Def 6.1); liveness theorem Thm 13. §II.5, §II.6, association-based reasoning throughout.

**Hou-Yoshida-Kuhn 2024** — "Less is more revisited" (2025 arXiv). Proved SY19's subject-reduction proofs using full merging were flawed; introduced association Δ ⊑_s G as the correct invariant. §II.5 (central).

Additional related: **Wadler 2012** (propositions as sessions, session-types-as-linear-logic), **Montesi 2013** (choreographic programming, v2 basis), **Cutner-Yoshida-Vassor 2022** (Rumpsteak's bounded-depth decision procedure, decidability basis), **Vieira-Parreaux-Wasowicz 2008** (transactional session types, speculative-decoding inspiration), **Deniélou-Yoshida 2011** (dynamic MPST, §F.1 basis), **Bocchi-Yang-Yoshida 2014** (timed MPST, §F.4 basis), **Mostrous-Yoshida 2015** (higher-order π-calculus sessions, §F.6 basis).

---

# Appendix B — Worked example: RequestResponse end-to-end

How a user-facing abstraction (RequestResponse) compiles down to L1 combinators, is checked by L2-L9, and executes at runtime.

**User-facing definition** (L10 pattern library):
```cpp
template <typename Req, typename Resp>
using RequestResponse = Loop<Send<Req, Recv<Resp, Continue>>>;
```

**Usage**:
```cpp
using QueryProto = RequestResponse<QueryReq, QueryResp>;
auto [client, server] = establish_channel<QueryProto, SomeResource>(res);

// Client side:
auto client2 = client.send(QueryReq{...});
auto [resp, client3] = client2.recv();
// client3 has type SessionHandle<QueryProto restarted at loop>;

// Server side:
auto [req, server2] = server.recv();
auto server3 = server2.send(QueryResp{...});
```

**What's checked at compile time**:
- `is_well_formed_v<QueryProto>` — yes, Loop with guarded Continue (L1)
- Dual check: `std::is_same_v<QueryProto, dual_of_t<dual_of_t<QueryProto>>>` — yes (L1)
- Client's `.send(req)` method exists because current handle type is `SessionHandle<Send<QueryReq, …>, …>` (L1 specialization)
- Server's `.send(resp)` exists because post-Recv the handle is `SessionHandle<Send<QueryResp, …>, …>`
- If user tries `server.send(QueryReq{…})` before recv — compile error; no such method on the initial-state handle (L11 diagnostic: `ProtocolViolation_State`)

**What's checked at compile time by higher layers** (if annotated):
- Γ composition: Γ = {(this_session, Client): QueryProto, (this_session, Server): dual_of_t<QueryProto>} is well-formed (L2)
- Subtype check for refinement: if user substitutes `SubtypedResp` for `QueryResp` where `SubtypedResp <: QueryResp`, the refined protocol is `RequestResponse<QueryReq, SubtypedResp>` and the subtype check `is_subtype_sync_v<refined, original>` passes (L6)
- Safety predicate: `is_safe_v<Γ>` — the LTS has no mismatched labels; verified by compile-time LTS walk (L7)
- Liveness: `is_live_plus_v<Γ>` under fair scheduling; mCRL2 verifies offline (L7 fallback)

**What happens at runtime**:
- `client.send(req)` → compiles to `underlying_transport.send(serialize(req))` — typically one MpmcRing try_push or one RDMA write.
- `client.recv()` → compiles to `underlying_transport.recv()` — one try_pop or one RDMA completion poll.
- Zero runtime cost for session-type machinery. All zero-overhead discipline per §III.4.

**Compile error example** (wrong-state send):
```cpp
auto [client, server] = establish_channel<QueryProto, Res>(res);
auto client2 = client.send(QueryReq{...});
// Try to send again without recv — should be a compile error:
auto client3_bad = client2.send(QueryReq{...});  // error
```
Error message (simulated with diagnostic layer):
```
error: static assertion failed: crucible::session: ProtocolViolation_State:
       .send() called on a handle in Recv-expected state.
       Current state: Recv<QueryResp, Continue>
       Suggested: call .recv() first to advance to the next Send state.
note: in instantiation of 'SessionHandle<Recv<QueryResp,Continue>,Res>::send(…)'
```

The diagnostic cuts 2K lines of template-instantiation noise to 3 lines of actionable information.

---

# Appendix C — Glossary

| Term | Definition |
|---|---|
| **Association** | The invariant Δ ⊑_s G: Δ's entries are subtypes of G's projections; HYK24's fix to SY19 |
| **Balanced+** | PMY25 well-formedness: en-route count bounded; makes precise async subtyping sound |
| **CNTP** | Crucible Native Transport Protocol; five-layer stack per CRUCIBLE.md §5 |
| **Combinator** | Session-type building block: Send, Recv, Select, Offer, Loop, End, etc. |
| **Compositionality theorem** | §II.12.4; well-typed sub-systems compose into a well-typed whole under disjointness |
| **Conversation** | §II.1; an ordered sequence of message-passing events among named participants |
| **Crash-stop** | Failure model: participants may die; others detect + handle via crash branches |
| **CSL** | Concurrent Separation Logic; permission-balanced frame rule; lifted to sessions at L9 |
| **Duality** | For binary sessions: two endpoints' types must flip every Send↔Recv, Select↔Offer |
| **En-route transmission** | PMY25; a globally-specified send-event that has occurred but not yet been received |
| **Frame rule** | CSL's parallel-composition rule; lifted to protocols at §II.12.1 |
| **Global type G** | Bird's-eye multiparty protocol specification; projected to local types per role |
| **LTS** | Labeled Transition System; the state machine derived from Γ over reduction events |
| **Precise async subtyping ⩽_a** | GPPSY23; sound + complete refinement for async; undecidable in general, decidable bounded |
| **Projection G↾p** | Extract participant p's local session type from global G |
| **Queue type σ** | Bounded FIFO of pending messages at a channel; part of async typing context |
| **Reliability set R** | Participants assumed not to crash within the protocol's scope |
| **SISO tree** | Single-input single-output session tree; the decomposition basis for ⩽_a |
| **Session type** | Syntactic description of a protocol from one participant's view; checked at compile time |
| **Stop type** | Local session type for a crashed endpoint; Stop ⩽ T for all T |
| **Subtyping T ⩽ T'** | "Process of type T can safely replace one of type T' in any context" |
| **Typing context Γ** | Compile-time map from (session, role) pairs to local types |
| **Unavailable queue ⊘** | Marker for a queue to a crashed recipient; subsequent sends are dropped |
| **φ (parametric safety)** | A user-chosen predicate on Γ preserved by reduction; unifies all theorem variants |

---

# Appendix D — Frontier inventory

Six open frontier items (§II.11 summary, here expanded with formal sketches).

## D.1 Dynamic participant sets

**Problem.** Classical MPST fixes the participant set at protocol declaration. Canopy's membership is dynamic: Keepers join, die, leave. Deniélou-Yoshida 2011 exists but has not been combined with precise async + crash-stop + queue types.

**Crucible's workaround.** Static upper bound N_max × M_max at compile time; runtime refinement to actual active set. Protocol has a "membership-change subprotocol" that, on member join/leave/death, runs a consistent-hash rebalance and updates Γ. The L2 Context machinery admits "open" Γ with placeholder entries for yet-to-join participants.

**Formal sketch.**
- Parameterize G over participant-count predicate φ_N (e.g., φ_N(n) = (1 ≤ n ≤ N_max))
- At any runtime moment, the "effective" G is G restricted to the current-active set A ⊆ [N_max]
- Reduction rule [GR-Join]: new member P joins → A := A ∪ {P}, G_effective expands
- Reduction rule [GR-Leave]: P leaves gracefully → A := A \ {P}, G_effective contracts, P's session ends via close()
- Reduction rule [GR-Die]: P crashes → A := A \ {P}, all outgoing queues-to-P become ⊘

**Association under dynamic A.** Δ ⊑_s G_effective always; as A changes, Δ changes accordingly. Safety preserved if each rule preserves Δ's association with G_effective — this is a theorem we need to prove (deferred).

**What this would need.** A paper combining Deniélou-Yoshida 2011 with PMY25 + BHYZ23. Probably 40-80 pages. Not Crucible's to write; we document the workaround and flag as frontier.

## D.2 Speculative sessions with rollback

**Problem.** Speculative decoding's draft → target verify → accept-or-reject is a session with rollback. Transactional session types (Vieira 2008) don't combine with MPST + async + crash-stop.

**Crucible's approach.** The `CheckpointedSession<ProtoSafe, ProtoRollback>` combinator from §II.12.2. At a declared checkpoint, the session state is serialized; on rollback, it is restored and execution continues from the checkpoint.

**Formal sketch.**
- Add checkpoint primitive `mark_checkpoint(cp_id)` to the session calculus; it pushes (cp_id, current_state) onto a stack.
- Add rollback primitive `rollback(cp_id)` that pops and restores.
- Reduction rules: `mark_checkpoint` has no observable effect; `rollback` reduces to the state at the checkpoint.
- Safety: checkpoint and rollback preserve φ iff the checkpoint state is consistent (all participants know about the checkpoint and agreed to the serialization point).

**What this would need.** A paper combining Vieira 2008 with PMY25. Deferred.

## D.3 Persistent sessions across crashes

**Problem.** Training runs cross crashes via Cipher checkpoints. Session logically spans the crash; the runtime loses and reloads state from Cipher. Persistent session types are not mainstream.

**Crucible's approach.** The FLR recovery protocol (§IV.33) has an implicit "load-cipher-and-resume" branch; any reduction from crash-state reduces to the same protocol-state as the last checkpoint. We encode this via Checkpointed combinator (§D.2) + an implicit session save-on-checkpoint.

**Formal sketch.** `PersistentSession<Proto, CheckpointInterval>` = `Checkpointed<Proto, ReplayFromCheckpoint>` with CheckpointInterval steps between consecutive checkpoints. Runtime's FLR detection invokes rollback to most-recent-checkpoint; replay from there. Identical to §D.2 but with periodic automatic checkpointing.

**What this would need.** Integration with Cipher's three-tier storage + formal-tracking of event-source trace; not session-type-specific but session-type-using. Deferred to Phase 2 (after M12).

## D.4 Timed sessions

**Problem.** Latency-critical paths (HOT-thread dispatch < 500 ns, ChainEdge < 400 ns PCIe, collective timeout 5 s). Timed session types (Bocchi-Yang-Yoshida 2014) exist but aren't mainstream in MPST.

**Crucible's approach.** Attach phantom latency-class tags: `HotSession<P>` / `WarmSession<P>` / `ColdSession<P>` wrap a protocol P with a timing budget. Runtime bench harness (Policy::production, CRUCIBLE.md §16.9) measures against the budget.

**Formal sketch.** Timed session type `T under B` where B is a compile-time budget. Budget composes: sequential adds, parallel maxes, choice maxes. Timeout is a first-class protocol event: every `Recv` has an implicit `Branch<Timeout, ErrorRecovery>`. Runtime enforces via deadlines on the underlying transport.

**What this would need.** Integration of Bocchi-Yang-Yoshida 2014 with PMY25. Deferred.

## D.5 Content-addressed protocol dedup

**Problem.** Cipher's L1/L2/L3 entries are content-addressed; two sessions transmitting identical payloads can share. Not in classical MPST.

**Crucible's approach.** The `ContentAddressed<T>` payload marker commutes with Send: `Send<ContentAddressed<T>, K>` means "conceptually send T but elide wire bytes if recipient has hash(T)". Recipient's state transition depends only on content hash.

**Formal sketch.** Add an "equivalence-by-content" quotient to the reduction relation: two reducts that differ only in "delivered via wire" vs "delivered via cache-hit" are equivalent. Safety + liveness preserved if the cache-hit delivery is reliable (hash collision probability negligible).

**What this would need.** A quotient-session-type paper; short (~20 pages). Combinable with existing MPST machinery without conflict. Candidate for Crucible-team-written paper.

## D.6 Cross-layer nesting

**Problem.** CNTP Layer 1 carries Layer 2-5 payloads. Each outer layer's payload IS a session type. Higher-order session types (DGS12 §5, Mostrous-Yoshida 2015) give the mechanism.

**Crucible's approach.** Declare Layer 1's payload type as `OpaqueProtocol<HigherLayerProtocol>`. Layer 1 verifies length + integrity + encryption; higher layer verifies protocol. Opaque at one level, transparent at its own.

**Formal sketch.** Channel-carrying sessions. `Channel<T>` is a value type that can be sent on another channel's Send<Channel<T>, K>. The receiver gains a Channel<T> handle and may use it. Compositionality: Layer 1's safety + Layer N's safety ⇒ composed safety, provided Layer 1 delivers reliably.

**What this would need.** Straightforward integration of DGS12 §5 with modern MPST; likely already implicit in Mostrous-Yoshida 2015. Formalize for Crucible in an internal note.

---

# Appendix E — How to add a new channel to Crucible

Operational checklist. A contributor who wants to add a new typed communication channel follows these ten steps. Each step is independent; each produces a reviewable artifact.

**Step 1 — Identify the channel's participants and messages.** What roles communicate? What messages flow? Write a short English description. 5-10 lines.

**Step 2 — Decide top-down vs bottom-up.** Does the channel have a natural global-type description (e.g., a structured protocol like Raft, 2PC, all-reduce) or is it a local coordination between two endpoints (e.g., SPSC ring, one-shot RPC)? Top-down → write G; bottom-up → write per-participant T's directly.

**Step 3 — Write the session types.** In a new header `include/crucible/safety/sessions/<channel_name>.h`:

```cpp
namespace crucible::sessions::<channel_name> {

    // Phantom tags for participants
    struct RoleA; struct RoleB; // …

    // Global type G (if top-down)
    using G = /* using the L4 SessionGlobal combinators */;

    // Local types
    using T_A = /* project_t<G, RoleA> or hand-written */;
    using T_B = /* project_t<G, RoleB> or hand-written */;

} // namespace
```

**Step 4 — Declare the channel's context Γ and reliability set R.** Which session tag? Which roles in R?

```cpp
using Gamma = Context<
    Entry<MySessionTag, RoleA, T_A>,
    Entry<MySessionTag, RoleB, T_B>
>;
using R = ReliableSet< /* roles that don't crash */ >;
```

**Step 5 — Choose φ.** From Part VI's table or by analogy with similar channels. Document the choice:

```cpp
static_assert(is_safe_v<Gamma>);
static_assert(is_df_v<Gamma>);
static_assert(is_live_plus_v<Gamma>);
static_assert(is_crash_safe_v<Gamma, R>);
// whichever conjuncts apply
```

**Step 6 — Write composition witnesses.** Which adjacent channels does this compose with? Sequential? Parallel? Delegation? Document each with a static_assert:

```cpp
// Sequential composition with §IV.<other channel>
static_assert(compatible_sequential_v<Gamma, OtherGamma>);

// Parallel composition with §IV.<yet another>
static_assert(compatible_parallel_v<Gamma, YetAnotherGamma>);
```

**Step 7 — Write the runtime handle code.** Implement the actual send/recv paths on top of the transport (MpmcRing, RDMA, io_uring, etc.):

```cpp
class ChannelResource { /* the underlying transport */ };

auto establish_channel() -> std::pair<
    SessionHandle<T_A, ChannelResource>,
    SessionHandle<T_B, ChannelResource>
>;
```

**Step 8 — Add the channel to Part IV of this document.** New subsection (number by insertion order) with: scope, transport, participants, G, local types, σ, R, φ, composition, cross-ref.

**Step 9 — Add φ conjuncts to Part VI's table.** New row with the channel name and its chosen φ.

**Step 10 — Write tests.**
- Positive: `test/session/test_<channel>_protocol.cpp` — exercises the happy path.
- Negative: `test/safety_neg/test_<channel>_misuse.cpp` — verifies that protocol violations fail to compile.
- Crash: `test/session/test_<channel>_crash.cpp` — simulates peer crash, verifies crash branch fires.
- Under mCRL2 (if applicable): emit to `tools/mcrl2_export/<channel>.mcrl2` and run verify.sh.

**Optional step 11 — Add to Cipher federation.** If this is a cross-run shareable protocol (e.g., a CNTP collective algorithm), register the certificate in Cipher L1 cold tier for federation:

```
crucible cipher publish --certificate <channel>_certificate.json --namespace <ns>
```

**Total effort per channel.** Well-scoped: ~2-4 hours for a simple binary channel; ~1-2 days for a complex multiparty with crash-stop; ~1 week for a flagship (MPMC, Raft). Most channels are the simple case.

---

*End of document. Last updated: 2026-04-24 (status-honesty pass — added Part IX, refreshed Part VII milestone status, marked Part V cost budget as PROJECTED, updated stale line counts in Part III; see #389). Authors: Crucible team. This document is a living artifact; it will be updated as the twelve milestones in Part VII complete, as open questions in Part VIII are resolved, and as frontier items in Appendix D reach the literature. Discrepancies between this document and observed reality will be reconciled in favor of reality — see Part IX for the current gap inventory.*









