# Session types: literature review and correct design

Date of the review: 2026-09-23.

This document records a review of session-type research from 2022 to 2026. It gives the correct design for the session layer that follows from that research. It also lists the defects in the present code and in our own design documents that the review found.

The design in this document replaces the parts of `misc/session_types.md` that it contradicts.

## 1. Scope and method

The review covers multiparty session types, binary session types, linear-logic session types, session types with separation logic, and implementations of session types.

We collected papers from these sources:

- The arXiv API, with phrase queries and author queries
- OpenAlex citation snowball, two rounds, from 17 seed papers
- Crossref, DataCite and the Dagstuhl DROPS pages for venues and DOIs
- Web search for the proceedings of POPL, PLDI, ICFP, OOPSLA, ECOOP, ESOP, CONCUR, ITP and the PLACES and ICE workshops.

The sweep found more than 380 candidate papers. After deduplication and a topic filter, 301 papers from 2022 or later remain. The citation snowball did not get to a fixpoint, because rate limits stopped three of the services. The true number is larger than 301.

We examined each author list in the bibliography against Crossref or DataCite.

The full list of 301 papers, by topic, is in `misc/session_types_catalog.md`.

Markers in this document:

- **[R]** — we read the paper in the PDF for this review
- **[S]** — we checked the cited sections, pages or theorems, but not the full paper.

## 2. The correct design

These rules give the target design for the session layer. Each rule names the paper that supports it. Section 4 gives the details.

1. **The top-down path is the primary path.** A global type goes through a well-formedness check, then projection, then subtyping. Liveness then holds by construction. Do not check liveness by a walk over local types. (Pischke and Yoshida, OOPSLA 2026.)
2. **Well-formedness is balanced+.** Each participant must have a bounded depth in each reachable global type. The count of en-route messages for each pair of roles must agree across branches. No en-route message can occur below a recursion binder. The check is structural and decidable. A bounded buffer does not give balanced+. (Pischke, Masters and Yoshida, v4, Definitions 13 to 17.)
3. **Projection is the sound and complete projection of Tirore, Bengtson and Carbone.** A role that has no part in a loop projects to `End`. It never projects to `Loop<Continue>`. Coinductive full merge is necessary to type the same live sessions as a bottom-up check. (ITP 2023 and JAR 2025. Pischke and Yoshida, OOPSLA 2026.)
4. **Recursion is iso-recursive, and it unfolds in one direction only.** A congruence that folds a loop back breaks subject reduction under subtyping. (Ekici, Kamegai and Yoshida, ITP 2025.)
5. **Each ordered pair of roles has its own FIFO queue.** A queue that roles share is the case that makes the 2008 subject-reduction theorem false. Soundness claims rest on the association relation, not on the 2008 proof. (Tirore, Bengtson and Carbone, ECOOP 2025. Hou, Yoshida and Kuhn, TCS 2026.)
6. **Subtyping has two parts.**
   - The synchronous relation uses the quadratic algorithm on type graphs. It is reflexive on each combinator. One walk gives the verdict and its reason. (Udomsrirungruang and Yoshida, POPL 2025.)
   - The asynchronous relation is the bounded, sound and incomplete check of Rumpsteak. The bound is a template parameter equal to the capacity of the channel. On the top-down path, refinement relative to the global type is decidable. A check that cannot prove the relation rejects the protocol. (Cutner, Yoshida and Vassor, PPoPP 2022. Li, Stutz and Wies, ESOP 2024.)
   - Precise asynchronous subtyping is undecidable. Do not claim to decide it. A relation that we use must be closed under duality. (Padovani and Zavattaro, TOPLAS 2026.)
7. **Deadlock freedom has two conditions.** (Jacobs, Hinrichsen and Krebbers, LinearActris, POPL 2024.)
   - A handle cannot disappear before `End` without an effect. The destructor must abort, or it must send a cancellation to the peer. This rule applies in Release builds too. A callback API, where the library owns the handle, gives the same result by construction. (Lagaillardie, Neykova and Yoshida, ECOOP 2022. Thiemann, ICFP 2023.)
   - A channel is made in the fork shape. The mint starts the peer with one endpoint and returns the other endpoint. No mint gives the two endpoints to one caller. This keeps ownership acyclic.
   - Deadlock freedom across more than one session is possible only with acyclic ownership or with priorities. The liveness result covers one session only. (van den Heuvel and Pérez, LMCS 2024. Kokke and Dardha, LMCS 2023.)
8. **Ownership moves with the message.** (Hinrichsen, Bengtson and Krebbers, Actris 2.0, LMCS 2022.)
   - A send consumes the permission. The matching receive produces it.
   - The payload check examines each component of the payload. A permission inside a `std::pair` or a similar type must not escape the check.
   - The mint consumes the permission tokens that it receives.
   - A `Borrowed` payload has a matching `Recv<Returned>`, or it uses ordered linear borrowing. A borrowed prefix that goes to a different thread blocks each operation on the suffix until the prefix is consumed. (Saffrich, Spaderna, Thiemann and Vasconcelos, OOPSLA 2025, with its errata.)
   - No code can make a read proof without a real permission.
9. **A crash class is local to the session.** It is metadata on the crash branch, together with the set of reliable roles and the marker for an unavailable queue. It is not a foundation lattice. It is not a fixy control atom. (Barwell, Hou, Yoshida and Zhou, LMCS 2025. Peters, Nestmann and Wagner, LMCS 2023.)
   - The fair-path definition must include the clause for crash detection.
   - The literature has no type system for crash-recover. Do not claim it.
   - Checkpoint and rollback use an explicit coordinated choice label. A design where each endpoint selects alone is unsound. (Mezzina, Tiezzi and Yoshida, LMCS 2025.)
10. **A streaming substrate is available only through its session handle.** Each push and each pop moves the protocol one step. The implementability check depends on the kind of network. Peer-to-peer FIFO, mailbox and bag networks match our SPSC, MPSC and MPMC substrates. (Li and Wies, PLDI 2026.)
11. **One transition algebra replaces the duplicated walks.** Duality, composition, subtyping and well-formedness are one fold over the protocol spine. This algebra is a peer of `Graded`, because protocol subtyping has mixed variance. Each primary template fails closed.
12. **One handle type replaces the twins.** `Handle<Proto, PS, Resource, LoopCtx, Policy>` replaces the plain handle and the permissioned handle. One decorator design covers the crash transport first, then the recording handle.
13. **Verification uses differential tests against published mechanisations.** A generator makes protocols. A third-party Rocq development gives the expected verdict. Our C++ relation must agree. We own no proofs. Section 8 lists the oracles.
14. **The compile-time budget follows the known bounds.** Synchronous subtyping is O(|T1|·|T2|). Plain-merge projection is O(|G| log |G|). Full-merge projection is PSPACE. We must measure the template cost of the session layer against Coconut, which moved its check out of templates into a GCC pass for compile time. (Udomsrirungruang and Yoshida, POPL 2025. Alsubhi, Dardha and Gay, SCP 2025.)

## 3. Defects in the present code

Each item gives the file, the defect and the rule in section 2 that it breaks. A compile probe confirmed the items marked with (probe).

### 3.1 Old session layer, `include/crucible/sessions` and `include/crucible/bridges`

| File | Defect | Rule |
|---|---|---|
| `SessionGlobal.h`, `ProjectImpl<Rec_G>` | A role with no part in a loop projects to `Loop<Continue>`. `is_well_formed` accepts that type in the old and the new tree. (probe) | 3 |
| `SessionGlobal.h`, `is_global_well_formed` | The check has no balancedness condition and no en-route count. | 2 |
| `SessionGlobal.h`, `roles_of_t` | Roles are deduplicated by a 64-bit hash. A collision drops a role. Projection is not gated on well-formedness. | 3 |
| `SessionQueue.h` | One `Queue<...>` holds all pairs, and `head_queue_t` reads the global head. | 5 |
| `SessionPhi.h`, lines 45-46, 146-147, 242-243 | Three walker primaries admit by default. `Delegate<Select<>>` passes the deadlock-freedom check. (probe) | 1, 11 |
| `SessionSubtype.h` | The relation is not reflexive for a `Sender`-annotated `Offer`, a plain `Delegate` or a plain `Accept`. (probe) | 6 |
| `SessionSubtypeReason.h` | The reason walk disagrees with the subtype walk on `VendorPinned`. (probe) | 6 |
| `SessionMint.h`, lines 635-638 | `mint_permissioned_session` does not consume its permission tokens. Two mints from one `std::move(perm)` compile. (probe) | 8 |
| `SessionMint.h`, lines 661-675 | `mint_channel` returns the two endpoints to one caller. | 7 |
| `SessionMint.h`, line 151 | The vendor walk over payloads admits by default. Each fixy wrapper is outside its list. | 11 |
| `SessionPermPayloads.h`, line 152 | `is_plain_payload_v` checks only the top-level type. `std::pair<Transferable<int, X>, int>` goes out with an empty permission set. (probe) | 8 |
| `permissions/_ReadView.h`, lines 64-76 | `proto::Borrowed` can make a `ReadView` without a permission. | 8 |
| `SessionCheckpoint.h` | Each endpoint selects base or rollback alone. The two endpoints can disagree on the type on the wire. | 9 |
| `SessionCrash.h` | It cites the CONCUR 2022 fair-path definition, which does not have the crash-detection clause. | 9 |
| `AsyncPipelineSession.h`, lines 166-181 | The transports mint root permissions in the receive loop. They always wait on phase 0. | 8 |
| Streaming wirings (`ShardedGrid` 213-224, `CalendarGrid` 364-375, `ChaseLev` 245-255) | The helpers call `resource()->try_push` directly and do not step the handle. | 10 |
| `bridges/RecordingPermissionedSessionHandle.h`, lines 674, 686, 750, 760 | The epoched delegate and accept steps record the plain event. The epoch thresholds are lost on replay. | 12 |
| `SessionEventLog.h`, line 434 | Bytes from disk become enum values without a check. The record is a format on disk. | — |

### 3.2 Ported core, `include/fixy/session`

| File | Defect | Rule |
|---|---|---|
| `Stepping.h`, lines 172-177 | `DefaultAbandonmentPolicy` is `check::Off` when `NDEBUG` is set. A Release handle can disappear before `End`, and the peer then waits forever. | 7 |
| `Protocol.h`, line 380 | The primary of `is_dual_involutive` is `true_type`. A combinator added later is involutive by default. | 11 |
| `Protocol.h` | The epoch and generation lattices are not in foundation. The epoch axis of the session layer cannot be ported until they are. | — |

## 4. Findings by topic

### 4.1 Global types, projection and well-formedness

- **[R] Pischke, Masters, Yoshida. Asynchronous Global Protocols, Precisely.** arXiv 2505.17676, v4, July 2026. Conference version LNCS 16065, 2025. Journal version I&C, 2026. This is the first sound top-down system for asynchronous MPST with precise asynchronous subtyping. The association relation between a global type and optimised local types is sound and complete. Coinductive full merge and balanced+ global types are necessary for the system. Definition 17 defines balanced+. Remark 2 shows that it is decidable.
- **[R] Hou, Yoshida, Kuhn. Less is More Revisited.** arXiv 2402.16741. TCS 1076, 2026. Projection with full merge is sound. The 2019 paper found flawed proofs, not a false theorem. Association is the proof invariant. The paper corrected its definition of process liveness (Definition 4.2). Cite the corrected version.
- **[S] Tirore, Bengtson, Carbone. A Sound and Complete Projection for Global Types.** ITP 2023. JAR 2025. The classical projection of recursion can give a loop body with no action before the loop-back. The paper gives a computable plain-merge projection. It is sound and complete against coinductive projection, and it is proved in Coq.
- **[S] Li, Stutz, Wies, Zufferey, and Li and Wies.** Complete projection with automata (Li, Stutz, Wies and Zufferey, CAV 2023). Implementability is decidable (Stutz, ECOOP 2023). It is co-NP-complete for finite explicit protocols and PSPACE-complete for symbolic protocols (OOPSLA 2025). Implementability depends on the network kind (Li and Wies, PLDI 2026). The CAV 2023 claim of PSPACE-hardness was withdrawn.
- **[R] Li, Stutz. Global Protocols under Rendezvous Synchrony.** arXiv 2602.09197. Synchronous realizability is decidable in 2-EXPTIME, and in EXPTIME for unambiguous protocols. It is undecidable in general. This matters only if we add rendezvous channels with mixed choice.

### 4.2 Liveness

- **[R] Pischke, Yoshida. Top-down = Bottom-up.** arXiv 2607.21489, OOPSLA 2026. For one synchronous session, top-down typing and bottom-up typing accept the same live sessions under three conditions. The subsumption rule uses precise synchronous subtyping. The global type is balanced. Projection uses coinductive full merge. A principal global type can be inferred from each safe and live typing context.
- **[R] Udomsrirungruang, Yoshida. Top-Down or Bottom-Up? Complexity Analyses of Synchronous MPST.** arXiv 2411.07452, POPL 2025. A bottom-up check of liveness for a typing context is PSPACE-complete. It is undecidable for asynchronous semantics. Projection and subtyping are polynomial.
- **[S] Keskin, Yoshida, van Glabbeek. Formally Verified Liveness with MPST in Rocq.** arXiv 2605.23633, ITP 2026. This is the first mechanised liveness proof for synchronous MPST. It uses coinductive plain-merge projection and association up to subtyping. That is our design. Its definitions of fair path and live path are the ones to use for `phi_live`.
- **[S] Giunti, Yoshida. Iso-Recursive Multiparty Sessions and their Automated Verification.** ESOP 2025. A terminating compliance function decides type mismatch and deadlock for iso-recursive sessions. Our `Loop` and `Continue` are iso-recursive.
- **[S] Priorities across sessions.** van den Heuvel and Pérez, APCP (LMCS 2024). Kokke and Dardha, Priority GV (LMCS 2023). Jacobs et al., connectivity graphs (POPL 2022) and deadlock-free locks (POPL 2023). Priorities or a sharing topology give deadlock freedom in cyclic networks. Asynchrony makes the priority check simpler.

### 4.3 Subtyping

- **Baseline.** Precise asynchronous multiparty subtyping (Ghilezan, Pantović, Prokić, Scalas and Yoshida, TOCL 2023) is sound and complete. It is undecidable (Bravetti, Carbone and Zavattaro, 2017. Lange and Yoshida, 2017).
- **[S] Cutner, Yoshida, Vassor. Rumpsteak.** PPoPP 2022, pages 7 and 8. A bounded check with an unroll bound n is sound and incomplete. Its cost is O(n·min(b, b')^n).
- **[S] Bocchi, King, Murgia (TACAS 2024), and Bocchi, King, Murgia, Thompson (CONCUR 2025).** Sound decidable algorithms for asynchronous multiparty subtyping by abstract interpretation. A bound of 1 was sufficient for all benchmarks but one.
- **[S] Li, Stutz, Wies. Deciding Subtyping for Asynchronous Multiparty Sessions.** ESOP 2024. Asynchronous subtyping relative to a fixed global type is decidable.
- **[S] Padovani, Zavattaro. Fair Termination of Asynchronous Binary Sessions.** TOPLAS 2026. Their fair asynchronous subtyping is closed under duality and has no orphan messages. Several earlier relations are not closed under duality. The paper does not give a decision procedure.
- **[S] Bravetti, Padovani, Zavattaro.** CONCUR 2025, arXiv 2506.06078. A sound and complete characterisation of fair asynchronous subtyping.
- **[S] Udomsrirungruang, Yoshida.** PLACES 2024, arXiv 2404.05480. The Gay-Hole algorithm is singly exponential. A quadratic algorithm exists.
- **[S] Murgia.** FORTE 2026. Binary asynchronous subtyping and two-party asynchronous subtyping are the same relation.
- **[S] Ekici, Yoshida.** ITP 2024 and TOCL 2026. A Coq proof of precise asynchronous subtyping. It corrects the negation rules of the TOCL 2023 paper, from 18 rules to 8.

### 4.4 Ownership transfer, borrowing and sharing

- **[S] Hinrichsen, Bengtson, Krebbers. Actris 2.0.** LMCS 2022. Each message of a protocol carries a separation-logic resource. Subprotocols follow asynchronous subtyping.
- **[S] Jacobs, Hinrichsen, Krebbers. LinearActris.** POPL 2024. Linearity and acyclic ownership give deadlock freedom and leak freedom. Channels are made only through `fork`.
- **[S] Saffrich, Spaderna, Thiemann, Vasconcelos. Borrowing from Session Types.** OOPSLA 2025. Borrowing by ordered linear typing, with deadlock freedom by translation to Priority GV. The errata require a borrowed prefix on a different thread to block the suffix.
- **[S] Rocha, Caires. CLASS.** ESOP 2023. Shared linear state in cells like a mutex. Programs do not leak, do not deadlock and terminate. This is the model for SWMR sessions and for the shared permission pool.
- **[S] Somers, Krebbers. Verified Lock-Free Session Channels with Linking.** OOPSLA 2024. A lock-free channel proved against its session specification, with a `link` operation.
- **[S] Multris (OOPSLA 2024), Mixtris (OOPSLA 2026), Multiparty GV (ICFP 2022).** Mechanised multiparty protocols with resource transfer. Multris proves partial correctness only.

### 4.5 Crash, recovery and time

- **[S] Barwell, Hou, Yoshida, Zhou.** CONCUR 2022, ECOOP 2023 and LMCS 2025. Reliable roles, a marker for an unavailable queue and a crash branch that runs only after detection. The published CONCUR 2022 Definition 17 does not have the clause that makes a crash-detection step fire. The technical report arXiv 2207.02015 got a revision on 2026-08-24. Compare it with the CONCUR text before you cite it.
- **[S] Peters, Nestmann, Wagner. FTMPST.** LMCS 2023. Reliability for each interaction. The types do not separate kinds of failure.
- **[S] Mezzina, Tiezzi, Yoshida.** LMCS 2025. Commit, rollback and abort with a decidable compliance check.
- **[S] Le Brun, Dardha, Fowler.** MAGπ (ESOP 2023), MAGπ! (2024), MPST with a Bang! (ESOP 2025). Message loss, delay, crash and timeout.
- **[S] Timed sessions.** Fearless Asynchronous Communications with Timed MPST (ECOOP 2024). Timeout Asynchronous Session Types (LMCS 2025).
- **Crash-recover.** No paper from 2022 to 2026 gives a type system for it.

### 4.6 Encodings without a borrow checker

- **[S] Thiemann. Intrinsically Typed Sessions with Callbacks.** ICFP 2023. The library owns the handle. Linearity holds by construction.
- **[S] Alsubhi, Dardha, Gay. Coconut: Typestates for C++.** COORDINATION 2024. SCP 2025. Version 2 moved its check from templates to a GCC GIMPLE pass for compile time.
- **[S] Tang, Hillerström, Lindley, Morris. Soundly Handling Linearity.** POPL 2024. A continuation that holds a linear channel can be dropped or resumed twice. A coroutine frame that holds a handle is therefore a use of the handle.
- **[S] Lagaillardie, Neykova, Yoshida. Stay Safe under Panic.** ECOOP 2022. Affine MPST in Rust. The destructor sends a cancellation.
- **[S] Jia, Liu, He, Deng, Bao, Rompf. Typestate via Revocable Capabilities.** PLDI 2026. Capabilities that a function receives, revokes or returns. This is the closest formal model of our consume-and-return `Permission` flow.
- **[S] Sano, Kavanagh, Pientka. Enforcing Linearity without Linearity.** OOPSLA 2023. Linearity as side predicates over a structural context. Our concepts use the same pattern.
- **[S] Cledou, Edixhoven, Jongmans, Proença (ECOOP 2022), and Jongmans (TACAS 2025).** Protocol states as type parameters with Scala 3 match types. This is the Scala analogue of our C++ templates.

## 5. Published results that were corrected or refuted

1. **Honda, Yoshida and Carbone, POPL 2008 and JACM 2016.** Subject reduction is false. Lemma 5.11 (projection) is false. The counterexample is possible only when roles share a queue. (Tirore, Bengtson and Carbone, ECOOP 2025, with a Coq proof.)
2. **The classical projection of recursion.** It can give a loop body with no action. (Tirore, Bengtson and Carbone, ITP 2023.)
3. **The symmetric recursion congruence.** It breaks subject reduction under subtyping in several papers from 2019 to 2024. (Ekici, Kamegai and Yoshida, ITP 2025, Remark 17.)
4. **Barwell et al., CONCUR 2022, Definition 17.** The fair-path definition does not have the crash-detection clause.
5. **Ghilezan et al., TOCL 2023, Figure 6.** The negation rules were incomplete. The subtyping relation is correct. (Ekici and Yoshida, ITP 2024.)
6. **Implementability proofs for infinite words.** Several proofs had gaps. They were proved again in Rocq. (Li and Wies, ITP 2025.)
7. **The belief that top-down MPST types fewer programs than bottom-up MPST.** It is false for synchronous sessions under the three conditions of section 4.2. (Pischke and Yoshida, OOPSLA 2026.)
8. **Several fair or asynchronous subtyping relations.** They are not closed under duality. (Padovani and Zavattaro, TOPLAS 2026.)
9. **Links, effect handlers and linearity.** Handlers let a continuation that holds a linear channel break linearity. (Tang, Hillerström, Lindley and Morris, POPL 2024.)
10. **Synthetic MPST, POPL 2026.** What it proves is progress, not liveness. (Pischke and Yoshida, OOPSLA 2026, page 24.)
11. **Basu and Bultan synchronisability.** It is flawed. (Finkel and Lozes. Delpy et al.)

## 6. Corrections to our own design documents

- **`misc/session_types.md`, lines 364-366.** The text defines balanced+ as a bound on the en-route count. It says that bounded buffers give balanced+ by construction. The two statements are incorrect. Rule 2 of section 2 gives the correct definition.
- **`misc/session_types.md`, sections that cite "PMY25 Def 6.1" and "PMY25 Def 4.3".** These numbers come from an earlier version of the paper. In version 4, balanced+ is Definition 17. Cite version 4 numbers, or cite the definition by name.
- **`misc/session_types.md`, Appendix A.** It rests subject reduction on Honda, Yoshida and Carbone 2008. Rest it on association (Hou, Yoshida and Kuhn. Pischke, Masters and Yoshida).
- **`misc/24_04_2026_safety_integration.md`, line 1484.** It says the Coq development `smpst-sr-smer` is "directly portable to Lean 4". The project has no Lean tree. Use the development as a test oracle (section 8).
- **`misc/25_04_2026.md`, section 21.4.** It plans a refactor of the session stack around Synthetic MPST. That paper proves progress, not liveness. Compare the plan with rule 1 before you start it.

## 7. Choices that stay open

- **Release abandonment.** Rule 7 permits two designs: a destructor that aborts or sends a cancellation, or a callback API. Measure the cost of the destructor check on the dispatch benchmark before you decide.
- **The asynchronous subtyping relation.** Rule 6 selects the Rumpsteak check. If a decision procedure for the relation of Padovani and Zavattaro is found, compare the two.
- **Mixed choice.** Masters and Yoshida (ESOP 2027, arXiv 2608.10704) and Bocchi, Hu, Voinea and Thompson (OOPSLA 2026) type mixed choice. Our `Select` and `Offer` do not mix. Keep them separate unless mixed choice becomes necessary for a protocol.
- **Timed sessions.** The layer has no timed sessions. Section 4.5 gives the designs to use if they become necessary.

## 8. Differential tests against published mechanisations

A generator makes random global types and local types with a bounded size. An oracle labels each type. A script writes one C++ translation unit for each batch, with a `static_assert` for each case, and a CSV golden file. The test uses a fixed seed. If our relation and the oracle disagree, the cause is in our relation or in our reading of the paper. Record which one.

| Relation | Oracle | Repository | Licence |
|---|---|---|---|
| Projection | Tirore, Bengtson, Carbone, ITP 2023 | github.com/Tirore96/projection | none |
| Subject reduction, asynchronous | Tirore, Bengtson, Carbone, ECOOP 2025 | github.com/Tirore96/subject_reduction, branch ECOOP2025 | MIT |
| Liveness, synchronous | Keskin, Yoshida, van Glabbeek, ITP 2026 | github.com/omerskeskin/mpstlive | none |
| Subject reduction with full merge | Ekici, Kamegai, Yoshida, ITP 2025 | github.com/Apiros3/smpst-sr-smer | none |
| Asynchronous subtyping certificates | Ekici, Yoshida, ITP 2024 | github.com/ekiciburak/sessionTreeST | none |
| Crash-stop liveness | Scalas et al., mCRL2 tool | github.com/alcestes/mpstk-crash-stop | to check |
| Implementability | Li, Wies, ITP 2025 | Zenodo 10.5281/zenodo.15760396 | to check |
| Binary sessions with separation logic | Actris and LinearActris | gitlab.mpi-sws.org/iris/actris | BSD |

No Rocq repository in this list has an `Extraction` command. A decision procedure must be run inside the development, or extracted by us. Use a repository with no licence only on a local machine, and commit only the verdicts that it gives. `mpstk-crash-stop` is the only oracle that operates as a tool. It is the oracle for the crash layer.

Start with duality, projection and balancedness, because they are small.

## 9. Bibliography

| Year | Venue | Authors | Title | Link |
|---|---|---|---|---|
| 2022 | ECOOP | Lagaillardie, Neykova, Yoshida | Stay Safe under Panic: Affine Rust Programming with Multiparty Session Types | doi 10.4230/LIPIcs.ECOOP.2022.4 |
| 2022 | ECOOP | Cledou, Edixhoven, Jongmans, Proença | API Generation for Multiparty Session Types, Revisited and Revised Using Scala 3 | doi 10.4230/LIPIcs.ECOOP.2022.27 |
| 2022 | PPoPP | Cutner, Yoshida, Vassor | Deadlock-Free Asynchronous Message Reordering in Rust with Multiparty Session Types | arXiv 2112.12693 |
| 2022 | POPL | Jacobs, Balzer, Krebbers | Connectivity Graphs | doi 10.1145/3498662 |
| 2022 | ICFP | Jacobs, Balzer, Krebbers | Multiparty GV | doi 10.1145/3547638 |
| 2022 | LMCS | Hinrichsen, Bengtson, Krebbers | Actris 2.0 | arXiv 2010.15030 |
| 2022 | CONCUR | Barwell, Scalas, Yoshida, Zhou | Generalised Multiparty Session Types with Crash-Stop Failures | arXiv 2207.02015 |
| 2023 | ITP / JAR 2025 | Tirore, Bengtson, Carbone | A Sound and Complete Projection for Global Types | doi 10.4230/LIPIcs.ITP.2023.28 |
| 2023 | ECOOP | Barwell, Hou, Yoshida, Zhou | Designing Asynchronous Multiparty Protocols with Crash-Stop Failures | arXiv 2305.06238 |
| 2023 | ECOOP | Stutz | Asynchronous Multiparty Session Type Implementability is Decidable | doi 10.4230/LIPIcs.ECOOP.2023.32 |
| 2023 | CAV | Li, Stutz, Wies, Zufferey | Complete Multiparty Session Type Projection with Automata | doi 10.1007/978-3-031-37709-9_17 |
| 2023 | ESOP | Rocha, Caires | Safe Session-Based Concurrency with Shared Linear State | doi 10.1007/978-3-031-30044-8_16 |
| 2023 | ESOP | Le Brun, Dardha | MAGπ: Types for Failure-Prone Communication | doi 10.1007/978-3-031-30044-8_14 |
| 2023 | ICFP | Thiemann | Intrinsically Typed Sessions with Callbacks | arXiv 2303.01278 |
| 2023 | OOPSLA | Sano, Kavanagh, Pientka | Mechanizing Session-Types using a Structural View | arXiv 2309.12466 |
| 2023 | LMCS | Kokke, Dardha | Prioritise the Best Variation | doi 10.46298/lmcs-19(4:28)2023 |
| 2023 | LMCS | Peters, Nestmann, Wagner | Fault-Tolerant Multiparty Session Types | doi 10.46298/lmcs-19(4:14)2023 |
| 2023 | TOCL | Ghilezan, Pantović, Prokić, Scalas, Yoshida | Precise Subtyping for Asynchronous Multiparty Sessions | TOCL 24(2) |
| 2024 | POPL | Jacobs, Hinrichsen, Krebbers | Deadlock-Free Separation Logic (LinearActris) | doi 10.1145/3632889 |
| 2024 | POPL | Tang, Hillerström, Lindley, Morris | Soundly Handling Linearity | arXiv 2307.09383 |
| 2024 | ESOP | Li, Stutz, Wies | Deciding Subtyping for Asynchronous Multiparty Sessions | doi 10.1007/978-3-031-57262-3_8 |
| 2024 | TACAS | Bocchi, King, Murgia | Asynchronous Subtyping by Trace Relaxation | doi 10.1007/978-3-031-57246-3_12 |
| 2024 | ITP / TOCL 2026 | Ekici, Yoshida | Completeness of Asynchronous Session Tree Subtyping in Coq | doi 10.4230/LIPIcs.ITP.2024.13 |
| 2024 | OOPSLA | Hinrichsen, Jacobs, Krebbers | Multris | doi 10.1145/3689762 |
| 2024 | OOPSLA | Somers, Krebbers | Verified Lock-Free Session Channels with Linking | doi 10.1145/3689732 |
| 2024 | LMCS | van den Heuvel, Pérez | Asynchronous Session-Based Concurrency: Deadlock-Freedom in Cyclic Process Networks | arXiv 2111.13091 |
| 2024 | ECOOP | Hou, Lagaillardie, Yoshida | Fearless Asynchronous Communications with Timed Multiparty Session Protocols | doi 10.4230/LIPIcs.ECOOP.2024.19 |
| 2024 | PLACES | Udomsrirungruang, Yoshida | Three Subtyping Algorithms for Binary Session Types | arXiv 2404.05480 |
| 2025 | POPL | Udomsrirungruang, Yoshida | Top-Down or Bottom-Up? Complexity Analyses of Synchronous MPST | arXiv 2411.07452 |
| 2025 | ECOOP | Tirore, Bengtson, Carbone | Multiparty Asynchronous Session Types: A Mechanised Proof of Subject Reduction | doi 10.4230/LIPIcs.ECOOP.2025.31 |
| 2025 | ESOP | Giunti, Yoshida | Iso-Recursive Multiparty Sessions and their Automated Verification | arXiv 2501.17778 |
| 2025 | ESOP | Stutz, D'Osualdo | An Automata-theoretic Basis for Specification and Type Checking of Multiparty Protocols | arXiv 2501.16977 |
| 2025 | ITP | Ekici, Kamegai, Yoshida | Formalising Subject Reduction and Progress for Multiparty Session Processes | doi 10.4230/LIPIcs.ITP.2025.19 |
| 2025 | ITP | Li, Wies | Certified Implementability of Global Multiparty Protocols | doi 10.4230/LIPIcs.ITP.2025.15 |
| 2025 | CONCUR | Bocchi, King, Murgia, Thompson | Abstract Subtyping for Asynchronous Multiparty Sessions | doi 10.4230/LIPIcs.CONCUR.2025.10 |
| 2025 | CONCUR | Bravetti, Padovani, Zavattaro | A Sound and Complete Characterization of Fair Asynchronous Session Subtyping | arXiv 2506.06078 |
| 2025 | OOPSLA | Saffrich, Spaderna, Thiemann, Vasconcelos | Borrowing from Session Types | doi 10.1145/3763173 |
| 2025 | LMCS | Barwell, Hou, Yoshida, Zhou | Crash-Stop Failures in Asynchronous Multiparty Session Types | LMCS 2025 |
| 2025 | LMCS | Mezzina, Tiezzi, Yoshida | Checkpoint-Based Rollback Recovery in Session Programming | arXiv 2312.02851 |
| 2025 | SCP | Alsubhi, Dardha, Gay | Design and Evaluation of Coconut: Typestates for C++ | doi 10.1016/j.scico.2025.103398 |
| 2025 | preprint (v4 2026) | Pischke, Masters, Yoshida | Asynchronous Global Protocols, Precisely | arXiv 2505.17676 |
| 2026 | TCS | Hou, Yoshida, Kuhn | Less is More Revisited | arXiv 2402.16741 |
| 2026 | OOPSLA | Pischke, Yoshida | Top-down = Bottom-up | arXiv 2607.21489 |
| 2026 | ITP | Keskin, Yoshida, van Glabbeek | Formally Verified Liveness with Multiparty Session Types in Rocq | arXiv 2605.23633 |
| 2026 | ESOP | McDermott, Yoshida | Denotational Reasoning for Asynchronous Multiparty Session Types | arXiv 2604.10646 |
| 2026 | PLDI | Li, Wies | Implementability of Global Distributed Protocols Modulo Network Architectures | doi 10.1145/3808319 |
| 2026 | PLDI | Jia, Liu, He, Deng, Bao, Rompf | Typestate via Revocable Capabilities | doi 10.1145/3808323 |
| 2026 | TOPLAS | Padovani, Zavattaro | Fair Termination of Asynchronous Binary Sessions | arXiv 2503.07273 |
| 2026 | OOPSLA | Bocchi, Hu, Voinea, Thompson | Mixed Choice in Asynchronous Multiparty Session Types | doi 10.1145/3798256 |
| 2026 | OOPSLA | Fowler, Hu | Speak Now: Safe Actor Programming with Multiparty Session Types | doi 10.1145/3798267 |
| 2026 | POPL | Castro-Perez, Ferreira, Jongmans | A Synthetic Reconstruction of Multiparty Session Types | arXiv 2511.22692 |
| 2027 | ESOP | Masters, Yoshida | Mixed Choice Multiparty Session Types, Precisely | arXiv 2608.10704 |
| 2026 | preprint | Li, Stutz | Global Protocols under Rendezvous Synchrony | arXiv 2602.09197 |
