# Fixy — Typed Compute on Crucible's Distributed Substrate

*The language and compiler pipeline that produces programs runnable on the Crucible distributed bare-metal substrate. Covers compiler architecture, reflection integration, error propagation, zero-cost preservation, optimization strategy, topology and capability specification, the Canopy data plane, CPU and accelerator allocation, deployment, and the generality of the substrate beyond machine learning.*

Fixy is a typed compute language compiled ahead of time by `fixyc`, lowering through the IR001 → IR002 → IR003\* pipeline that Forge and Mimic implement, into vendor-binary artifacts the Crucible runtime executes across a fleet. The language's surface — grammar, lexical rules, formal semantics, kernel calculus — lives in `fixy_grammar.md`, `fixy_lexer.md`, `fixy_semantics.md`, and `fixy_kernel.md`. This document covers everything else: the architectural placement of the components, how the compiler wires its phases together, how reflection threads through compile-time and runtime, how errors propagate from Fixy source through to user-facing diagnostics, how the substrate preserves zero runtime cost, what optimizations the compiler enables, how Fixy programs declare topology and capability requirements, how the Canopy data plane satisfies them, how CPU and accelerator allocation are specified and resolved, how programs deploy through Crucible, and how the substrate handles non-ML workloads.

Written in the voice of CLAUDE.md, CRUCIBLE.md, FORGE.md, and MIMIC.md: direct, dense, without hedging. Read alongside those documents; cross-references are explicit.

---

## Contents

1. Thesis and scope
2. Audience entry points — researcher path and substrate-engineer path
3. Architectural placement — what lives in `fixy/` versus `crucible/`
4. The IR pipeline — Fixy source through binary
5. The transpiler — `fixyc` architecture
6. Reflection integration
7. Error propagation
8. Zero-runtime-cost preservation
9. Compiler optimizations
10. Topology specification
11. The Canopy data plane
12. CPU compute specification
13. Empirically-fastest mesh topology
14. Accelerator allocation
15. Deployment through Crucible
16. Beyond ML — HPC and mixed workloads
17. The runtime tracer and the frontends path
18. Forge inside Fixy
19. Mimic inside Fixy
20. Stdlib organization
21. Build plan
22. Open questions deferred
23. Glossary
24. Formal specifications — 20-dimension grade vector, §6.8 collision catalog, trust algebra, effect lattice, mode semiring, session safety levels, machine algebra, contract grammar, decidability + discharge, strong-type catalog, cross-vendor numerics CI matrix, negative-compile fixture corpus, build profiles, FX inheritance map

---

## 1. Thesis and scope

Six commitments:

1. **Fixy is the language; the C++26 type system is the kernel.** Programs are written in `.fixy` source files. The `fixyc` compiler parses, elaborates, and emits C++26 that consumes the existing safety / effects / permissions / sessions / algebra wrapper substrate. The C++ type system, contracts (P2900R14), reflection (P2996R13), and concept gates discharge every dimension obligation. There is no separate kernel binary, no SMT solver in the elaborator, no Lean-extracted certificate. Soundness is witnessed operationally by the cross-vendor numerics CI matrix and structurally by the substrate's neg-compile fixtures.

2. **Capabilities are denied by default and granted explicitly.** Every dimension every binding carries — usage, effect, security, lifetime, provenance, trust, observability, complexity, precision, space, overflow, FP order, mutation, reentrancy, size, version, staleness, representation, protocol — defaults to its most restrictive setting. The signature is the capability manifest. A function that does more than its signature declares is a compile error. Sketch builds infer aggressively with warnings; release builds reject anything below `Verified` trust.

3. **Forge and Mimic are part of Fixy.** The 12-phase IR optimization pipeline (Forge) and the per-vendor IR003\* code generators (Mimic) are the Fixy compiler's middle and back ends. They consume the Fixy IR (formerly Crucible IR001) the Fixy compiler produces, lower it to portable kernel IR (IR002) and per-vendor machine IR (IR003\*), and emit the vendor binary bytes the Crucible runtime executes. They live in `fixy/forge/` and `fixy/mimic/` respectively, and are invoked transparently by `fixyc`.

4. **Crucible is the distributed bare-metal substrate.** Crucible owns the bare-metal runtime, the fleet mesh, the persistent state. Specifically: the safety wrapper substrate (already shared as a build dependency), the Canopy mesh with SWIM gossip and Raft-scoped consensus, the CNTP transport over AF_XDP and io_uring, the three-tier Cipher persistence (hot RAM / warm NVMe / cold S3), the Keeper daemon lifecycle, the Augur monitoring loop, and the K8s / SLURM / bare-metal launchers. Crucible does not know what computation is being run; it knows how to run any compiled `.fixy-pkg` artifact across a fleet, route data to it, and recover from failures.

5. **Fixy is not ML-specific.** The language describes typed compute. The standard library happens to ship machine-learning primitives (`Std.Tensor`, `Std.Kernel`, `Std.Vigil`, `Std.Recipe`, `Std.Collective`) because that is the workload class motivating the project, but the language itself accommodates HPC (FFT, sparse linear algebra, MPI-pattern collectives, finite-element solvers, Monte Carlo simulations), data engineering (streaming aggregations, content-addressed deduplication pipelines), and mixed workloads (training with simulation in the loop, HPC postprocessing followed by inference). The Vigil concept generalizes from "the model" to "the typed long-lived program with state."

6. **Programs deploy as artifacts, not as source.** `fixyc compile app.fixy` produces `app.fixy-pkg` — a content-addressed bundle of compiled per-vendor binaries, IR snapshots for replay and federation, capability manifest, default fleet configuration, and Cipher seeds. `crucible fleet apply -f fleet.yaml --app app.fixy-pkg` deploys the artifact to a Canopy fleet via the chosen launcher. The runtime instantiates Vigils across nodes per the capability match, threads data via Cipher and CNTP, and runs until termination or the user requests teardown.

Scope boundaries:

| Category | In scope for this document | Covered elsewhere |
|---|---|---|
| Compiler architecture | Phase pipeline, codegen targets, build integration | Grammar in `fixy_grammar.md` |
| Type system | How dimensions thread through compilation | Semantics in `fixy_semantics.md`, kernel in `fixy_kernel.md` |
| Reflection | How `fixyc` and emitted code use P2996, how Fixy AST exposes itself | Per-language details in semantics doc |
| Error propagation | Three-layer model, structured diagnostics, LSP integration | Per-error-code documentation in `fixy_diagnostics.md` |
| Zero-cost guarantees | Substrate techniques, verification harnesses | Per-wrapper details in `safety/Safety.h` and CLAUDE.md §VIII |
| Topology and capability | Declaration syntax, matching algorithm, runtime resolution | Existing Crucible mesh in CRUCIBLE.md §7, §15 |
| Forge inside Fixy | Phase ordering, ownership of IR levels | Per-phase details in FORGE.md |
| Mimic inside Fixy | Per-vendor backend pattern | Per-vendor details in MIMIC.md |
| Beyond ML | Generality of the substrate, HPC examples | Workload-specific stdlibs in `Std.HPC.fixy`, `Std.Sim.fixy` |

What this document explicitly does not cover: the surface grammar, the lexical rules, the formal semantics of dimension composition, the per-rule SMT obligations (where applicable), the test-mode / sketch-mode / release-mode behavioral differences beyond their compilation effect, the stdlib API per-function reference, and the wire-byte specification of CNTP frames produced by Fixy contracts.

---

## 2. Audience entry points

Fixy serves two audiences whose technical preferences barely overlap. The same artifacts ship to both; the surface they see is different.

### 2.1 ML researcher entry point

Researchers writing models against PyTorch want to keep writing PyTorch and have the runtime do the rest. Fixy's Python frontend (`crucible` package) is a drop-in capability extension to existing PyTorch code:

```python
import crucible as cr
import torch

class MyModel(torch.nn.Module):
    def __init__(self, ...):
        ...
    def forward(self, x):
        ...

# Three lines of new code surrounding existing PyTorch.
with cr.fleet("local"):           # auto-detects local hardware
    model = MyModel().to("cr")     # cr device captures dispatch
    train(model, dataloader, epochs=10)
```

Underneath: Vessel-PyTorch intercepts every `aten::` dispatch into Fixy IR via TraceRing. The background thread builds RegionNodes. Forge optimizes. Mimic emits per-vendor binaries. Cipher persists. The user wrote PyTorch; the runtime delivered cross-vendor bit-exact training with content-addressed checkpoints. No CUTLASS, no NCCL configuration, no FSDP2 incantations.

For research patterns that need new kernels — a custom attention variant, a novel optimizer, a non-standard normalization — three lines of Python decoration handle it:

```python
@cr.kernel.attention(
    score_mod=lambda s, b, h, q, kv: s - 0.1 * cr.abs(q - kv),  # ALiBi bias
    mask_mod=lambda b, h, q, kv: cr.abs(q - kv) < 512,          # sliding window
    recipe=cr.Recipe.BITEXACT_TC,
)
def my_attention(q, k, v):
    return cr.sdpa(q, k, v)
```

The lambdas reflect into IR000 ComputeBody fragments, splice into the FlashAttention-3 kernel template at IR002 → IR003\* lowering, MAP-Elites runs over the tile space per vendor. Result: FlashAttention-class throughput for a custom attention the user invented, with cross-vendor bit-equivalence under the declared recipe.

The Cursor experience is structural: every `cr.*` API ships type stubs (`.pyi` files), one-line docstrings with examples, deterministic naming patterns. Cursor trains on the stubs and autocompletes correctly. The user types `@cr.kernel.` and sees the menu (`attention`, `norm`, `reduce`, `scan`, `embedding`, `pool`, `conv`, `recurrence`, ...). They type `score_mod=lambda` and Cursor suggests common patterns from the stdlib.

Errors come back in PyTorch-style traceback format, not C++ template format. The diagnostic translator catches C++ template instantiation failures and rewrites them at the Fixy source level (Goal/Have/Gap/Suggestion structure), then the Python translator produces a familiar-looking Python traceback:

```
CrucibleKernelError: attention score_mod incompatible with sliding mask
  File "research.py", line 47, in my_attention
    score_mod=lambda s, b, h, q, kv: s - 0.1 * cr.abs(q - kv),
                                     ^^^^^^^^^^^^^^^^^^^^^^^^

  Issue: score_mod returns shape (B, H, Q, KV) but mask_mod expects
         broadcast-compatible shape; current declarations imply
         different KV strides

  Fix:   change mask_mod to:
         lambda b, h, q, kv: cr.broadcast_to(cr.abs(q - kv) < 512, score_mod_shape)

  See: https://crucible.docs/diagnostics/E044-python
```

For paper reproducibility: `cr.snapshot(model_state, tag="paper-v1-step-12500")` returns a content-addressed hash. The paper cites the hash. Reviewers run `pip install crucible && cr.replay("paper-v1-step-12500")` and get bit-exact reproduction on whatever hardware they have. The reproducibility crisis disappears for Fixy-trained models — not by policy, by construction.

Recommended researcher onboarding path: one-page tutorial → first existing-model wrapping in 30 minutes → custom kernel decorator in two hours → snapshot + replay in 30 more minutes. Total: half a day to working capability over an existing PyTorch codebase. No Fixy syntax learned along the way; the researcher discovers Fixy only if they want to.

### 2.2 Substrate engineer entry point

Engineers evaluating Fixy as production infrastructure want to understand the trust base, the type system, the determinism story, the bare-metal transport, and the formal alignment. They will read the design docs and form judgments quickly. Fixy is built so that judgment goes well.

The TCB inventory:

```
kernel UABI    Linux ioctls into /dev/nvidia*, /dev/kfd, /dev/accel*, /dev/neuron*
+ libbpf       XDP for CNTP packet classification
+ HACL*        Ed25519 signatures, ChaCha20-Poly1305 for inter-Keeper auth
+ GCC 16       the compiler
+ Crucible     the substrate
```

That is the entire trusted base. No libcuda. No NCCL. No libtpu. No libnrt. No vendor SDK linked at runtime. Every transport byte to silicon goes through first-party C++26 code wrapping the kernel driver's UABI directly. The tinygrad blueprint applied at scale across five vendors.

The 20-dimension typed substrate. Every binding carries a grade vector: type, refinement, usage, effect row, security, protocol, lifetime, provenance, trust, representation, observability (constant-time), complexity, precision, space, overflow, mutation, reentrancy, size, version, staleness. Twenty dimensions checked simultaneously by one C++26 concept gate (`ValidComposition<F>`) that aggregates the §6.8 collision catalog (12 named soundness rules — classified × Fail, borrow × Async, CT × Async, ghost × runtime, monotonic × concurrent, decimal × overflow(wrap), etc.). Violations produce structured diagnostics naming the specific rule. Wall-clock dimensions (latency, energy, power) are deliberately NOT in the catalog — the compiler cannot prove physical bounds, so claiming the type system enforces them would be a lie.

The replay-determinism invariant. Training state at step T is a pure function of `(weights_T, optimizer_T, cursor_T, seed)`. Philox4x32 counter-based RNG with no internal state. Static content-addressed memory plan computed offline. Canonical reduction topology pinned by UUID-sort. Bit-exact kernels under BITEXACT_TC and BITEXACT_STRICT recipes, enforced by the cross-vendor numerics CI matrix on every PR. The conventional ML stack is silently nondeterministic at every layer; eliminating that nondeterminism is the design commitment of CRUCIBLE.md §10.

Bare-metal CNTP. AF_XDP transport at ~220 ns intra-host one-way (measured, not promised — see CRUCIBLE.md §6 for the bench harness). RemotePermission tokens Ed25519-signed via HACL\*. Content-addressed frame deduplication in the BPF classifier. Effect-row routing read at byte 16 of the frame header before deserialization. WireGuard's discipline composed with typed-language semantics.

ExecutionPlan with PatchPoints and ChainEdges. Pre-composed pushbuffer bytes per training step. Sub-μs CPU dispatch in the steady-state path (120-200 ns CPU critical path for a 5-patch plan, measured per CRUCIBLE.md §11.9). Eight typed PatchPoint kinds for runtime mutation. ChainEdge semaphore composition for inter-plan sequencing without host round-trips. CUDA Graphs aspired to this and never quite achieved it; Megakernel (MLSys 2026) is the closest published prior art and does not cover the cross-vendor + distribution gaps.

Z3 partition solver + Augur drift detection. Forge Phase K formulates 5D parallelism (TP, DP, PP, EP, CP) as a constraint-satisfaction problem over the Meridian-measured N×N latency/bandwidth matrix. Z3 returns the lowest-cost partition for the cost model. Augur monitors actual runtime per collective and re-solves on >10% drift. Solver-driven topology that the conventional stack (NCCL with hardcoded ring algorithms) cannot do.

The FX polar star. A 16,622-line dependently-typed language design (`/root/iprit/FX/fx_design.md`) with a 22-dimension grade vector and a finite collision catalog that the Crucible substrate is structurally aligned to. Crucible discharges operationally — concept gates + neg-compile fixtures + cross-vendor numerics CI matrix — rather than via the proof-assistant kernel FX targets. The alignment means any specific Crucible construct has a known formal shape if a future situation demands a more rigorous discharge mechanism.

Recommended substrate-engineer reading order:

```
CRUCIBLE.md         the substrate this DSL targets
FORGE.md            the IR optimization pipeline
MIMIC.md            per-vendor backend framework
fixy.md             the DSL + integration story (this doc)
fixy.md §24         formal specifications appendix
```

Two hours of focused reading covers the inventory. Either the reaction lands or it doesn't; either way, the engineer has formed an informed judgment from explicit design choices, not marketing claims.

### 2.3 Fixy is a DSL, not a standalone language

A note on framing that matters for both audiences. Fixy is not a new programming language with its own compiler infrastructure. Fixy is a **typed compute DSL embedded in C++26**, optionally surfaced as `.fixy` source files that a thin transpiler (`fixyc`) lowers to C++26.

The Fixy "compiler" is structurally lightweight:

| Phase | What it does | Approximate LoC |
|---|---|---|
| Lex + parse | Recognize Fixy tokens, build AST | ~1200 |
| Elaborate | Bidirectional type check, dimension resolution | ~3000 |
| Lower to IR000 | Translate AST to dataflow IR | ~800 |
| Lower to IR001 | Hand off compute portions to Forge | ~600 |
| Codegen to C++26 | Emit substrate template instantiations | ~1500 |
| Diagnostic translator | Rewrite C++ template errors as Fixy-source diagnostics | ~1500 |

Total: ~8.6K LoC for the entire transpiler pipeline. The C++26 substrate it emits to (the safety / effects / permissions / sessions / algebra / concurrent wrappers + Forge + Mimic + Crucible runtime) is much larger but already exists. Forge's 12 phases and Mimic's per-vendor backends are not part of `fixyc` — they are Crucible substrate that Fixy reuses. There is no separate Fixy optimizer, no separate Fixy codegen, no separate Fixy linker.

Three surfaces all lower to the same substrate:

- **Python decorators** (researcher path) — `@cr.kernel.attention(...)`, `@cr.fn(...)`. Python frontend does AST reflection on the Python lambda, builds Fixy IR000 directly, never goes through `fixyc`. Fastest iteration loop.
- **C++26 with Fn templates** (engineer path) — `mint_fn<...>(ctx, body)` calls directly. No frontend at all; the substrate IS the surface. Maximum control.
- **`.fixy` source files** (purist path) — ergonomic surface for users who want the language-feel without writing C++26 verbosity. `fixyc` is the transpiler.

The three surfaces interoperate. A `.fixy` module exports symbols visible to C++26 callers; Python code calls into both via the existing Vessel adapter. The choice of surface is a UX decision, not an architectural one.

This framing matters because it bounds the maintenance cost. Fixy ships with C++26 macros + reflection + a few thousand lines of transpiler — not a 200K-LoC compiler with its own optimization passes. The optimization passes already exist (Forge), the codegen already exists (Mimic), the runtime already exists (Crucible). Fixy adds the typed surface; the substrate does the work.

---

## 3. Architectural placement

The codebase splits into two top-level trees plus the existing supporting projects.

```
fixy/                                       — the language and its toolchain
├── compiler/                                — fixyc binary
│   ├── lex/                                 — lexer (token recognition)
│   ├── parse/                               — parser (AST construction)
│   ├── elab/                                — elaborator (typed AST)
│   ├── ir000/                               — high-level dataflow IR
│   ├── ir001/                               — typed kernel-graph IR (was Crucible's)
│   ├── codegen/                             — emits C++26 + auxiliary artifacts
│   ├── diag/                                — diagnostic translator
│   └── driver/                              — CLI entry point
├── ir/                                      — IR data structures (compiler guts)
│   ├── Expr.h, ExprPool.h                   — interned symbolic expressions
│   ├── Graph.h                              — mutable kernel graph
│   ├── MerkleDag.h                          — content-addressed regions
│   ├── SymbolTable.h                        — per-symbol metadata
│   ├── Ops.h, CKernel.h                     — symbolic and compute op taxonomies
│   ├── NumericalRecipe.h                    — recipe pool
│   ├── RecipePool.h, RecipeRegistry.h       — interning and registry
│   ├── TensorMeta.h                         — tensor metadata
│   └── TypesML.h                            — ScalarType, DeviceType, Layout
├── runtime/                                 — runtime tracer and replay
│   ├── TraceRing.h, MetaLog.h               — SPSC capture buffers
│   ├── BackgroundThread.h                   — drainer
│   ├── IterationDetector.h                  — boundary detection
│   ├── TraceGraph.h, TraceLoader.h          — bidirectional graph
│   ├── Lower.h                              — TraceEntry → Graph
│   ├── CrucibleContext.h                    — recording context
│   ├── ReplayEngine.h                       — region execution
│   ├── Transaction.h, RegionCache.h         — transaction and cache
│   ├── Serialize.h                          — graph serialization
│   ├── SchemaTable.h                        — frontend schema dispatch
│   ├── Arena.h, PoolAllocator.h             — memory infrastructure
│   ├── Vigil.h                              — typed-program orchestrator
│   ├── DimHash.h                            — dimension hashing
│   ├── Philox.h, PhiloxSimd.h               — RNG
│   ├── CallSiteTable.h                      — source location dedup
│   ├── Saturate.h, StorageNbytes.h          — utility primitives
│   └── ...                                  — additional runtime helpers
├── stdlib/                                  — user-facing .fixy modules
│   ├── prelude.fixy                         — auto-imported types and functions
│   ├── Std/Tensor.fixy                      — tensor type and operations
│   ├── Std/Kernel.fixy                      — kernel decoration
│   ├── Std/Vigil.fixy                       — vigil syntax
│   ├── Std/Recipe.fixy                      — numerical recipe selection
│   ├── Std/Collective.fixy                  — communication patterns
│   ├── Std/Data.fixy                        — streaming, prefetch, channels
│   ├── Std/HPC.fixy                         — FFT, sparse, MPI-style ops
│   ├── Std/Sim.fixy                         — Monte Carlo, FEM, N-body
│   ├── Std/Math.fixy                        — scalar math, frac, decimal
│   ├── Std/Logic.fixy                       — Decidable, Eq, Ord
│   └── Std/Cluster.fixy                     — capability declarations
├── frontends/                               — adapters from other languages
│   ├── pytorch/                             — Vessel-PyTorch adapter
│   ├── jax/                                 — Vessel-JAX adapter
│   └── native/                              — direct .fixy compilation
├── forge/                                   — IR optimization pipeline
│   ├── PhaseA/                              — INGEST
│   ├── PhaseB/                              — ANALYZE
│   ├── PhaseC/                              — REWRITE
│   ├── PhaseD/                              — FUSE
│   ├── PhaseE/                              — LOWER_TO_KERNELS
│   ├── PhaseF/                              — TILE
│   ├── PhaseG/                              — MEMPLAN
│   ├── PhaseH/                              — COMPILE (delegates to mimic/)
│   ├── PhaseI/                              — SCHEDULE
│   ├── PhaseJ/                              — EMIT
│   ├── PhaseK/                              — DISTRIBUTE
│   └── PhaseL/                              — VALIDATE
├── mimic/                                   — per-vendor backends
│   ├── core/                                — shared simulator and MAP-Elites
│   ├── nv/                                  — NVIDIA Hopper, Blackwell
│   ├── am/                                  — AMD CDNA3, RDNA3
│   ├── apl/                                 — Apple AGX, AMX
│   ├── tpu/                                 — Google TPU v5+
│   ├── trn/                                 — AWS Trainium
│   └── cpu/                                 — CPU reference oracle
└── tools/                                   — auxiliary tooling
    ├── fixy-lsp/                            — language server
    ├── fixy-fmt/                            — formatter
    └── fixy-pkg/                            — package manager

crucible/                                    — distributed bare-metal substrate
├── safety/                                  — Linear, Refined, Tagged, ...
├── effects/                                 — effect rows, capabilities
├── permissions/                             — CSL Permission, fork
├── sessions/                                — MPST session types
├── handles/                                 — OS handles, ScopedFd
├── algebra/                                 — Graded substrate, lattices
├── concurrent/                              — Topology, AdaptiveScheduler, queues
├── bridges/                                 — adapter primitives
├── cipher/                                  — content-addressed persistence
├── canopy/                                  — gossip, Raft, membership
├── cntp/                                    — bare-metal transport, AF_XDP
├── keeper/                                  — daemon lifecycle, FLR
├── augur/                                   — drift detection, recommendations
├── operator/                                — k8s controller, CRD
├── Cipher.h                                 — top-level Cipher interface
├── Reflect.h                                — reflection helpers
├── Platform.h                               — platform macros
├── SwissTable.h                             — generic open-addressing table
└── rt/                                      — runtime layer primitives
```

Three observations follow from the layout.

**Crucible is small.** The substrate measured by current header surface is roughly thirty percent of what the codebase carries today; the remaining seventy percent moves into `fixy/` as compiler and runtime guts. Crucible's responsibility shrinks to "run any `.fixy-pkg` artifact across a fleet of Keepers, route data through Cipher and CNTP, recover from failures, monitor drift via Augur." Nothing in Crucible knows what a kernel is, what a recipe is, what a tensor is — those are Fixy concepts.

**Fixy guts are private.** The headers under `fixy/ir/`, `fixy/runtime/`, `fixy/forge/`, `fixy/mimic/`, and `fixy/frontends/` are compiler and runtime implementation. Users do not include them, do not depend on their stability, and do not write code against them. The user-facing surface is the `.fixy` language and the `Std.*` modules. Guts can be refactored, rewritten in Fixy itself, or replaced wholesale without breaking user code.

**Stdlib carries the stability contract.** The `.fixy` modules in `fixy/stdlib/` are the public API. They are versioned (per dimension 21 in the type system), diff-checked at publish time, and consumed by user `open Std.Tensor` and `open Std.Kernel` declarations. Changes to a stdlib module flow through the contract diff per Crucible §14.10 (when that lands) and produce semver bumps.

The split makes the Turing-completeness question resolve cleanly. The user surface is a typed language with arbitrary control flow, recursion, closures, sessions, effects — Turing-complete by construction. The compiler lowers that to IR001 for the kernel-shaped portions and emits direct C++26 for the scalar and control-flow portions. The Crucible runtime executes whatever the compiler produces; it does not constrain expressiveness.

---

## 4. The IR pipeline

A `.fixy` source file traverses six representations on its way to executable bytes.

**Source.** `.fixy` text. UTF-8, ASCII identifiers, semicolon-terminated statements, explicit type annotations on every binding that requires one. The source is the only artifact under the user's direct control.

**AST.** Untyped abstract syntax tree, built by the parser. Tree nodes correspond directly to surface constructs: `FnDecl`, `LetStmt`, `MatchExpr`, `WithClause`, `RequiresClause`, etc. Source positions are preserved on every node for diagnostic generation.

**Typed AST.** The elaborator walks the AST top-down, performing bidirectional type checking. Every node acquires a fully-resolved 22-dimension grade vector (or however many dimensions the Fixy variant ships; the substrate is parameterized over the dimension catalog). The §6.8 collision rules apply at every site where two dimensions meet; failures emit structured diagnostics. References to stdlib symbols resolve to specific `Std.*` declarations; references to user-defined types resolve to module-local declarations or to imported declarations from `open` clauses.

**IR000.** A high-level dataflow representation. Functions, type declarations, contracts, vigils, pipelines, and stages become IR000 nodes. Control flow is preserved; no lowering to kernel form has occurred yet. Effects are explicit on every operation; lifetimes are annotated; refinements travel as predicates attached to bindings. IR000 is the representation the Fixy compiler reasons about for non-kernel transformations: dead-code elimination, common-subexpression elimination on control-flow operations, coercion insertion at trust boundaries, instrumentation insertion (e.g., observability hooks), dispatch-table construction for handlers.

**IR001.** The typed kernel-graph IR. This is the representation that Forge consumes; everything that compiles to a kernel — every tensor operation, every collective, every reduction, every scan, every elementwise chain — becomes an IR001 node. Symbolic shapes live in the `Expr` symbolic engine (`fixy/ir/Expr.h`). The taxonomy of compute operations comes from `fixy/ir/CKernel.h` (146 ML ops in the current taxonomy plus user-defined ops added via Fixy `fn` decorations). Pinned numerical recipes attach via `NumericalRecipe`. Region nodes — content-addressed compilation units — live in `MerkleDag.h`. The IR001 boundary is also where the Vessel adapters (PyTorch, JAX) emit: their output is IR001 produced via the runtime tracer (`TraceRing` and friends), which is then handed to Forge identically to AOT-compiled IR001 from `.fixy` source.

**IR002.** Forge's portable kernel IR. Each KernelNode carries a kind (one of 22 KernelKinds), an attribute struct per kind, a pinned NumericalRecipe, a TileSpec (concrete or symbolic with bounded ranges), a layout commitment, and content hashes for cache lookup. IR002 is vendor-neutral; the same IR002 produces ULP-bounded equivalent results on any backend that supports the recipe. Per-kind extension-point fields (`ComputeBody*` for `score_mod`, `mask_mod`, custom `update_body` on optimizers, etc.) inline at the IR002 → IR003\* lowering and fuse with the structural kernel via peephole.

**IR003\*.** Per-vendor machine IR; one per Mimic backend. IR003NV for NVIDIA, IR003AM for AMD, IR003APL for Apple AGX, IR003TPU for Google, IR003TRN for AWS Trainium, IR003CPU for the reference. Contains vendor-specific instruction encoding, register allocation, scheduling, address-space resolution. Mimic owns the entire layer; Forge never touches it.

**Vendor binary.** The terminal artifact: cubin for NVIDIA, HSACO for AMD, AGX shader bytes for Apple, TPU executable for Google, NEFF for Trainium, ELF for CPU. Mimic emits these via per-vendor encoders that wrap the kernel driver's UABI directly (no vendor SDK linkage). The bytes get packaged into the `.fixy-pkg` artifact alongside the IR snapshots and capability manifest.

The pipeline runs entirely inside `fixyc` for AOT compilation. For Vessel-frontend programs, the pipeline runs entirely inside the Crucible runtime: the frontend records ops to TraceRing, the background thread builds IR001, Forge optimizes on the background thread, Mimic compiles per-vendor on a thread pool, the result lands in KernelCache, the foreground thread launches the compiled kernels via the per-vendor runtime library. Both paths produce identical IR001 from identical inputs (same source code, same dispatch ops); the difference is only the capture mechanism — AOT for Fixy, runtime trace for PyTorch.

The IR pipeline is content-addressed at every level. IR001 nodes hash their (kind, attrs, recipe, tile, input slot types) into a `KernelContentHash`. IR002 region nodes hash their member kernel hashes plus their merge structure into a `RegionContentHash`. IR003\* binaries hash the vendor bytes plus the chip caps signature into an `ImageContentHash`. The three levels populate the L1 (vendor-neutral, federation-shareable), L2 (vendor-family-shareable), and L3 (per-chip) Cipher caches respectively. A Fixy program that compiles cleanly once anywhere in the federation populates the L1 cache, and every other site running the same program hits cache instead of recompiling from source.

---

## 5. The transpiler — `fixyc` architecture

`fixyc` is the only user-facing executable in the Fixy toolchain. It accepts `.fixy` source files plus a workspace configuration (`fixy.toml`) and produces `.fixy-pkg` artifacts ready for Crucible deployment. Internally it runs eight phases.

**Phase 1: Lex.** The lexer reads `.fixy` source as UTF-8 bytes, recognizes ASCII tokens, and emits a stream of `(kind, span, value)` triples. Whitespace handling is minimal: newlines are significant only as statement separators when followed by a closing `;` token in the previous line; otherwise they are ignored. Comments are stripped before tokens reach the parser. Source positions are tracked as `(file_id, byte_offset, line, column)` quadruples, with file IDs registered in a workspace-global `SourceMap` for diagnostic generation. The lexer is hand-written — approximately 600 lines of C++26 — because the token surface is small and stable, and a hand-written lexer produces better diagnostic positions than a generated one.

**Phase 2: Parse.** The parser is a recursive-descent implementation following the grammar in `fixy_grammar.md`. The output is an AST allocated in a per-compilation arena (one `Arena` instance per `.fixy` file). AST nodes are POD-like structures with strong-typed parent and sibling pointers. The parser is approximately 2,000 lines; recursive descent suits the grammar's left-to-right precedence and avoids the build-time cost of a parser generator. Errors at this phase are syntactic (unexpected token, missing semicolon, unbalanced brace) and emit structured diagnostics with the source position pointing at the offending token.

**Phase 3: Elaborate.** The elaborator performs bidirectional type checking, dimension resolution, and overload resolution. For every binding, it computes a 22-dimension grade vector. For every expression, it computes a type and an effect row. For every call site, it resolves the callee (via name resolution against open modules and lexical scope), substitutes type parameters, and verifies the caller's grade vector composes with the callee's per the Tier S/L/T/F/V composition rules. The §6.8 collision rules apply at every site where two dimensions meet; rule violations produce named errors (`I002`, `L002`, `E044`, etc.) with the FX rule citation in the diagnostic. The output is a typed AST in which every node has a fully-resolved type, a fully-resolved grade vector, and a fully-resolved effect row. The elaborator is approximately 4,000 lines and is the largest single component of the compiler.

**Phase 4: Lower to IR000.** The typed AST lowers to IR000, a high-level dataflow representation. Function declarations become IR000 function nodes with explicit parameter and return types annotated by their grade vectors. Control-flow statements (`if`, `match`, `for`, `while`) become IR000 control-flow nodes. Effect operations (`fail`, `await`, `yield`, custom handler operations) become IR000 effect nodes that thread the effect row through their continuation. Vigil declarations become IR000 vigil nodes that aggregate stages, pipelines, and capability requirements into a single deployable unit. Refinements attached to bindings become IR000 predicate nodes that attach to value flows. Lifetimes attach to references; provenance tags attach to data crossings. The lowering is a straightforward AST walk, approximately 1,500 lines.

**Phase 5: Lower to IR001.** Operations on tensors, recipes, kernels, collectives, and other compute primitives lower to IR001 — the typed kernel-graph IR Forge consumes. This is where the Fixy compiler hands off to Forge. Every IR000 node that maps to a kernel becomes an IR001 KernelNode (or a chain of KernelNodes for compound operations); every IR000 node that does not map to a kernel (control flow, effect operations, scalar arithmetic on non-tensor types) emits as direct C++26 in the codegen phase. The split between "compiles to kernel" and "compiles to direct C++26" is determined by the operation's stdlib origin: anything reaching through `Std.Tensor`, `Std.Kernel`, `Std.Recipe`, `Std.Collective`, `Std.HPC` lowers to IR001; anything reaching through `Std.Math`, `Std.Logic`, or arbitrary user-defined types compiles to direct C++26. The lowering is approximately 1,200 lines.

**Phase 6: Forge phases A–L on IR001.** Forge runs its 12-phase pipeline on the IR001 produced by Phase 5. Each phase has a wall-clock budget and a verification gate at its exit (FORGE.md §5). The pipeline produces an `ExecutionPlan` per region: pre-composed pushbuffer bytes, compiled vendor binaries, abstract launch records, content-addressed memory plan, guards, chain edges, hash identity. Phase H delegates to Mimic per-vendor backends for actual binary emission. The execution plan ends up serialized into the `.fixy-pkg` artifact alongside the L1/L2/L3 Cipher cache entries Forge populated during compilation.

**Phase 7: Codegen.** The codegen phase walks the IR000 + IR001 + ExecutionPlan tree and emits C++26 source files. For each Fixy module, the output comprises:

| Generated file | Purpose |
|---|---|
| `module.cpp` | Implementation: function definitions, vigil orchestrators, dispatch tables |
| `module.h` | Public API: function declarations, type declarations, capability annotations |
| `module.neg.cpp` | Negative-compile fixtures (HS14: ≥2 per `mint_fn` invocation) |
| `module.test.cpp` | Cross-vendor numerics CI tests, one per (kernel, recipe, vendor) tuple |
| `module.cipher.cpp` | Cipher serialization handlers for declared `contract` types |
| `module.cntp.cpp` | CNTP frame layouts for declared `contract … format(wire)` types |
| `module.augur.cpp` | Augur drift telemetry tags (`std::meta::identifier_of` extracted) |
| `module.manifest.json` | Capability manifest entry: which capabilities the module provides and requires |

Codegen uses a template-based emitter — approximately 2,500 lines — that walks the typed IR and substitutes into pre-defined C++26 output patterns. Each pattern is keyed by a (Fixy construct, target dimension) pair. For example, a Fixy `with IO, Crypto` clause emits as a `safety::Computation<effects::Row<effects::IO, effects::Crypto>, ReturnType>` template instantiation. A `requires Capability::S3Read` emits as a `requires CtxFitsCapability<Capability::S3Read, Ctx>` constraint on the function template. A `pre length(xs) > 0` emits as a `pre (xs.length() > 0)` contract clause per P2900R14. The patterns are exhaustive: every Fixy construct has a defined emission target.

**Phase 8: Drive C++ build.** `fixyc` invokes GCC 16 on the generated C++26 with the flags from CRUCIBLE.md §V (Common, Release, Bench, TSan, Verify presets, depending on user request). The C++ compilation discharges the type-system obligations: contracts evaluate per the per-TU semantic, concept gates fire on the dimensional composition, neg-compile fixtures verify the Fn substrate rejects misuse, the cross-vendor CI tests run against the CPU oracle. The output object files plus the per-vendor binaries Mimic emitted plus the IR snapshots Forge produced get packaged into the `.fixy-pkg` artifact.

**Workspace structure.** A Fixy project has a `fixy.toml` at its root declaring its name, version, dependencies, and target capabilities. Source lives in `src/`; tests in `tests/`; benchmarks in `benches/`; examples in `examples/`. The `fixyc build` invocation reads `fixy.toml`, resolves dependencies (Fixy stdlib plus any user-declared imports), and invokes the eight-phase pipeline. Build artifacts land in `target/` (analogous to Cargo). Each module compiles independently; cross-module dependencies are resolved through the `module.h` headers Phase 7 emits.

**Incremental compilation.** Each phase is incremental at the module level. A change to one `.fixy` file invalidates that module's outputs and any module that transitively depends on it. The dependency graph is constructed from `open` and `include` declarations. The incremental build cache lives in `target/cache/` and is keyed by `(module_path, source_content_hash, dependencies_content_hash, fixyc_version)`. A clean build of a 100-module project takes approximately fifteen minutes on a 16-core machine; an incremental build after a single-module change takes approximately three seconds.

**LSP integration.** `fixy-lsp` is a separate binary that wraps `fixyc` in `--check` mode, running the lex / parse / elaborate phases and emitting LSP-protocol diagnostics back to the editor. The LSP server caches the typed AST per-file and invalidates on save. Hover provides type information by walking the typed AST at the cursor position. Goto-definition resolves through the elaborator's name resolution table. Find-references walks the use-def chains. Auto-format invokes `fixy-fmt` (a separate ~400-line binary) which prints the AST in canonical form.

---

## 6. Reflection integration

Reflection threads through Fixy at three levels: the compiler consumes C++26 reflection (P2996R13) on the substrate types it emits, the Fixy AST is itself reflection-walkable at compile time via a `meta::*` family in the elaborator, and emitted code uses reflection for serialization, content hashing, neg-compile fixture generation, cross-vendor CI test generation, and Augur telemetry tagging.

**Compiler-side reflection on emitted C++26.** The codegen phase produces C++26 templates that instantiate the Fn substrate. After emission, the C++ compiler instantiates those templates and the substrate's internal machinery walks the resulting types via P2996 reflection to verify well-formedness and compute content hashes. A `mint_fn<Type, Refinement, Usage, EffectRow, Security, ...>` invocation produces a `Fn<...>` type whose dimension parameters become accessible via `std::meta::reflect_type(^^Fn)` and the standard reflection API. The substrate uses this access to:

- Compute the function's `KernelContentHash` deterministically from its dimension parameters (so two functions with identical dimensions produce identical hashes regardless of where they were declared).
- Verify the §6.8 collision catalog at template instantiation time (the `ValidComposition<Fn<...>>` concept walks the dimension parameters via reflection and checks the per-pair collision rules).
- Generate the function's serialization signature for Cipher and CNTP — the wire format for a function call across CNTP includes the function's reflected dimension list as a header prefix, so the receiver routes by dimension before deserializing the payload.
- Compute the function's effect-row mask for XDP routing — the BPF classifier reads the mask at byte 16 of the CNTP frame and routes to a Worker holding the matching capability set.

This reflection happens entirely in the C++26 layer; `fixyc` itself does not run a reflection algorithm. The Fixy compiler's job is to emit the right substrate instantiations; the substrate's job is to use reflection to discharge the structural obligations on those instantiations.

**Fixy AST reflection.** The `fixyc` elaborator exposes the typed AST to compile-time Fixy code through a `meta::*` family. A Fixy function decorated with `@[meta]` runs at elaboration time, takes the typed AST of its argument as a parameter, and returns a typed AST that the elaborator splices back in place of the call site. This is the mechanism for compile-time code generation, derive macros, and contract-driven boilerplate. For example:

```fixy
@[meta]
fn derive_serialize<T: type>(spec: meta::TypeSpec) : meta::FnDecl
  with meta::Pure;
= meta::synthesize_fn_decl_from_record_fields(spec);

@[derive(serialize)]
type config { host: string; port: nat; tls: bool; };
```

The `@[derive(serialize)]` annotation invokes the `derive_serialize` meta function with the typed `config` AST as its argument; the meta function returns a synthesized `serialize` function declaration that the elaborator splices into the module. The synthesis logic walks the record fields, emits one field-write call per field, and produces the function body. The whole process happens at elaboration time; the runtime sees only the synthesized function.

The `meta::*` API is approximately 300 functions covering AST inspection (field access, type inspection, attribute inspection), AST construction (decl synthesis, expression synthesis, type synthesis), elaborator integration (name resolution, type checking sub-expressions, error emission). The API is documented in `fixy_meta.md`; the implementation lives in `fixy/compiler/elab/meta/`.

**Emitted-code reflection for downstream artifacts.** The codegen phase emits reflection-walkable structures for every declared `contract`, every `vigil`, every `pipeline`, every `stage`. The downstream artifact generators (Cipher serializer, CNTP frame layout, cross-vendor CI test generator, Augur telemetry tag generator) walk those structures via P2996 reflection rather than via per-construct code. This means adding a new dimension to the substrate, a new annotation to the codegen, or a new downstream artifact does not require teaching the codegen about every existing construct — the new walker reflects over what is already there. The `safety/Reflectable.h` header (planned, foundation deliverable per the prior session) defines the `[[crucible::reflect_annotation]]` taxonomy that Fixy emission consumes; the eight annotations cover content-hashing, padding policy, serialization skip, external-pointer handling, inline cipher commit, CNTP frame designation, Cipher persistence, and cross-vendor test generation.

**Limits of reflection.** Reflection in Fixy does not perform arbitrary computation at compile time. The `meta::*` API is restricted to typed AST manipulation and elaborator integration; it cannot execute IO, allocate memory, or invoke runtime functions. Meta functions run in a sandboxed elaboration context with bounded execution time (default 100ms per invocation, configurable via workspace policy). A meta function that exceeds its budget aborts with a structured diagnostic indicating which call exceeded the limit and how to restructure. This restriction prevents the compile-time turing-tarpit problem where reflection-driven code generation produces unpredictable build times.

---

## 7. Error propagation

Errors in Fixy traverse three layers from origin to user-facing diagnostic: the Fixy compiler's own analysis (parse / elaborate / lower / codegen phases), the C++26 compiler's analysis of the emitted code, and the runtime checks (contract violations, capability mismatches, etc.) that fire during execution. Each layer has a corresponding diagnostic translation pass that produces user-facing messages anchored at the original `.fixy` source position.

**Layer 1: Fixy compiler diagnostics.** The lex, parse, elaborate, lower, and codegen phases emit structured diagnostics in a unified format. Every diagnostic carries an error code, a primary source position, optional secondary positions (for related code), a one-line summary, an extended explanation with context, and a suggested fix in the form of valid Fixy source code. The diagnostic format follows the structure FX uses: error code, file:line:col, source line with caret underline, "Goal:" (what the compiler needed), "Have:" (what was found), "Gap:" (what was missing or contradictory), "Suggestion:" (concrete code that resolves the issue). A typical elaborator diagnostic:

```
error[E044]: dimensional composition violates constant-time guarantee
  --> example.fixy:23:5
   |
23 |     fn encrypt_async(key: ref aes_key) : ciphertext
   |     ^^^^^^^^^^^^^^^^ this declaration combines incompatible dimensions
24 |       with CT, Async, Crypto;
   |            -- ----- here
   |
   = Goal:    function declared with constant-time observability (CT)
   = Have:    function also declared with Async effect (suspension permitted)
   = Gap:     async scheduling introduces timing variation that defeats
              constant-time guarantee per §6.8 rule E044
   = Suggestion:
              Either remove `Async` and run the crypto on a synchronous
              thread, or remove `CT` and use a higher-level constant-time
              primitive that wraps async-safe operations.
   = Reference: fx_collision_catalog.md §E044
```

Every error code corresponds to an entry in the collision catalog or to a structural failure category. The catalog ships approximately 80 error codes across the categories named in §10.10: T0xx (type errors), R0xx (refinement errors), E0xx (effect errors), M0xx (mode/ownership errors), S0xx (session type errors), I0xx (information flow errors), P0xx (proof errors), N0xx (precision errors), and W0xx (warnings). Each error code has a dedicated documentation entry (`fixyc explain E044`) that walks through the underlying soundness concern, the typical patterns that trigger it, and the standard remediation strategies.

The diagnostic emitter is approximately 1,000 lines and lives in `fixy/compiler/diag/`. It produces diagnostics in three formats: human-readable text (default), JSON (for LSP and CI tooling), and SARIF (for security audit tooling). The format is selected by the `--diagnostic-format` CLI flag.

**Layer 2: C++26 template error translation.** When the C++26 compiler reports an error against `fixyc`-emitted code, the error references the generated `.cpp` source rather than the original `.fixy` source. The translator catches common C++ template error patterns and rewrites them as Fixy-source-level diagnostics. Patterns the translator recognizes:

- **Concept failure on a substrate template.** The substrate uses concept gates (`requires CtxFitsFn<...>`, `ValidComposition<Fn>`, etc.) that fail with structured "constraint not satisfied" messages. The translator extracts the failing concept name and the participating types, maps them back through the codegen's bookkeeping table to the originating Fixy expression, and emits a Fixy-source-level error with the corresponding §6.8 rule citation.
- **Ambiguous overload at a generated dispatch table.** When the per-vendor dispatch table generated by codegen has overlapping entries (typically due to a user-declared `vendor module … targeting …` block whose target overlaps with a built-in target), the translator emits a Fixy-source-level error pointing at the overlap.
- **SFINAE failure on a stdlib call.** When a stdlib function declared with `requires SomeCapability<T>` fails to instantiate because the user's type `T` doesn't satisfy the capability, the translator emits a Fixy-source-level error citing the stdlib function's documented capability requirement.
- **Sizeof verification failure.** The substrate emits `static_assert(sizeof(Fn<...>) == expected_size)` for every `mint_fn` invocation. When this fails (typically because the user added a non-empty grade where an empty grade was expected), the translator emits a Fixy-source-level error with the dimension that exceeded its size budget.
- **Contract violation at compile time.** The substrate uses `pre()` and `post()` contracts that may evaluate at compile time when their arguments are constant. Compile-time contract violations emit as Fixy-source-level errors with the contract clause and the violating value.

The translator is approximately 1,500 lines and lives in `fixy/compiler/diag/cpp_translate.cpp`. It uses a hand-written pattern-matching engine over the GCC 16 diagnostic format. When a C++ error does not match any known pattern, the translator emits a fall-through diagnostic that includes both the original C++ error text and a pointer to the corresponding `.cpp:line` and the Fixy source position derived from the `#line` directives the codegen emits at every translation point. This ensures that even errors the translator does not understand still anchor at the original source.

**Layer 3: Runtime diagnostics.** Contract violations, capability mismatches, refinement failures, and effect leaks that fire at runtime emit through the existing `safety/Diagnostic.h` framework with FX-style structured output. Each runtime diagnostic carries the error code, the source position (recovered from the `CallSiteTable` and the per-function source-location annotation the codegen embeds), the violating value, and a suggested remediation. Runtime diagnostics route through Crucible's Augur subsystem for fleet-wide aggregation; a single contract violation on one Keeper produces a fleet-visible event that records the source position, the participating types, and the value that triggered the violation.

**LSP error streaming.** `fixy-lsp` invokes `fixyc --check --diagnostic-format=json` after every save and streams the resulting diagnostics to the editor. Diagnostics anchor at the original `.fixy` source position, even those originating from C++ template instantiations the translator caught. Hover over an error displays the full structured diagnostic (Goal/Have/Gap/Suggestion). Quick-fix suggestions (where the diagnostic includes one) are presented as code actions.

**Per-error-code documentation.** Every error code has a corresponding markdown entry under `fixy/docs/diagnostics/`. The `fixyc explain E044` command prints the entry. Entries cover: the underlying soundness concern (why this is a violation, not a stylistic preference), the typical user code patterns that trigger it, the standard remediation strategies, the literature citation when applicable, and the related error codes that may co-occur. The entries are written in the same direct, opinionated voice as the rest of the documentation.

---

## 8. Zero-runtime-cost preservation

The Fixy substrate maintains zero runtime cost through a combination of compile-time techniques inherited from the underlying C++26 implementation and codegen choices that prevent the language layer from introducing overhead.

**EBO collapse on dimension grades.** The Fn substrate uses `[[no_unique_address]]` on every dimension grade field. Empty grades (the most restrictive defaults, which carry no runtime data) collapse to zero bytes. A typical user-defined function with the most-restrictive defaults compiles to a struct whose size equals `sizeof(T)` where `T` is the function's return type (or the closure's captured state). The `algebra/Graded.h` substrate's regime taxonomy (regime-1 zero-cost EBO collapse, regime-2 grade-equals-T collapse, regime-3 grade-derived-from-value collapse) covers every non-residual case; the residual two-field case (regime-4) only arises for genuinely externally-set runtime metadata (e.g., a `Stale<T>` whose timestamp is set by a producer external to the value itself). The codegen never introduces dimension grades that would force regime-4 unless the user explicitly requested staleness or external timestamping.

**Contract semantics per translation unit.** Per CRUCIBLE.md §V and §XII, contracts compile under per-TU semantic. Hot-path TUs (marked by the `@[hot_path]` Fixy attribute) compile with `-fcontract-evaluation-semantic=ignore`, which discharges the contract clauses but does not evaluate them at runtime. Boundary TUs (marked by `@[boundary]` or by default for `pub fn` declarations on stdlib modules) compile with `enforce`, which evaluates contracts at the function entry. Release builds default to `observe` (log + continue) for non-hot, `enforce` for boundary, and `ignore` for hot. The Fixy codegen emits the appropriate `#pragma GCC contract_evaluation_semantic` directive at the top of each generated `.cpp` file based on the source module's annotations.

**Refinements as `[[assume]]` hints.** Refinement predicates emitted at function boundaries (where the contract semantic is `enforce`) propagate into the function body as `[[assume(pred(value))]]` annotations after the contract has been verified. This communicates the invariant to the C++ optimizer, which uses it for downstream code transformations: range analysis on integer values, alias analysis on pointer values, branch elimination when a refined value is used in a conditional. The optimizer can eliminate bounds checks, narrow integer types, and devirtualize calls based on the refinement. A `Refined<bounded_above<256>, uint64_t>` becomes a `uint64_t` with an `[[assume(value < 256)]]` annotation; subsequent uses of the value in array index expressions can elide bounds checks because the optimizer knows the range.

**Linear types as deleted-copy templates.** A `Linear<T>` compiles to a struct holding a `T` with the copy constructor and copy assignment operator deleted (with reason strings per the safety wrapper convention). The move constructor and move assignment are defaulted; the destructor is defaulted. The struct has no per-instance metadata — no refcount, no thread-local marker, no debug bookkeeping in release builds. In debug builds, an additional `is_consumed` flag tracks whether the value has been moved-from; this is the only additional state, and it costs one byte per instance, eliminated in release. The `Linear<T>::consume()` method consumes the value via `std::move` and asserts the value is not already consumed.

**Effect rows as phantom types.** `Computation<Row<E1, E2, ...>, T>` is a templated wrapper whose `Row` parameter pack carries the effect labels. The wrapper's runtime representation is just the underlying `T`; the row exists only at the type level. Effect operations (e.g., `await`, `fail`, custom handler operations) compile to direct invocations of the appropriate runtime primitive (e.g., `co_await` for async, `throw` would be the emission target for `fail` if exceptions were enabled, but Fixy uses `std::expected` instead and emits the appropriate error-propagation pattern). The effect row composes at the type level via `Subrow<R1, R2>` concept gates that the compiler verifies at every call boundary; no runtime check happens.

**Sessions as typestate.** A `Session<Proto>` compiles to a templated handle whose type changes as the protocol advances. Each `send`, `receive`, `select`, `branch` operation returns a new handle whose `Proto` parameter reflects the next state; the previous handle is consumed (linear). The runtime representation is a `Channel*` plus per-protocol-state metadata; no per-state branch occurs at runtime because the typestate is encoded at compile time. The `safety/Session*.h` family ships approximately twelve layers of session machinery (binary Honda, MPST, subtyping, association, crash-stop, etc.), all of which compile to direct typestate manipulation with zero runtime overhead.

**Per-vendor recipe specialization.** When a kernel is compiled with a specific NumericalRecipe (e.g., `Recipe::BITEXACT_TC`), Mimic's per-vendor backend specializes the emitted IR003\* code for that recipe. The emitted vendor binary contains exactly the instructions the recipe demands; no runtime branching on recipe choice occurs. A function declared `with Recipe::ORDERED` compiles to one set of vendor binaries; the same function declared `with Recipe::BITEXACT_TC` compiles to a different set. The KernelCache stores both variants under different content hashes; the runtime selects the appropriate variant at dispatch time based on the function's declared recipe.

**Verification harnesses.** The codegen emits `static_assert(sizeof(Fn<...>) == expected)` for every `mint_fn` invocation. The expected size is computed at codegen time from the dimension parameters (using the regime taxonomy from `algebra/Graded.h`). A dimension that exceeds its size budget — typically because the user added a non-empty grade where an empty grade was expected — produces a compile-time error with the specific dimension and the size delta. Disassembly snapshots for the hot-path functions live under `target/asm-snapshot/` (analogous to CRUCIBLE.md §VIII) and are diffed on every PR; an unexpected `lock` prefix, a spilled register, an indirect call where a direct call was expected fails the snapshot comparison and blocks the merge.

**Compile-time dimension folding.** When dimension grades are statically known (which is the common case — most dimensions resolve to specific values at compile time), the codegen folds them into the emitted code. A `with IO, Alloc(4096)` declaration compiles to a function whose effect row template parameter is `Row<effects::IO, effects::Alloc>` and whose space budget is encoded as a `BoundedAlloc<4096>` constraint that the substrate verifies at the allocation site. No runtime check on the budget occurs unless the budget is non-static; in that case, the codegen emits a `pre()` contract that fires only when `enforce` is the contract semantic for that TU.

---

## 9. Compiler optimizations

Beyond preserving zero cost, the Fixy compiler enables a set of optimizations that the substrate primitives expose to the C++26 backend, plus optimizations that operate at the IR000 / IR001 levels before C++26 emission.

**Refinement-as-assume propagation across function boundaries.** When a function declared with `pre length(xs) > 0` is called from a context where the caller has already verified `length(xs) > 0` (either through a previous refinement check, a pattern match, or arithmetic that establishes the property), the codegen propagates the `[[assume]]` annotation across the call boundary. The downstream optimizer sees the invariant inside the callee body even though the contract semantic is `ignore`. This requires the codegen to track refinement provenance through the call graph; the elaborator computes which refinements hold at each program point and the codegen emits the corresponding assumption annotations.

**Body opacity by default.** Per the FX discipline, function bodies are opaque to the SMT solver (when `verify` preset is active) and to the type system at function call sites. The caller's proof obligations see only the callee's signature — pre/post/effects/modes — as axioms. Editing a callee's body invalidates only that function's compilation cache entry; downstream callers stay cached. This default minimizes the blast radius of routine refactors and keeps incremental compile times low. The `@[transparent]` attribute opts a function into transparent compilation when the body is small and the caller benefits from inlining-driven analysis.

**Content-addressed deduplication at compile time.** Two functions with identical dimensions and identical bodies produce identical content hashes and identical compiled output. The codegen detects this via a hash table indexed by the function's `KernelContentHash` (computed from the dimensions and the body's IR000 form) and emits only one C++26 implementation per unique hash. Multiple Fixy declarations that resolve to the same content hash become aliases for the single implementation. This dedups across modules within a project and, via the L1 Cipher cache, across projects within a federation. A user-defined optimizer that happens to compile to the same content hash as an existing stdlib optimizer hits cache and skips the per-vendor compilation entirely.

**Per-vendor specialization via `vendor module` blocks.** A Fixy declaration of the form:

```fixy
vendor module mimic.nv targeting nv_sm_90, nv_sm_100 {
  fn matmul_specialized<m: nat, n: nat, k: nat>(...)
    requires Recipe::BITEXACT_TC and m % 128 == 0 and n % 128 == 0 and k == 8
    with Bg, Alloc;
  = ...;
}
```

declares a per-vendor specialization that takes precedence over the default kernel-template lowering for matching shapes and recipes. Forge's Phase E recognizes the specialization and emits the user's specialized body in place of the default template; if the shape or recipe does not match, the default template applies. This mechanism allows users to ship hand-tuned kernels for specific (shape, recipe, chip) tuples without abandoning the rest of the compilation pipeline. The specialized kernels participate in the cross-vendor numerics CI matrix identically to default-template kernels.

**Constant folding of dimension grades.** When a function's dimensions are statically known (all grade parameters resolve to compile-time constants), the codegen folds the grades into the emitted template instantiation. A function declared `with Recipe::BITEXACT_TC` compiles to a template instantiation parameterized over a `Recipe<BITEXACT_TC>` type tag; the type tag participates in compile-time dispatch (Mimic emits the recipe's specific code path) but carries no runtime data. A function declared `cost O(n)` where `n` is a runtime value carries the bound as a runtime field; the codegen emits a `pre()` contract that verifies the cost at the appropriate program point. Fixy does NOT carry wall-clock dimensions (latency, energy, power) — the compiler cannot prove physical bounds, and annotation-only dimensions create false guarantees, so they are deliberately excluded from the substrate.

**Devirtualization of effect handlers.** Effect handlers in Fixy are statically resolved when the handler is established by a lexically-enclosing `handle` block (the common case). The codegen emits direct calls to the handler's clauses in place of dynamic dispatch. The handler's continuation is similarly statically resolved when the handler is one-shot (the default); multi-shot handlers (`@[multishot]`) require additional bookkeeping to capture the continuation at every operation site, but this is the rare case. The result is that effect operations compile to direct function calls or direct returns, with no runtime polymorphism.

**Inlining controlled by attributes.** `@[transparent]` opts a function into aggressive inlining (the C++26 backend treats the function as a candidate at every call site regardless of release/debug profile). `@[no_inline]` opts a function out of inlining (the function boundary survives into the binary, useful for stack traces and profiler signatures). The default for stdlib functions is the C++26 backend's standard heuristic; the default for user-defined functions is `@[transparent]` in release and the C++26 standard heuristic in debug.

**Cross-module inlining via LTO.** The release build profile enables `-flto=auto` per CRUCIBLE.md §V, which permits cross-module inlining at link time. Functions declared `@[transparent]` in one module become inlining candidates at call sites in any module that links against them. The cross-module inlining is particularly valuable for the stdlib functions that wrap the substrate primitives; without LTO, every stdlib call would be a function call across the module boundary, which would defeat the EBO collapse the substrate relies on.

**MAP-Elites kernel search at the IR001 / IR002 boundary.** Forge's Phase H delegates kernel compilation to Mimic per-vendor backends, which run MAP-Elites search over the kernel's tile space, schedule space, and warp specialization. The search is parameterized by the function's NumericalRecipe and TargetCaps; the result is the fastest kernel that satisfies the recipe's determinism tier and the chip's resource constraints. The search runs asynchronously during compilation; the foreground compile path uses the Genesis Kernel Pack seed (per FORGE.md §23.7) for known-shape kernels and waits for MAP-Elites only on cache misses for novel shapes.

**Constant folding at the IR000 level.** The Fixy compiler folds compile-time-known expressions before lowering to IR001. Expressions involving only `comptime`-declared values, type parameters, and `Std.Math` primitives evaluate at elaboration time. The folded values appear as constants in the lowered IR000 and IR001; downstream optimizations (Forge phases C and D) benefit from the additional constants for algebraic simplification and fusion legality.

---

## 10. Topology specification

A Fixy program declares its topology requirements at two levels: per-stage capability requirements (in `.fixy` source) and per-node capability advertisements (in the fleet manifest). Canopy intersects the two at deploy time to compute per-stage assignment.

**Per-stage capability requirements.** Each stage of a Fixy pipeline declares the capabilities it requires through a `requires` clause:

```fixy
stage data_loader<r: region>(
  uri: string;
  ref(r) batch_size: nat;
) : stream<batch>
  requires Capability::S3Read,
           Capability::Network(Bandwidth >= 10gbps),
           Capability::Cpu(Cores >= 4);
  cpu(cores: 4, numa: local);
  with IO, Network, Alloc;
  cost O(batch_size);
= ...

stage trainer(
  ref model: weights;
  batch: batch;
) : (loss, gradients)
  requires Capability::GpuTensorCore,
           Capability::Memory(Hbm >= 80gb),
           Capability::FabricRdma;
  with Compute(Recipe::BITEXACT_TC), Async, Alloc;
  cost O(model.parameter_count);
= ...

stage aggregator(grads: stream<gradients>) : weights
  requires Capability::GpuTensorCore,
           Capability::FabricRdma,
           Capability::Memory(Hbm >= 40gb);
  with Compute(Recipe::BITEXACT_TC), Async;
= ...

vigil training_run(
  data_uri: string;
  model_uri: string;
) with Bg, IO, Network, Compute(Recipe::BITEXACT_TC);
  topology Auto;
= 
  let initial_weights = load_model(model_uri);
  let raw_data = data_loader(uri: data_uri, batch_size: 32);
  let losses_grads = raw_data |> map(trainer(model: initial_weights, _));
  let final_weights = losses_grads |> reduce(aggregator);
  save_model(final_weights, uri: f"{model_uri}.trained");
end vigil;
```

The capability vocabulary lives in `Std.Cluster` and covers the dimensions Crucible's Canopy mesh probes at Keeper startup: storage capabilities (`S3Read`, `S3Write`, `NfsRead`, `NfsWrite`, local NVMe), network capabilities (`Network(Bandwidth)`, `FabricRdma`, `FabricIb`, `FabricRoce`, `FabricNvlink`), compute capabilities (`Cpu(Cores)`, `Cpu(NumaNodes)`, `Gpu`, `GpuTensorCore`, `GpuFp8`, `Tpu`, `Trainium`), memory capabilities (`Memory(Ram)`, `Memory(Hbm)`, `Memory(Nvme)`), and special capabilities (`SecureEnclave`, `EbpfPrivileged`, `RealtimePriority`). Users define their own capabilities through `Std.Cluster::declare_capability` for application-specific requirements; the declaration adds a tag the fleet manifest can advertise.

**Per-node capability advertisement.** The fleet manifest is a YAML file (or equivalent in TOML or JSON) that declares per-node capabilities:

```yaml
apiVersion: crucible.io/v1
kind: CrucibleFleet
metadata:
  name: training-fleet-prod
spec:
  capabilities:
    s3_creds_secret: aws-creds  # secret name in K8s, or path on bare-metal
  nodes:
    - name: data-loader-node-0
      capabilities: [S3Read, Network(Bandwidth: 25gbps), Cpu(Cores: 32, NumaNodes: 2)]
    - name: gpu-node-0
      capabilities: [GpuTensorCore, GpuFp8, Memory(Hbm: 80gb), FabricRdma, FabricIb, Cpu(Cores: 64, NumaNodes: 2)]
    - name: gpu-node-1
      capabilities: [GpuTensorCore, GpuFp8, Memory(Hbm: 80gb), FabricRdma, FabricIb, Cpu(Cores: 64, NumaNodes: 2)]
    # ... additional nodes
  policy:
    matching: STRICT      # refuse to deploy if any stage has no capability match
    placement: Tight      # prefer co-locating stages on the same node when capabilities allow
```

**Canopy capability intersection.** At deploy time, the Canopy mesh computes per-stage assignment:

1. For each stage, compute the set of nodes whose advertised capabilities are a superset of the stage's required capabilities.
2. If the set is empty, the deployment fails with an error indicating which stage has no capability match. Under `policy.matching: ADAPT`, the deployment proceeds with the closest-matching node and emits a warning, but the matching is still required to be a superset.
3. Apply the placement policy. Under `Tight`, prefer nodes that already host one of the stage's upstream or downstream stages (data locality). Under `Spread`, prefer nodes that host no other stages from this vigil (resource isolation). Under `Custom`, evaluate a user-supplied placement function declared in `Std.Cluster::placement_policy`.
4. Materialize the assignment in the fleet's persistent state via Raft commit. Each Keeper observes the assignment at the next epoch boundary and instantiates the assigned stages locally.

**Topology declarations in source.** A vigil can declare its topology explicitly:

```fixy
vigil training_run(...) ... 
  topology explicit {
    data_loader: 1,                        # one instance, on any matching node
    trainer: replicate 8 across nodes,     # one instance per node, 8 nodes total
    aggregator: 1 on coordinator,           # one instance on the designated coordinator
    pipeline: data_loader -> trainer*8 -> aggregator,
  };
= ...
```

The explicit form constrains Canopy's matching: the user has specified the multiplicities and placement constraints, and Canopy must satisfy them or fail the deployment. The default `Auto` form lets Canopy choose multiplicities based on the workload's measured throughput per the Forge Phase K partition solver (FORGE.md §25.6).

**Capability negotiation during deployment.** When a stage requires a capability that no node in the fleet provides, the deployment fails by default. Under the `ADAPT` policy, Canopy searches for the closest match — a node that provides a superset of the capabilities and a subset of the required capabilities. If no closest match exists either, the deployment fails. Capability negotiation happens at deploy time, not at runtime; the fleet manifest is the source of truth, and changing it requires a redeploy.

**Capability evolution over time.** The fleet manifest may change over time: nodes added, nodes removed, capabilities upgraded (e.g., a node receives a GPU upgrade and now advertises `GpuFp8` in addition to `GpuTensorCore`). Each change triggers a Canopy epoch bump; running stages observe the change and Canopy re-runs the placement algorithm at the next iteration boundary. Stages that no longer have a matching node are paused and resumed when a match becomes available, or terminated if the match never returns.

**Failure semantics.** When a node hosting an assigned stage fails (detected by SWIM gossip per CRUCIBLE.md §7), Canopy commits a new membership at epoch E+1 and re-runs the placement algorithm for the failed stages. If the surviving fleet still satisfies the capability requirements, the failed stages reassign and resume. If the surviving fleet does not satisfy the requirements (e.g., the fleet had only one S3-capable node and that node failed), the vigil enters a degraded state and emits a structured event; user-defined policy (declared in the vigil's `on_capability_loss` clause) determines whether the vigil pauses for manual intervention or terminates.

---

## 11. The Canopy data plane

Data flow between stages is mediated by Cipher-backed channels. A Fixy `stream<T>` declaration desugars to a Cipher channel; producers write to their local Cipher hot tier and consumers pull from the producer's Cipher tier via CNTP RDMA reads. The data plane integrates the existing Cipher three-tier persistence (CRUCIBLE.md §9), CNTP zero-syscall transport (CRUCIBLE.md §6), and Canopy mesh routing (CRUCIBLE.md §7).

**Stream-as-Cipher-channel.** A `stream<T>` declaration in Fixy compiles to a `Std.Data::Stream<T>` instance backed by a `Cipher::Channel<T>`. The channel has a producer endpoint and one or more consumer endpoints. Producers append values to the channel via `stream.send(value)`; consumers receive values via `stream.recv()`. The channel's tier — hot, warm, or cold — defaults to hot (in-RAM) and is configurable per-stream via `Std.Data::tier(stream, Cipher::Tier)` for streams whose volume exceeds RAM capacity or whose lifetime exceeds the producing process.

**Backpressure.** Each stream declares its bounded depth at construction. The producer blocks (or returns `BackpressureError` per the user's choice) when the buffer is full; the consumer blocks (or returns `EmptyError`) when the buffer is empty. The default depth is the prefetch parameter (typically 4-8 for ML data loading) — small enough to avoid memory bloat, large enough to hide producer-consumer latency mismatch.

**Producer-consumer routing.** When a producer and a consumer are on different nodes, the data plane routes the stream through CNTP. The producer writes to its local Cipher hot tier; the consumer issues an RDMA read from the producer's Cipher tier (using the producer's CNTP endpoint, which is registered with Canopy at Keeper startup). The RDMA read happens out-of-band of the stream's append-and-poll API; the consumer's `stream.recv()` blocks on a local condition variable that the RDMA completion notifies.

**Prefetch policy.** Consumers declare a prefetch depth per stream. The runtime issues `prefetch_depth` RDMA reads ahead of the consumer's current position; arrived data lands in the consumer's local Cipher hot tier and is delivered via local memory access on `stream.recv()`. Prefetch depth is auto-tuned by Augur based on the observed consumer throughput; if the consumer consistently waits for data, prefetch depth increases; if the consumer's local Cipher tier consistently overflows with prefetched data that is not yet consumed, prefetch depth decreases.

**Replication policy per stream.** A stream may declare a replication factor for fault tolerance:

```fixy
let raw_data : stream<batch> = data_loader(...)
  with Replication(factor: 3, policy: Async);
```

`factor: 3` means three Keepers hold a replica of each value before it is acknowledged to the producer. `policy: Async` means the acknowledgment returns to the producer after one replica is durable, with the remaining replicas propagating in the background. `policy: Sync` means the acknowledgment returns only after all replicas are durable. `policy: Quorum` means the acknowledgment returns when a majority of replicas are durable. Replication uses Crucible's existing RAID-α policy (CRUCIBLE.md §13) — content-addressed deduplication ensures that replicas of identical values share storage across Keepers.

**Cross-tier flow.** A stream may flow across Cipher tiers as it ages. Hot-tier values that have been consumed and are no longer in any consumer's prefetch window may demote to warm tier. Warm-tier values that have not been accessed for a configurable interval may demote to cold tier. The demotion is a pointer update in the Cipher metadata; the value's content hash does not change, so consumer references continue to work transparently.

**The S3-only-on-some-nodes pattern.** A common pattern is to have S3 credentials only on a subset of nodes for security isolation. The `data_loader` stage requires `Capability::S3Read` and is assigned by Canopy to one of those nodes. The `trainer` stage requires `Capability::GpuTensorCore` and is replicated across the GPU nodes. The producer-consumer routing handles the cross-node data flow: the data_loader on the S3-capable node fetches from S3, parses, transforms, writes batches to its local Cipher hot tier; the trainers on the GPU nodes pull batches via RDMA from the data_loader's Cipher tier, with prefetch depth tuned to keep the GPUs fed. The data crosses the network exactly once per batch (RDMA-pull from data_loader to trainer) regardless of how many trainers consume the same batch — content-addressed deduplication ensures that multiple trainers consuming the same batch share the underlying value rather than fetching it multiple times.

**Dataloader as a first-class Vigil concept.** A data loader is a stage like any other in a Fixy vigil, but the stdlib provides a `Std.Data::Dataloader` family that bundles common patterns: shard discovery (enumerate files in an S3 prefix), parsing (zstd, msgpack, parquet, arrow, custom), augmentation (transformations applied per-record), batching (assemble N records into a batch), shuffling (Philox-seeded permutation across shards), prefetching (out-of-band fetch ahead of consumer demand), and cursor checkpointing (record the position in the data stream for replay). The data loader's cursor is part of the vigil's checkpoint state; on recovery from a Keeper failure, the cursor reloads to the last-acknowledged position and the data loader resumes deterministically.

**Backpressure interaction with replication.** When replication is enabled, the producer's `send` operation blocks until the configured replication policy is satisfied. Sync replication blocks the producer on the slowest replica; async replication blocks only on the first acknowledgment. In high-throughput settings, async replication is preferred — the producer continues at the speed of the fastest replica, and the slower replicas catch up in the background. Augur monitors the replication lag; if a replica consistently falls behind, Canopy may demote it to a non-quorum replica or evict it from the replica set entirely.

**Channel content-addressing.** Each value sent on a stream is content-hashed at the producer side. The hash becomes the value's identifier in Cipher; consumers reference the value by hash. This enables several optimizations: deduplication across producers (two producers sending identical values share storage), idempotent retransmission (a producer that failed mid-send can re-send; the consumer recognizes the duplicate hash and discards), and verification (a consumer can verify the content hash on receipt to catch transmission errors).

---

## 12. CPU compute specification

Stages declare their CPU requirements through a `cpu(...)` clause:

```fixy
stage compute_intensive(...) ...
  cpu(cores: 32, numa: local, isolated: true);
= ...

stage io_bound(...) ...
  cpu(cores: 4, numa: any, isolated: false);
= ...

stage realtime_signal(...) ...
  cpu(cores: 1, numa: local, isolated: true, priority: realtime);
= ...
```

**Cores.** The `cores` parameter declares the number of CPU cores the stage needs. Values: a positive integer for a fixed count, `all` for all available cores, `all_p_cores` for all performance cores (excluding efficiency cores on hybrid systems), `auto` to let the AdaptiveScheduler choose based on the workload's measured working set per the cache-tier rule (CLAUDE.md §IX). The default is `auto`.

**NUMA placement.** The `numa` parameter declares the NUMA placement preference. Values: `local` to place all of the stage's work on a single NUMA node (preferred for cache-resident workloads), `spread` to distribute across NUMA nodes (preferred for memory-bandwidth-bound workloads that benefit from multiple memory controllers), `any` to leave placement to the kernel scheduler, or an explicit node ID for fixed placement. The default is `auto`, which the AdaptiveScheduler selects based on the workload's working set size and access pattern.

**Isolation.** The `isolated: true` flag requests that the stage run on cores excluded from kernel scheduling decisions for other workloads. On bare-metal deployments, this requires `isolcpus=` in the kernel boot parameters and the appropriate cgroup cpuset configuration (CRUCIBLE.md §16.2). On K8s, the operator translates this to a `dedicated-cores` annotation that the kubelet honors. Isolated stages do not preempt or get preempted by non-isolated workloads on the same node.

**Priority.** The `priority` parameter selects the scheduler class. Values: `default` (`SCHED_OTHER`), `low` (`SCHED_OTHER` with elevated nice value), `high` (`SCHED_OTHER` with reduced nice value), `realtime` (`SCHED_DEADLINE` per CRUCIBLE.md §16.1, requires `CAP_SYS_NICE`), `fifo` (`SCHED_FIFO`, requires explicit user authorization in the workspace policy). The default is `default`.

**Mapping to the AdaptiveScheduler.** The cpu clause translates to a `concurrent::Workload` declaration that the AdaptiveScheduler consumes. The scheduler integrates with the existing `concurrent/Topology.h` (sysfs probe of cores, caches, NUMA distances), `concurrent/ParallelismRule.h` (cache-tier decision), and `concurrent/scheduler/Policies.h` (per-policy scheduling). The stage's parallelism is set at deploy time; runtime adjustments (e.g., scaling up cores when measured throughput indicates capacity headroom) happen via Augur recommendations that the user may apply via `crucible fleet adjust`.

**HPC use case.** A finite-element solver running on 1024 cores across 32 nodes:

```fixy
stage fem_step<r: region>(
  ref(r) mesh: ref Mesh,
  ref(r) state: ref State,
  dt: f64,
) : State
  requires Capability::Cpu(Cores >= 32),
           Capability::FabricRdma,
           Capability::Memory(Ram >= 64gb);
  cpu(cores: all_p_cores, numa: spread, isolated: true);
  with Compute(Recipe::BITEXACT_STRICT), Network;
  cost O(mesh.element_count);
= 
  let local_assembly = assemble_local(mesh, state, dt);
  let halo_exchanged = exchange_halos(local_assembly);
  let solved = local_solve(halo_exchanged);
  return solved;
end stage;

vigil simulation(...)
  topology explicit {
    fem_step: replicate 32 across nodes,
    pipeline: load_mesh -> initial_state -> fem_step*32 -> save_result,
  };
= ...;
```

The 32 instances of `fem_step` each consume all of their node's performance cores, distribute work across NUMA nodes for memory bandwidth, run isolated from other workloads. The `exchange_halos` step uses CNTP RDMA over the FabricRdma capability; the `local_solve` step is pure compute. The vigil composes them into a 1024-core simulation running across the fleet, with Crucible providing the substrate (Canopy assignment, CNTP transport, Cipher persistence for checkpointing, Keeper failure recovery).

**Mixed CPU and accelerator use case.** A training workload with CPU-bound data loading and GPU-bound training:

```fixy
stage data_loader<r: region>(uri: string; ref(r) batch_size: nat) : stream<batch>
  requires Capability::S3Read, Capability::Cpu(Cores >= 4);
  cpu(cores: 4, numa: local, isolated: false);
  with IO, Network, Alloc;
= ...;

stage trainer(ref model: weights; batch: batch) : (loss, gradients)
  requires Capability::GpuTensorCore, Capability::Memory(Hbm >= 80gb);
  with Compute(Recipe::BITEXACT_TC), Async, Alloc;
= ...;

vigil training_run(...) ...
  topology Auto;
= 
  let data = data_loader(uri: data_uri, batch_size: 32);
  data |> map(trainer(model: initial_weights, _)) |> ...;
end vigil;
```

The data loader runs on 4 CPU cores per node (any node with the S3 capability); the trainer runs on the GPU on each GPU-capable node. Canopy assigns the stages, the data plane routes batches from data loaders to trainers via Cipher-backed CNTP streams, the trainers compute on the GPU.

**Interaction with realtime.** A realtime stage (declared `priority: realtime`) is admitted to `SCHED_DEADLINE` with the runtime/deadline/period parameters declared at the workspace policy level. The scheduler provides admission control plus elevated-priority best-effort execution against the declared budget — Linux SCHED_DEADLINE is not a hard real-time guarantee, and the kernel can still miss the deadline under load. Misses are tracked by the watchdog (CRUCIBLE.md §16.1) and emit Augur events. If the stage's measured execution consistently approaches the deadline, Augur recommends adjusting the parameters. If misses accumulate, the watchdog downgrades the stage to `SCHED_FIFO` and then to `SCHED_OTHER`, with structured diagnostic events at each step. Fixy does NOT promise wall-clock latency through the type system — `priority: realtime` is an OS-level scheduling-class hint, nothing more.

---

## 13. Empirically-fastest mesh topology

Collective operations — all-reduce, all-gather, reduce-scatter, broadcast, all-to-all — require a topology over the participating nodes. The choice of topology (ring, tree, halving-doubling, hierarchical, in-network offload) substantially affects per-collective latency and bandwidth utilization. Fixy declares topology requirements declaratively and lets the runtime choose the empirically-fastest topology based on Meridian's measured matrix and Augur's continuous monitoring.

**Declarative topology in source.** A collective operation in Fixy declares its algorithm and topology preferences:

```fixy
stage all_reduce_grads(
  local_grads: gradient;
  ref group: ReduceGroup;
) : gradient
  with Collective(
    Op: Sum,
    Algorithm: AugurChosen,        # let runtime pick the empirically-fastest
    Topology: AugurFastest,
    Recipe: BITEXACT_TC,
  );
  cost O(group.size);
= ...;
```

The `Algorithm: AugurChosen` and `Topology: AugurFastest` defaults delegate the choice to the runtime. Alternative declarations: `Algorithm: Ring`, `Algorithm: Tree`, `Algorithm: HalvingDoubling`, `Algorithm: Hierarchical`, `Algorithm: InNetworkOffload`. Each names a specific algorithm; the runtime uses the named algorithm without further optimization. Users select an explicit algorithm when they have specific knowledge about the workload that the runtime's heuristics do not capture; the default delegates to the runtime.

**Meridian's N×N probe.** At Keeper startup, Meridian probes the latency and bandwidth between every pair of nodes in the fleet. The probe runs over CNTP using small-message RTT for latency and large-message throughput for bandwidth. The probe is repeated periodically (configurable interval, default 10 minutes) to detect link degradation. The result is an N×N matrix per metric: `latency[i][j]` is the measured round-trip time between nodes i and j; `bandwidth[i][j]` is the measured throughput. The matrix is stored in the fleet's persistent state via Raft commit so that all Keepers observe the same matrix.

**Z3 partition solver.** Forge's Phase K (FORGE.md §25.6) consumes the Meridian matrix and the workload's per-stage compute and communication requirements to compute an optimal 5D parallelism partitioning (TP, DP, PP, EP, CP). The solver is Z3-based; it formulates the partition as a constraint satisfaction problem with the objective of minimizing predicted step time. The cost model includes: per-stage compute time (from Mimic's fast cost estimator), per-collective communication time (from the Meridian matrix and the chosen algorithm), pipeline bubble overhead (schedule-dependent), and per-collective overhead. The solver's solution is the assignment of stages to partition dimensions; the runtime materializes the assignment.

**Per-collective algorithm choice.** Within a partition, each collective operation chooses its algorithm based on the message size, the participant set, and the topology:

| Algorithm | Best for | Latency complexity | Bandwidth complexity |
|---|---|---|---|
| Ring | Bandwidth-bound large messages | O(N) | 2(N-1)/N · message_size |
| Tree | Latency-bound small messages | O(log N) | log(N) · message_size |
| Halving-doubling | Mid-size messages | O(log N) | 2 · message_size |
| Hierarchical | Multi-tier topologies | O(log N1 + log N2) | depends on tier |
| InNetworkOffload | When fabric supports SHARP / equivalent | O(log N) | hardware-bound |

The choice is parameterized by the measured Meridian matrix; Z3 evaluates each algorithm against the cost model and selects the lowest-cost choice. The selection is recorded in the partition's metadata and applied at every invocation of the collective until the partition is re-solved.

**Augur drift detection.** Augur samples per-collective timings at runtime — every Nth invocation (default 1%) measures the actual latency and bandwidth and compares to the predicted values from the cost model. Sustained drift greater than 10% for 100+ samples triggers a re-solve: Z3 re-runs with the updated Meridian matrix (which Augur may have triggered to refresh) and the updated cost model. If the new partition differs from the current one, the new partition commits via Raft at the next iteration boundary and the stages reassign accordingly.

**Custom collective algorithms.** Users may declare custom collective algorithms via `vendor module mimic.<vendor>.collective` blocks:

```fixy
vendor module mimic.nv.collective targeting nv_sm_90 {
  fn ring_with_compression(
    local_grads: gradient;
    ref group: ReduceGroup;
  ) : gradient
    with Collective(Op: Sum, Recipe: BITEXACT_TC), Async;
  = ...;
}
```

The custom algorithm becomes a candidate that Z3 considers alongside the built-in algorithms. Z3 invokes the custom algorithm's cost model (which the user must declare via a `cost_model` clause) to estimate its predicted time; if the custom algorithm wins, the runtime invokes it. The custom algorithm participates in the cross-vendor numerics CI matrix identically to built-in algorithms; if it produces results outside the recipe's tolerance, the build fails.

**HPC analog.** Non-ML collectives — global reductions in a Monte Carlo simulation, ghost-cell exchanges in a finite-element solver, all-to-all transposes in an FFT — use the same topology selection mechanism. The Fixy declaration wraps the operation in `Std.Collective::custom_op` with the appropriate combine operation, identity element, and reduction scope; the runtime applies the same Z3 partition solver and Augur drift detection. The HPC case differs from the ML case only in the operation semantics; the topology selection and runtime adaptation are identical.

---

## 14. Accelerator allocation

Accelerator allocation is a special case of capability matching. Fixy stages declare accelerator requirements through the `requires` clause; nodes advertise accelerator capabilities via the fleet manifest; Canopy intersects the two at deploy time. Beyond the basic capability vocabulary, accelerator allocation involves capability negotiation (matching a "requires GPU with 80GB HBM" against the available pool), dynamic reassignment on failure, and explicit-vs-found semantics.

**Accelerator capability vocabulary.** The `Std.Cluster` module declares accelerator capabilities at varying granularity:

| Capability | Granularity |
|---|---|
| `Gpu` | Any GPU |
| `GpuTensorCore` | GPU with tensor core support (NV sm_70+, AM CDNA, etc.) |
| `GpuFp8` | GPU with FP8 tensor core (NV sm_89+, AM CDNA3+) |
| `GpuFp16` | GPU with FP16 tensor core |
| `Tpu` | TPU of any generation |
| `TpuV5p`, `TpuV6e`, `TpuV7` | Specific TPU generations |
| `Trainium` | Trainium of any generation |
| `Trn2`, `Trn3` | Specific Trainium generations |
| `AppleAGX` | Apple GPU |
| `AppleAMX` | Apple matrix coprocessor |
| `Memory(Hbm: bytes)` | Minimum HBM capacity |
| `Memory(Vram: bytes)` | Minimum VRAM (any kind) |
| `FabricNvlink` | NVLink-capable |
| `FabricXgmi` | xGMI-capable |
| `FabricIci` | TPU ICI-capable |
| `FabricNeuronlink` | Trainium NeuronLink-capable |
| `FabricRdma` | RDMA-capable (RoCE, IB, etc.) |
| `FabricUnifiedMemory` | Unified CPU/GPU memory (Apple, Grace+H100) |

The vocabulary is extensible through `Std.Cluster::declare_capability`; users add application-specific capabilities (e.g., a research lab's custom NPU advertises `ResearchLabNpu` and stages requiring that NPU declare `requires Capability::ResearchLabNpu`).

**Capability advertisement at Keeper startup.** The Keeper daemon probes the local hardware at startup using Mimic's per-vendor `detect_local_chip` routines (MIMIC.md §40). The probe returns a vendor-specific chip descriptor (e.g., `nv_sm_100a` for an H200, `am_gfx942` for an MI300X, `tpu_v5p`, etc.). The Keeper translates the descriptor into the abstract capability vocabulary and advertises the resulting capability list to Canopy via SWIM gossip. Canopy aggregates the advertisements into the fleet's capability matrix.

**Capability matching at deploy time.** When `crucible fleet apply` runs, Canopy intersects each stage's `requires` clause with the fleet's capability matrix. The intersection algorithm is straightforward set membership: a node satisfies a stage's requirements if the node's advertised capabilities are a superset of the stage's required capabilities, and the node's resource quantities (cores, memory, bandwidth) satisfy the stage's quantitative requirements. The algorithm is deterministic; the same fleet manifest and the same source produce the same assignment.

**"Specified" vs "found" semantics.** A stage declaring `requires Capability::GpuFp8` may be satisfied by any node that advertises `GpuFp8` — whether the user explicitly enumerated the capability in the fleet manifest (specified) or the Keeper's probe at startup discovered the capability (found). The two are unified at the capability vocabulary level; downstream matching does not distinguish them. The fleet manifest may explicitly enumerate capabilities to override the probe (e.g., to disable a known-faulty FP8 unit on a specific node); the override applies before the matching algorithm runs.

**Mixed-vendor scheduling.** A vigil that benefits from specific vendors (e.g., FP8 training that performs best on NVIDIA H200) may declare a vendor preference:

```fixy
stage trainer_fp8(...) ...
  requires Capability::GpuFp8;
  prefer Vendor::NV;
= ...;
```

The `prefer` clause is a soft constraint: Canopy first attempts to match on nodes of the preferred vendor; if no preferred-vendor nodes are available, Canopy falls back to any matching node. The `require Vendor::NV` form (without `prefer`) is a hard constraint: only NVIDIA-vendor nodes are considered.

**Dynamic reassignment on failure.** When a node hosting an assigned stage fails, Canopy commits a new membership at epoch E+1 and re-runs the matching algorithm. The reassigned stages resume from the last Cipher checkpoint. If the failed node held a unique capability (e.g., the only S3-capable node), the matching may fail; the vigil enters a degraded state per the policy declared in `on_capability_loss`. Common policies: `pause` (halt the vigil and emit a manual-intervention event), `terminate` (halt the vigil with a final state save), `degrade` (continue with a reduced configuration that still satisfies the matching; e.g., dropping a non-critical stage if the vigil's `topology Auto` allows).

**Heterogeneous fleets.** A vigil that runs on a heterogeneous fleet — mixed NVIDIA + AMD + TPU + Trainium — invokes the cross-vendor numerics CI matrix at compile time to verify that every (kernel, recipe, vendor) tuple produces ULP-bounded equivalent results. If the matrix passes, the vigil may run on the heterogeneous fleet under the `Recipe::BITEXACT_TC` (or stronger) declaration. Forge's Phase K partition solver considers per-vendor compute throughput when assigning work; faster vendors receive proportionally more work per the `CAPACITY_WEIGHTED` policy (FORGE.md §25.8).

**Memory capacity matching.** A stage declaring `requires Capability::Memory(Hbm: 80gb)` matches nodes with at least 80GB of HBM. For partial-fit cases (a stage requires 40GB; a node has 80GB; another stage on the same node also requires 40GB), Canopy performs bin-packing across the fleet's nodes to maximize density. The bin-packing algorithm is deterministic; it considers the fleet's full assignment as a single optimization. Users may override with explicit placement constraints in the topology declaration.

**Accelerator pool sharing.** Multiple vigils running on the same fleet share the accelerator pool. Each vigil's stages claim a subset of the pool's accelerators; Canopy tracks per-accelerator allocation and refuses to over-subscribe. If a new vigil's deployment would over-subscribe, the deployment fails with an error indicating which accelerator pool has insufficient capacity. Users may resolve by adjusting the new vigil's requirements, scaling up the fleet, or evicting an existing vigil.

---

## 15. Deployment through Crucible

A Fixy program becomes a deployable artifact via `fixyc compile` and runs on a Crucible fleet via `crucible fleet apply`. The deployment pipeline integrates with K8s, SLURM, and bare-metal launchers per the existing CRUCIBLE.md §8 design.

**The `.fixy-pkg` artifact.** `fixyc compile app.fixy` produces `app.fixy-pkg`, a content-addressed bundle containing:

- Per-vendor compiled binaries (one per (vendor, chip) tuple the source declares targeting)
- IR snapshots at L1 (IR002, vendor-neutral) and L2 (IR003\*, vendor-family) levels
- Capability manifest (the union of all `requires` clauses across the source's vigils)
- Default fleet configuration (declared in the source via `vigil ... fleet { ... }` clauses)
- Cipher seed (initial values for any persistent state the vigils declare)
- Source position table (for runtime diagnostic generation)
- Cross-vendor CI test results (proving the build passed the numerics matrix)

The artifact is content-addressed by a Merkle hash over its contents. Two builds of identical source on identical compiler versions produce byte-identical artifacts; deploying an unchanged artifact is a no-op.

**`crucible fleet apply` command.** The deployment command is:

```bash
crucible fleet apply -f fleet.yaml --app app.fixy-pkg
```

The command reads the fleet manifest, the artifact, and the deployment policy. It validates that the artifact's capability requirements are satisfied by the fleet, computes the per-stage assignment via Canopy's intersection algorithm, and submits the assignment to the Crucible operator (or directly to the SLURM/bare-metal launcher).

**K8s operator path.** The `crucible-operator` is a Kubernetes controller (CRUCIBLE.md §8) that extends the existing `CrucibleCluster` CRD with an `app` field referencing the `.fixy-pkg`:

```yaml
apiVersion: crucible.io/v1
kind: CrucibleCluster
metadata:
  name: training-prod
spec:
  app: 
    artifact: s3://my-artifacts/training_v23.fixy-pkg
    artifact_hash: sha256:abc123...
  compute:
    gpu_replicas:
      min: 8
      max: 64
    cpu_replicas:
      min: 2
      max: 16
  policy:
    fleet: STRICT
    elastic: true
```

The operator pulls the artifact from the declared location, validates the hash, distributes the per-vendor binaries to matching nodes, deploys Keeper pods (one per node), and monitors the resulting fleet's status. Scale operations (up or down) translate to operator actions; a `kubectl scale crucible-cluster training-prod --replicas=128` triggers the operator to add Keeper pods, which join Canopy via SWIM, and the next iteration boundary reshards the assignment.

**SLURM launcher path.** For HPC environments running SLURM, the deployment uses `srun`:

```bash
sbatch --nodes=64 --ntasks-per-node=1 --gpus-per-node=8 \
  --wrap="srun crucible-keeper --app=/path/to/training_v23.fixy-pkg \
                                --seed-peers=$(scontrol show hostname $SLURM_NODELIST | head -1):7831"
```

SLURM allocates the nodes; each node starts a Keeper via `srun`; Keepers discover each other via the seed-peer hint; Canopy converges; the artifact deploys; training proceeds. Elasticity under SLURM is limited by SLURM's static allocation — the fleet stays the same size for the job's duration unless the user submits multiple chained jobs.

**Bare-metal launcher path.** For bare-metal deployments, each node runs a systemd unit:

```ini
[Unit]
Description=Crucible Keeper
After=network-online.target

[Service]
ExecStart=/usr/local/bin/crucible-keeper \
  --app=/var/lib/crucible/training_v23.fixy-pkg \
  --config=/etc/crucible/node.json
Restart=always
AmbientCapabilities=CAP_NET_ADMIN CAP_BPF CAP_IPC_LOCK CAP_SYS_NICE
LimitMEMLOCK=infinity

[Install]
WantedBy=multi-user.target
```

The Keeper joins Canopy via the gossip seed-peer list in the config; the artifact deploys; the fleet runs. No orchestrator is required; updates to the artifact are deployed via standard package-distribution mechanisms.

**Fleet manifest format.** The fleet manifest is YAML by default (TOML and JSON supported as well). It declares: cluster name, capability declarations per node, S3 (or equivalent storage) credentials, network configuration, security policy, and deployment policy. The manifest is the single source of truth for the deployment; changes to the manifest trigger a redeploy. The manifest may reference external secrets (K8s secrets, Vault, AWS Secrets Manager) for credentials; the operator resolves the references at deploy time.

**Update and rollback.** A new artifact version deploys via `crucible fleet apply` with the updated artifact reference. The Crucible operator implements a rolling update: half the Keepers (at a time, configurable via `policy.update_batch_size`) update simultaneously, the new Keepers re-join Canopy, the operator verifies their health, then the other half updates. Rollback to a previous version uses the same mechanism with the previous artifact. Cipher state survives the update; the new version inherits the previous version's persistent state.

**Local development.** A `fixyc run app.fixy --local` invocation runs the artifact on a single-node fleet (the developer's machine). The single-node fleet uses CPU-only Mimic backends by default; if accelerators are available locally, the developer may opt in via `--accelerators=local-gpu`. The local mode is useful for development and testing; production deployments use the multi-node path.

**CI and CD integration.** The artifact is the natural unit of CI. A CI pipeline runs `fixyc compile app.fixy` to produce the artifact, runs `fixyc test app.fixy` to execute the tests (which compile internally to additional vigils), publishes the artifact to a registry (S3, container registry, etc.), and triggers deployment via `crucible fleet apply`. The artifact's content-addressed nature makes the pipeline reproducible: the same source produces the same artifact, and the same artifact produces the same deployment.

---

## 16. Beyond ML — HPC and mixed workloads

Fixy is not specific to machine learning. The language describes typed compute; the runtime executes typed compute. The stdlib happens to ship ML primitives (`Std.Tensor`, `Std.Kernel`, `Std.Recipe`) because that is the workload class motivating the project, but the language and runtime accommodate HPC, data engineering, scientific simulation, and mixed workloads with no architectural changes.

**The Vigil concept generalizes.** A vigil is a typed program with state running on a fleet. The state may be model weights and optimizer state (the ML case), it may be a finite-element mesh and per-cell physical state (the FEM case), it may be a Monte Carlo random walk's accumulated samples (the simulation case), it may be a streaming aggregation's accumulated counts (the data engineering case). The vigil's lifecycle — checkpoint, recover, redistribute, terminate — is the same regardless of what the state means.

**HPC primitives in the stdlib.** `Std.HPC` provides primitives for HPC workloads:

- `Std.HPC.fft` — Fast Fourier Transform variants (FFT, RFFT, NUFFT, multi-dimensional)
- `Std.HPC.sparse` — Sparse linear algebra (CSR / CSC / BSR / COO matrix-vector multiply, sparse direct solvers, iterative solvers)
- `Std.HPC.dense` — Dense linear algebra (LU, QR, SVD, Cholesky, eigenvalue decompositions)
- `Std.HPC.collective` — MPI-style collectives (Send, Recv, Bcast, Reduce, Allreduce, Allgather, Alltoall) wrapping the same Crucible CNTP transport but exposing the MPI-equivalent semantics
- `Std.HPC.partitioning` — Domain decomposition primitives (graph partitioning via METIS-equivalent algorithms, block partitioning, halo exchange)
- `Std.HPC.io` — Parallel I/O (HDF5-equivalent, ADIOS-style staging, parallel checkpointing)

The primitives compile to IR001 just like ML primitives; Forge optimizes them; Mimic emits per-vendor code. The cross-vendor numerics CI matrix verifies that every HPC primitive produces ULP-bounded equivalent results across vendors when the recipe demands it.

**Simulation primitives.** `Std.Sim` provides simulation primitives:

- `Std.Sim.monte_carlo` — Monte Carlo sampling, importance sampling, stratified sampling
- `Std.Sim.fem` — Finite element method primitives (mesh, basis functions, assembly, halo exchange)
- `Std.Sim.fdtd` — Finite-difference time-domain primitives (stencil computations, time-stepping)
- `Std.Sim.particles` — N-body simulation primitives (tree codes, fast multipole methods)
- `Std.Sim.cfd` — Computational fluid dynamics primitives (advection, diffusion, pressure-Poisson)

The primitives are ML-adjacent in the sense that they share the IR pipeline and the runtime substrate; they differ in the operation semantics they implement. A Monte Carlo simulation reduces over random samples; an FEM solver assembles stiffness matrices and exchanges halos; an FDTD simulation iterates over a stencil. Each pattern has a corresponding stdlib primitive that compiles to IR001 and benefits from Forge's optimization pipeline.

**Mixed workloads.** A reinforcement learning agent training in an environment with embedded physics simulation:

```fixy
vigil rl_with_physics(...)
  with Bg, IO, Network, Compute(Recipe::ORDERED), Async;
  topology Auto;
= 
  let world = init_physics_world();
  let policy = init_policy_network();
  for episode in 0..num_episodes;
    let trajectory = run_episode(world, policy);
    let updated_policy = train_step(policy, trajectory);
    policy = updated_policy;
    if episode % checkpoint_interval == 0;
      save_state((world, policy), epoch: episode);
    end if;
  end for;
end vigil;

stage run_episode(world: ref PhysicsWorld; policy: ref PolicyNetwork) : Trajectory
  with Compute(Recipe::ORDERED), Alloc;
  requires Capability::Cpu(Cores >= 8), Capability::GpuTensorCore;
= 
  let mut state = world.reset();
  let mut trajectory = empty_trajectory();
  for step in 0..max_steps;
    let action = policy.forward(state);     # GPU compute
    state = world.step(state, action);     # CPU physics simulation
    trajectory = trajectory.append(state, action);
    if state.is_terminal() then break end if;
  end for;
  return trajectory;
end stage;

stage train_step(policy: ref PolicyNetwork; trajectory: Trajectory) : PolicyNetwork
  with Compute(Recipe::BITEXACT_TC), Alloc, Async;
  requires Capability::GpuTensorCore;
= 
  let advantages = compute_advantages(trajectory);
  let loss_grads = policy.backward(advantages);
  let new_weights = policy.weights - lr * loss_grads;
  return policy.with_weights(new_weights);
end stage;
```

The `run_episode` stage interleaves GPU forward passes (the policy network) with CPU physics simulation (the environment); the `train_step` stage runs entirely on the GPU. The vigil composes them; Canopy assigns each stage to nodes with matching capabilities; the data plane routes trajectories between stages. The same substrate handles both the ML-style `train_step` and the HPC-style `world.step`; the only difference is the recipe declaration and the capability requirements.

**Data engineering workloads.** A streaming aggregation pipeline:

```fixy
vigil streaming_etl(...)
  with Bg, IO, Network, Async;
= 
  let raw = read_kafka(topic: "events", partitions: all);
  let parsed = raw |> map(parse_json);
  let validated = parsed |> filter(.is_valid);
  let aggregated = validated |> group_by(.user_id) |> agg(count, sum, p99);
  aggregated |> write_clickhouse(table: "metrics");
end vigil;
```

The vigil reads from Kafka, parses, filters, aggregates, writes to ClickHouse. Each stage runs on nodes with the appropriate capabilities (Kafka readers on nodes with `KafkaConsume`, ClickHouse writers on nodes with `ClickHouseWrite`). The substrate handles the data plane, the partitioning, the failure recovery. No ML primitives are involved; the substrate is general enough to handle the workload natively.

**The user-defined-op extension.** Users define new ops via `fn` declarations annotated with `@[kernel]`. The annotation triggers Forge's kernel-template-matching to recognize the function as a candidate for kernel emission. Mimic's per-vendor backends emit per-vendor code for the function. The cross-vendor numerics CI matrix verifies the function's results across vendors. This mechanism allows users to add domain-specific compute operations that are not in the stdlib without compromising the substrate's guarantees.

**The CKernel taxonomy as a stdlib starting point.** The 146-op CKernel taxonomy from the current Crucible (which moves to `fixy/ir/CKernel.h` per the architectural reorganization) is a starting point for the stdlib; it covers ML's needs reasonably comprehensively. HPC and other workloads add their own ops via the user-defined-op mechanism; over time, popular user-defined ops migrate into the stdlib if they have broad applicability. The taxonomy is not closed; new ops are added at version boundaries per the contract diff mechanism.

---

## 17. The runtime tracer and the frontends path

The runtime tracer (the TraceRing / MetaLog / BackgroundThread / IterationDetector machinery) lives in `fixy/runtime/` because it is the mechanism by which non-Fixy frontends emit IR001. It is invoked at runtime by Vessel-PyTorch, Vessel-JAX, and any future framework adapter that captures dispatched ops into Fixy's IR.

**The frontend pattern.** A frontend adapter intercepts the framework's dispatch layer and translates each dispatched op into an entry in TraceRing. The entry contains the op's schema (a hash identifying the operation kind), input shapes (a shape hash), input data pointers (for the actual tensor data), and any scalar arguments. The background thread drains TraceRing, builds a TraceGraph from the entries, detects iteration boundaries via IterationDetector, and constructs RegionNodes (content-addressed compilation units) from the graph. The RegionNodes are IR001; Forge consumes them identically to AOT-compiled IR001 from `.fixy` source.

**Vessel-PyTorch.** The PyTorch adapter registers with `DispatchKey::Crucible`, intercepts every ATen op, translates to TraceRing entries, returns shadow tensor handles to the user's Python code. The shadow handles satisfy the PyTorch tensor interface (shape, dtype, device, strides, requires_grad) without holding actual storage; the user's loop builds an autograd graph but never runs forward computation. At a sync point (`tensor.item()`, `print(tensor)`, `trainer.step()` return, etc.), the runtime forces compilation and execution: the BackgroundThread builds RegionNodes from the trace, Forge compiles, Mimic emits per-vendor binaries, the runtime executes, the results materialize into the shadow handles, control returns to Python.

**Vessel-JAX.** The JAX adapter follows the same pattern using JAX's `jax.core.Tracer` infrastructure. A `CrucibleTracer` subclass intercepts JAX primitives, translates to TraceRing entries, returns tracer handles for the user's traced computation. At `jax.jit` invocation, the runtime forces compilation as in the PyTorch case.

**Vessel-native.** The native adapter is for Python users who want to use Fixy without going through PyTorch or JAX. The user imports `crucible` and uses `crucible::tensor::*` constructors and `crucible::ops::*` operations directly; the constructors and operations are thin wrappers that translate to TraceRing entries the same way the framework adapters do. The native adapter is the path for users who want PyTorch-like ergonomics without PyTorch's dispatch overhead.

**`.fixy` source skips the tracer.** A `.fixy` source file is compiled AOT by `fixyc`. The compiler emits IR001 directly from the typed AST without going through TraceRing. This is the primary path for production code; the tracer is for capturing existing Python codebases that have not been rewritten in Fixy. Both paths produce identical IR001 from identical inputs (the user's algorithm), so the downstream compilation and execution are identical.

**Tracer overhead.** TraceRing is the mock-tensor capture path's per-op cost: approximately 5-20ns per op on the dev hardware (per CRUCIBLE.md §3.3 and the bench suite). This is dominated by the SPSC ring write and the metadata copy. The cost is the same regardless of whether the op is a small elementwise operation or a large matrix multiplication; the actual computation does not happen until the sync point. For a 1000-op training step with one sync point, the tracer's overhead is approximately 5-20μs total; the rest of the step is the actual computation, which runs at hardware speed.

**Recovery and replay.** The TraceRing entries are persisted to Cipher; on recovery from a Keeper failure, the runtime replays the trace from the last checkpoint to reconstruct the program state. The replay is deterministic given the same trace and the same compiled binaries; this is the basis for Crucible's replay-determinism invariant (CRUCIBLE.md §10).

---

## 18. Forge inside Fixy

Forge's 12-phase pipeline lives in `fixy/forge/`. Each phase has a dedicated directory containing the phase's implementation, its sub-passes, its tests, and its diagnostics. The phase ordering, budget, and semantics are documented in FORGE.md and unchanged by the move into Fixy.

**Phase directories.**

- `fixy/forge/PhaseA/` — INGEST. Shape specialization, dtype propagation, algebraic simplification, constant folding, dead code elimination, canonical ordering, subgraph CSE.
- `fixy/forge/PhaseB/` — ANALYZE. Dominator trees, liveness, use-def chains, dependence graphs, cost estimation, persistence analysis, communication analysis, uniformity analysis.
- `fixy/forge/PhaseC/` — REWRITE. 10 sub-passes: algebraic simplification, constant folding, strength reduction, copy propagation, GVN, LICM, IV narrowing, barrier elimination, DCE, peephole fusion.
- `fixy/forge/PhaseD/` — FUSE. Build fusion lattice, cost fusion groups, solve fusion DP, solve fusion ILP (gated at O3+), multi-layer fuse, emit fused regions.
- `fixy/forge/PhaseE/` — LOWER_TO_KERNELS. Kernel template matching, layout commit, recipe selection, tile spec seed, emit KernelGraph, verify IR002.
- `fixy/forge/PhaseF/` — TILE. Propose tiles via Mimic, rank by fast cost, fuse tile constraints, shape range bucket, commit tile pareto.
- `fixy/forge/PhaseG/` — MEMPLAN. Lifetime intervals, alias detection, offset assign, checkpoint decide, OOM check, deterministic plan.
- `fixy/forge/PhaseH/` — COMPILE. Dispatch to Mimic per kernel, collect results.
- `fixy/forge/PhaseI/` — SCHEDULE. Stream assign, barrier insert, launch order, graph capture hint.
- `fixy/forge/PhaseJ/` — EMIT. Serialize plan, generate dispatch table, relocate slot pointers, commit to Cipher.
- `fixy/forge/PhaseK/` — DISTRIBUTE. Partition graph, insert collectives, optimize collectives, topology-aware routing.
- `fixy/forge/PhaseL/` — VALIDATE. Probe counters, compare to model, drift detect, regression detect, cache invalidate.

**Invocation from `fixyc`.** The Fixy compiler invokes Forge after lowering to IR001. The Forge driver (`fixy/forge/driver.cpp`) takes the IR001 KernelGraph, the abstract TargetCaps from Mimic's per-vendor backends, and the CompileConfig (which includes the workspace's optimization level, search mode, and deadline). The driver runs the 12 phases in sequence, with parallelism inside each phase where applicable (Phase H parallelizes per-kernel compilation across hardware threads).

**Per-phase budgets.** Each phase has a hard wall-clock budget (FORGE.md §5). When a phase exceeds 1.5× its budget, the driver aborts the compile attempt for the affected region, marks the region OPAQUE, and falls back to the Genesis Kernel Pack seed (FORGE.md §23.7) for that shape. The background thread retries compilation on the next opportunity. This prevents pathological inputs from blocking the compiler indefinitely.

**Per-phase verification.** Each phase exits through an IR verifier that checks structural invariants (FORGE.md §5). Verifier failures are bugs in the phase's implementation; they emit structured diagnostics with the IR state at the point of failure for debugging.

**Determinism.** Each phase is deterministic given its inputs and the abstract TargetCaps signature (FORGE.md §5). No hash-table-order iteration, no floating-point sums whose order depends on allocator state, no random number usage outside the explicitly-seeded Philox stream derived from the content hash. Same inputs produce same IR002 produce same IR003\* produce same compiled bytes.

**Integration with the cross-vendor CI.** Forge's Phase J emits a per-recipe per-vendor compilation manifest that the cross-vendor CI consumes. The CI runs each (kernel, recipe, vendor) tuple, executes on real hardware (or the per-vendor simulator for backends without hardware access), compares the outputs pairwise against the CPU oracle. Tolerance violations fail the build.

---

## 19. Mimic inside Fixy

Mimic's per-vendor backends live in `fixy/mimic/` with one subdirectory per vendor. The shared core (simulator, MAP-Elites driver, archive) lives in `fixy/mimic/core/`.

**Vendor directories.**

- `fixy/mimic/nv/` — NVIDIA. Hopper (sm_90, sm_90a), Blackwell (sm_100, sm_100a, sm_100f, sm_103, sm_103a, sm_103f, sm_120). SASS emitter, raw `/dev/nvidia*` ioctl runtime (no libcuda), MAP-Elites with Hopper-specific behavior axes (warp specialization, register allocation policy, smem swizzle), CUPTI-equivalent counter probes.
- `fixy/mimic/am/` — AMD. CDNA3 (gfx942), CDNA3+ (gfx950), RDNA3+, future generations. AMDGCN emitter, raw `/dev/kfd` ioctl runtime (no librocm), MAP-Elites with CDNA-specific behavior axes (MFMA shape, VGPR pressure), rocprof-equivalent counter probes.
- `fixy/mimic/apl/` — Apple. AGX (M-series GPUs), AMX (matrix coprocessor). Reverse-engineered AGX shader format (per Asahi Linux blueprint), AMX intrinsics (per Dougall Johnson's reverse-engineering work), IOKit Mach port runtime, simulator calibrated against M3 / M4 measurements.
- `fixy/mimic/tpu/` — Google TPU. v5p, v5e, v6e, v7. TPU executable emitter (per reverse-engineered libtpu format), raw `/dev/accel*` ioctl runtime (no libtpu), MAP-Elites with TPU-specific behavior axes (MXU utilization, VMEM pressure), PJRT profiler-equivalent counter probes.
- `fixy/mimic/trn/` — AWS Trainium. trn1, trn2, trn3. NeuronCore bytecode emitter (per AWS open driver), raw `/dev/neuron*` ioctl runtime (no libnrt), MAP-Elites with Trainium-specific behavior axes, neuron-profile-equivalent counter probes.
- `fixy/mimic/cpu/` — CPU reference. x86_64 (AVX-512, AVX2, SSE4.2), aarch64 (NEON, SVE2), riscv. Pure C++26 implementations, scalar FMA only (BITEXACT_STRICT), serves as the cross-vendor CI oracle.

**Per-vendor backend pattern.** Each vendor backend implements the five-function Mimic interface (FORGE.md §22):

- `mimic::<vendor>::fast_cost(KernelNode, TargetCaps) → Cycles` — fast cost estimation
- `mimic::<vendor>::propose_tiles(KernelNode, TargetCaps) → AbstractTile[]` — tile proposal
- `mimic::<vendor>::compile_kernel(KernelNode, TargetCaps, CompileConfig) → CompiledKernel` — full compilation
- `mimic::<vendor>::predict(CompiledKernel, TargetCaps, SimTier) → SimResult` — prediction for drift detection
- `mimic::<vendor>::probe_counters(CompiledKernel, TargetCaps) → Measurements` — hardware counter read

Forge invokes these five functions through the abstract `mimic::*` interface; the dispatch is by `caps.vendor_id`. Forge never includes a vendor header; the per-vendor backends include only their own headers plus the shared core.

**Per-vendor runtime libraries.** Each backend includes its own runtime library (`mimic/<vendor>/rt/`) wrapping the kernel driver ioctls directly. This replaces libcuda, librocm, libtpu, libnrt with first-party C++26 code. The runtime library handles device opening, memory allocation (raw ioctls into the device memory pool), kernel submission (raw doorbell writes), completion polling. The Crucible runtime invokes the per-vendor runtime through a thin abstract interface that dispatches by `caps.vendor_id`.

**Per-vendor collective libraries.** Each backend includes its own collective library (`mimic/<vendor>/comm/`) implementing the CNTP collective primitives (ring, tree, halving-doubling, hierarchical) using the vendor's transport primitives (NVLink, xGMI, ICI, NeuronLink, RoCE, IB). This replaces NCCL, RCCL, libnccom, hcoll with first-party C++26 code. The collective library supports the in-network offload plane (SHARP for IB, ICI aggregation for TPU, etc.) when the fabric supports it.

**MAP-Elites archive per vendor.** Each backend maintains a MAP-Elites archive of compiled kernel candidates per kernel template. The archive is content-addressed by the kernel template's hash and the chip's caps signature. The Cipher cold tier persists the archive across runs so that subsequent compilations warm-start from the previous run's best candidates. The archive is federation-shareable at the L2 cache level (vendor-family-specific, not chip-specific), so a kernel compiled on H100 may benefit from another site's MAP-Elites archive on H200 within the same vendor family.

**Calibration.** Each backend calibrates its three-tier simulator (fast / medium / accurate) against measured hardware via Meridian's microbenchmark harness. The calibration runs at Keeper startup and produces vendor-specific TargetCaps extensions that Forge consumes. Continuous calibration via Augur drift detection updates the calibration as hardware behavior shifts (thermal throttling, firmware updates, chip aging).

**Adding a new vendor.** A new vendor backend is a new subdirectory under `fixy/mimic/`. The backend implements the five-function Mimic interface, ships its own runtime library and collective library, calibrates against measured hardware, and integrates with the cross-vendor CI matrix. No changes to Forge or to the rest of Fixy are required. The new vendor becomes available to all Fixy programs through the existing capability-matching mechanism.

---

## 20. Stdlib organization

The Fixy stdlib lives in `fixy/stdlib/` as `.fixy` source files. The stdlib is the user-facing surface; users `open` stdlib modules to access primitives, types, and capabilities. The stdlib's stability contract is per-module-version, with the contract diff (CRUCIBLE.md §14.10) computing semver bumps on changes.

**Prelude.** `prelude.fixy` is auto-imported into every Fixy file. It declares the basic types (`int`, `nat`, `bool`, `string`, `unit`, `never`, `option`, `result`, `list`), the basic operations (arithmetic, comparison, logical), the foundational effect (`Tot`, `IO`, `Alloc`, `Read`, `Write`, `Async`, `Div`, `Ghost`), and the foundational dimensions (`Linear`, `Refined`, `Tagged`, `Secret`). The prelude is small (~200 lines) and stable.

**Std.Tensor.** Tensor types and operations. Declares `tensor<dtype, shape>` as a type constructor parameterized by the element type and the shape (a list of natural numbers, possibly symbolic). Operations: construction (`zeros`, `ones`, `rand_uniform`, `rand_normal`, `from_list`), elementwise (`+`, `-`, `*`, `/`, `relu`, `sigmoid`, `tanh`, `exp`, `log`), reductions (`sum`, `mean`, `max`, `min`, `argmax`, `argmin`), reshaping (`reshape`, `transpose`, `permute`, `squeeze`, `unsqueeze`), slicing (`slice`, `index`, `gather`), composition (`cat`, `stack`).

**Std.Kernel.** Kernel decoration. Declares the `@[kernel]` attribute that marks a function for Forge's kernel-template matching. The `@[kernel.attention]`, `@[kernel.norm]`, `@[kernel.reduce]`, etc. variants from CRUCIBLE.md §3.8 declare specific kernel families with their extension-point bodies (score_mod, mask_mod, normalize_fn, etc.).

**Std.Vigil.** Vigil syntax. Declares the `vigil` keyword for declaring deployable programs, the `stage` keyword for declaring pipeline stages, the `pipeline` keyword for declaring dataflow compositions, and the topology-declaration syntax (`topology Auto`, `topology explicit { ... }`).

**Std.Recipe.** Numerical recipe selection. Declares the four-tier recipe vocabulary (`UNORDERED`, `ORDERED`, `BITEXACT_TC`, `BITEXACT_STRICT`) plus the per-recipe configuration (accumulator dtype, reduction algorithm, rounding mode, scale policy, softmax recurrence). Provides the `with Recipe::*` clause for use in stage and kernel declarations.

**Std.Collective.** Communication patterns. Declares `all_reduce`, `all_gather`, `reduce_scatter`, `broadcast`, `all_to_all`, `send`, `recv`, `barrier`. Each operation accepts an algorithm parameter (`Algorithm::Ring`, `Algorithm::Tree`, etc.) and a topology parameter (`Topology::AugurFastest`, `Topology::Static(...)`); the defaults delegate to the runtime per §12.

**Std.Data.** Streaming, prefetch, channels. Declares `stream<T>` as a Cipher-backed channel type, `dataloader<T>(...)` as a streaming source from a URI, `prefetch(stream, depth)` to set the consumer prefetch depth, `tier(stream, Cipher::Tier)` to select the storage tier. Also declares the parsing primitives (`parse_json`, `parse_msgpack`, `parse_parquet`, `parse_arrow`) and the augmentation primitives.

**Std.HPC.** HPC primitives per §15. FFT, sparse linear algebra, dense linear algebra, MPI-style collectives, partitioning, parallel I/O.

**Std.Sim.** Simulation primitives per §15. Monte Carlo, FEM, FDTD, particles, CFD.

**Std.Math.** Scalar math. `sin`, `cos`, `tan`, `exp`, `log`, `pow`, `sqrt`, `abs`, `floor`, `ceil`, `round`. Plus `frac`, `decimal`, and the conversion primitives between numeric types (`widen`, `narrow`).

**Std.Logic.** Decidability and ordering. Declares the `Decidable<P>` type class, the standard `Eq` and `Ord` type classes, the `decide` tactic for proof-by-reflection (§10.7).

**Std.Cluster.** Capability declarations per §13. The capability vocabulary, the `requires` clause, the topology declaration syntax, the placement policies.

**Adding stdlib modules.** A new stdlib module is a new `.fixy` file under `fixy/stdlib/`. The module is included in the next Fixy release after CI validation. Backwards compatibility is enforced by the contract diff: a new version of a module must not break existing users; if it does, it requires a major version bump.

---

## 21. Build plan

The Fixy compiler and its surrounding ecosystem build out in phases.

**Phase 0: Substrate foundation (months 0–1).** Ship `safety/Fn.h` — the unified `Fn<...>` template aggregating the existing safety / effects / permissions / sessions / algebra wrappers under one Tier S/L/T/F/V dispatch. Ship `safety/CollisionCatalog.h` — the §6.8 collision rules ported as `ValidComposition<Fn>` concept gates with named diagnostics per the existing `safety/Diagnostic.h` framework. Ship `safety/DimensionTraits.h` — the SemiringGrade / LatticeGrade / TypestateGrade / FoundationalGrade / VersionedGrade concept families. Five worked examples in `examples/fn/` written directly in C++26 using `Fn<...>`: a custom kernel, a custom optimizer, a CNTP frame contract, a Forge phase, a Mimic backend hook. HS14-mandate negative-compile fixtures: ≥2 per `Fn` instantiation. Validates that the substrate works before any parser is built.

**Phase 1: Compiler skeleton (months 1–3).** `fixyc` parser, elaborator, codegen targeting the Phase 0 substrate. Lex / Parse / Elaborate phases with the structured diagnostic framework. Codegen for the basic constructs: `fn` declarations, `let` bindings, `if` / `match`, `with` clauses, `pre` / `post` clauses, `vigil` / `stage` / `pipeline`. CMake integration via `add_fixy_library(...)`. CI-mandate: codegen output for the five Phase 0 examples is byte-equivalent to the hand-written C++26.

**Phase 2: Diagnostic translator and LSP (months 3–4).** `fixy/compiler/diag/cpp_translate.cpp` — the C++ template error translator. `fixy-lsp` — the language server. `fixy-fmt` — the formatter. End-to-end Fixy program: a single `.fixy` file that compiles, runs on a single-node Crucible fleet, produces correct output. Hello-world equivalent.

**Phase 3: IR pipeline integration (months 4–6).** Move Crucible's IR001 machinery to `fixy/ir/` and `fixy/runtime/` per the architectural reorganization. Update Vessel-PyTorch to emit Fixy IR via the runtime tracer. Forge phase A through E land in `fixy/forge/` (consuming Fixy IR001). First end-to-end demo: an MLP forward pass written in Fixy compiles, runs on the CPU reference backend, produces numerically-identical output to a hand-written reference.

**Phase 4: First production backend (months 6–10).** Mimic-NV lands in `fixy/mimic/nv/` with raw NVIDIA ioctl runtime, SASS emitter for one kernel family (GEMM), MAP-Elites search with one behavior axis (warp specialization). Forge phases F through J land. First training run on H100: a small transformer trains via Fixy + Forge + Mimic-NV, produces correct results, achieves ≥50% MFU on simple kernels.

**Phase 5: Multi-vendor expansion (months 10–18).** Mimic-AM lands in `fixy/mimic/am/` with raw AMD KFD runtime, AMDGCN emitter. Mimic-CPU lands in `fixy/mimic/cpu/` as the reference oracle. Cross-vendor numerics CI matrix runs every (kernel, recipe, vendor) tuple. Mixed-vendor training demo: same model trains on H100 + MI300X cluster with bit-exact gradient agreement under BITEXACT_TC recipe.

**Phase 6: Distributed runtime substrate (months 12–18, parallel to Phase 5).** Crucible's distributed pieces land: Canopy mesh (SWIM gossip + Raft consensus), CNTP transport (AF_XDP + io_uring), Cipher three-tier persistence, Keeper daemon lifecycle, Augur drift detection, K8s operator + CrucibleCluster CRD. The substrate runs Fixy programs across multi-node fleets with capability-based stage assignment, content-addressed data plane, replay-deterministic recovery.

**Phase 7: Stdlib and HPC (months 15–24).** `Std.HPC` and `Std.Sim` modules land. First HPC demo: a finite-element solver written in Fixy runs on a 32-node CPU-only fleet, produces correct results. Mixed-workload demo: an RL agent with embedded physics simulation trains on a heterogeneous fleet (CPU for physics, GPU for policy network).

**Phase 8: Production hardening (months 18–24).** TPU and Trainium backends land in `fixy/mimic/tpu/` and `fixy/mimic/trn/`. Apple AGX backend lands in `fixy/mimic/apl/`. Full cross-vendor CI matrix across all five hardware families. Production-ready release: documented stability contract, version-pinned stdlib, contract-diff-checked changes.

**Phase 9: Ecosystem (months 24+).** JAX frontend lands in `fixy/frontends/jax/`. Fixy package manager (`fixy-pkg`) for distributing third-party libraries. Federation across organizations: shared L1 IR cache via S3-backed Cipher cold tier. Long-tail features: live binary upgrade during training, cross-region partitioning, federated learning patterns.

**Total compiler investment.** Approximately 30,000 lines of C++26 across the compiler (parser ~600 lines, elaborator ~4,000 lines, codegen ~2,500 lines, diagnostic translator ~1,500 lines, IR000 lowering ~1,500 lines, IR001 lowering ~1,200 lines, LSP ~2,000 lines, plus per-phase Forge work ~10,000 lines and per-vendor Mimic work ~15,000 lines). Stdlib approximately 8,000 lines of `.fixy` source.

**Resourcing.** A team of 3 engineers can execute Phases 0–4 in 12 months (one engineer on the compiler, one on the substrate and runtime, one on the per-vendor backends). Phases 5–7 add a 4th engineer for HPC / stdlib work. Phase 8 adds 2 more engineers for additional vendor backends. Total team-months at Phase 8 completion: ~120 engineer-months for the full multi-vendor production-ready system.

---

## 22. Open questions deferred

Questions not resolved in this document that need resolution before or during implementation.

**Macro hygiene.** The `@[meta]` reflection-driven code generation needs a hygiene model to prevent name capture between meta functions and the surrounding code. Two options under consideration: full hygiene per Racket's syntax objects (more complex implementation, fewer surprises) or unhygienic with explicit gensym (simpler, more debugging burden). Decision needed before the meta function family lands.

**Module versioning.** The contract diff (per CRUCIBLE.md §14.10) computes semver bumps on changes to a module's public surface. The exact rules for what counts as a breaking change versus an additive change are not yet specified for Fixy's specific dimension space. Adding a new dimension to a function signature is breaking; widening an effect row is breaking; tightening a refinement is breaking; loosening a refinement may or may not be breaking depending on caller context.

**Cross-language FFI.** Fixy programs may need to call C / C++ libraries (e.g., a research library not yet ported to Fixy). The `extern "C"` form provides a basic interop but does not carry dimensional information; the C function appears as an unrefined `with IO, Alloc` operation. A more sophisticated FFI that allows the user to declare dimensional contracts on imported C functions and have the compiler verify them at runtime is possible but not yet specified.

**Debugger integration.** A Fixy program compiled in debug profile should be debuggable by gdb / lldb with source-level fidelity. The `#line` directives the codegen emits provide basic file:line mapping, but variable inspection requires the debugger to understand the substrate's wrapper types. A `safety/debug_pretty_printer.py` (analogous to libstdc++'s pretty printers) would resolve this. Not yet specified.

**Performance tuning interface.** When a vigil's measured performance falls short of the expected MFU, the user needs an interface to investigate. Augur emits structured drift events; a `crucible vigil profile <name>` command should aggregate these into actionable recommendations. The recommendation format and the command-line UX are not yet specified.

**Multi-tenancy.** A single Crucible fleet may host multiple vigils for multiple users. Resource isolation, capability quotas, and cross-tenant data flow restrictions are not yet specified. The current model assumes one vigil per fleet; production deployments will need multi-tenancy.

**Live evolution.** A vigil running in production may need to evolve over time without restart: model architecture changes, new stages added, capability requirements updated. The existing live-reshard mechanism (CRUCIBLE.md §12) handles topology changes; live updates to the vigil's structure are not yet specified.

**Reproducibility across compiler versions.** The L1 / L2 / L3 Cipher cache is keyed on (content_hash, chip_caps_signature). The content hash includes the source's IR001 form; a change to the compiler that changes the IR001 form invalidates the cache even if the source is unchanged. The version-skew handling (deprecation, parallel cache by compiler version, automatic migration) is not yet specified.

**Bootstrapping Fixy in Fixy.** The compiler is currently planned in C++26. Eventually, the compiler may be rewritten in Fixy itself (the language compiles itself). The bootstrap model (cross-compilation from a previous Fixy compiler, hand-translation of compiler internals, etc.) is not yet specified.

---

## 23. Glossary

**Augur.** Crucible's monitoring and recommendation subsystem. Continuously samples per-kernel and per-collective measurements, compares to predicted values, triggers recompiles or repartitions on drift. See CRUCIBLE.md §13.

**Canopy.** Crucible's fleet mesh. SWIM gossip plus Raft-scoped consensus, no master node, dynamic membership. See CRUCIBLE.md §7.

**Capability.** An advertised property of a node (a piece of hardware, a credential, a network interface) that a stage may require. Canopy intersects per-stage requirements with per-node advertisements to compute the assignment.

**Cipher.** Crucible's three-tier content-addressed persistence (hot RAM / warm NVMe / cold S3). See CRUCIBLE.md §9.

**Cluster.** A deployment unit comprising a fleet manifest plus a Fixy artifact. Managed by the Crucible operator on K8s, by SLURM via srun, or by systemd on bare metal.

**CNTP.** Crucible Native Transport Protocol. Bare-metal transport over AF_XDP and io_uring with no vendor library dependencies. See CRUCIBLE.md §5.

**Codegen.** The Fixy compiler phase that emits C++26 source from the typed IR. See §4.

**Collision.** A composition of dimensions that the type system rejects on soundness grounds. Each collision has a named error code and a corresponding entry in §6.8 of the FX semantics.

**ContentHash.** A deterministic 64-bit hash over the IR001 representation of a value or function. The L1 / L2 / L3 Cipher caches are keyed on content hashes.

**Dimension.** One axis of the Fixy type system's grade vector. Approximately 22 dimensions total: usage, effect, security, lifetime, provenance, trust, observability, complexity, precision, space, overflow, FP order, mutation, reentrancy, size, version, staleness, representation, protocol, type, refinement, clock domain (when present).

**Elaborator.** The Fixy compiler phase that performs bidirectional type checking and dimension resolution. See §4.

**Fixy.** The language. The compiler. The toolchain.

**fixyc.** The Fixy compiler binary.

**fixy-lsp.** The Fixy language server.

**fixy-fmt.** The Fixy formatter.

**Forge.** The IR optimization pipeline. 12 phases from INGEST through VALIDATE. Lives in `fixy/forge/`. See FORGE.md.

**Frontend.** A language adapter that produces Fixy IR. Vessel-PyTorch, Vessel-JAX, native `.fixy`. Each frontend lives in `fixy/frontends/<name>/`.

**Gossip.** SWIM-based membership protocol. See CRUCIBLE.md §5.2.

**Guts.** Compiler and runtime implementation that users do not reference directly. Includes `fixy/ir/`, `fixy/runtime/`, `fixy/forge/`, `fixy/mimic/`, `fixy/frontends/`. Refactorable without breaking user code.

**IR000.** High-level dataflow IR. The Fixy compiler's intermediate representation between the typed AST and IR001. Preserves control flow.

**IR001.** Typed kernel-graph IR. The boundary between the Fixy compiler and Forge. Lives in `fixy/ir/`.

**IR002.** Forge's portable kernel IR. Vendor-neutral. Each KernelNode has a kind, attribute struct, recipe, tile spec, layout commitment.

**IR003\*.** Per-vendor machine IR. One per Mimic backend.

**Keeper.** Per-Relay daemon. Lifecycle and health monitoring. See CRUCIBLE.md §14.

**KernelKind.** One of 22 kernel families that Forge recognizes (GEMM, ATTENTION, NORM, etc.). Plus user-defined ops via `@[kernel]` annotation.

**Layer (in Fixy IR).** A complete pipeline stage that compiles to one or more kernels. May contain multiple operations that fuse together at IR002 lowering.

**Manifest.** A YAML / TOML / JSON file declaring per-node capabilities for a Crucible fleet. Consumed by Canopy at deploy time.

**Meridian.** Crucible's startup calibration subsystem. Probes hardware caps, builds N×N latency / bandwidth matrix. See CRUCIBLE.md §15.

**Mimic.** Per-vendor backend framework. Lives in `fixy/mimic/`. See MIMIC.md.

**Mock tensor.** A `CrucibleTensorImpl` returned from intercepted ops in the Vessel frontends. Carries metadata only; no real storage until materialization.

**NumericalRecipe.** A pinned set of algorithmic choices (accumulator dtype, reduction algorithm, rounding mode, scale policy, softmax recurrence) that determines numerical behavior across vendors. See FORGE.md §19.

**`.fixy-pkg`.** Content-addressed deployable artifact produced by `fixyc compile`. Contains compiled binaries, IR snapshots, capability manifest, default fleet config, Cipher seeds.

**Phase.** A unit of compilation in Forge. 12 phases total from INGEST through VALIDATE. Each phase has a budget and a verification gate.

**Pipeline.** A composition of stages in a Fixy vigil. Declares the dataflow.

**Recipe.** Numerical recipe per the four-tier vocabulary (`UNORDERED`, `ORDERED`, `BITEXACT_TC`, `BITEXACT_STRICT`). See FORGE.md §19.

**Reflection.** Compile-time introspection of types and AST. Provided by C++26 P2996 (consumed by the substrate) and by Fixy's `meta::*` family (provided to user `@[meta]` functions). See §5.

**Region (RegionNode).** Content-addressed compilation unit. The boundary at which Forge invokes Mimic per-vendor backends. See FORGE.md §11.

**Sketch mode.** A build profile that infers aggressively, treats proof obligations as runtime assertions, accepts incomplete code. For exploration and prototyping.

**Stage.** A unit of computation in a Fixy pipeline. Declares its capability requirements, its CPU and accelerator needs, its effects, its cost.

**Stdlib.** The user-facing `.fixy` modules under `fixy/stdlib/`. Stable surface with semver versioning.

**Substrate.** The C++26 wrapper layer that the Fixy codegen targets. Includes `safety/`, `effects/`, `permissions/`, `sessions/`, `algebra/`, `concurrent/`, `handles/`, `bridges/`. Lives in `crucible/`.

**TargetCaps.** Abstract per-chip capability descriptor. Forge reads abstract fields; Mimic backends extend with vendor-specific fields. See FORGE.md §21.

**TileSpec.** Per-kernel tile shape and resource budget. Mimic per-vendor backends propose tiles via `propose_tiles`; Forge selects a pareto set via `rank_by_fast_cost`. See FORGE.md §11.

**TraceRing.** SPSC ring buffer used by Vessel frontends to emit dispatched ops to the background thread. Lives in `fixy/runtime/`.

**Trust level.** One of `External`, `Sorry`, `Tested`, `Verified`. Propagates through the call graph as the minimum. Release builds require Verified on every reachable function.

**Vessel.** A frontend adapter for an external framework (PyTorch, JAX). Translates the framework's dispatch into Fixy IR via the runtime tracer.

**Vigil.** A typed long-lived program with state. Declared in `.fixy` source via the `vigil` keyword. Compiles to a deployable artifact that Crucible runs on a fleet.

**Workspace.** A Fixy project rooted at a `fixy.toml` file. Contains `src/`, `tests/`, `benches/`, `examples/` directories. Built via `fixyc build`.

---

---

## 24. Formal specifications

This section is the formal-specification appendix. It catalogs the type-level surface that the Fixy DSL exposes: the 18-dimension grade vector, the §6.8 collision catalog ported as `ValidComposition<F>` concept gates, the trust algebra, the effect lattice, the mode (usage) semiring, the seven session safety levels, the machine algebra, the contract grammar, and the FX-inheritance map. The full surface grammar lives in `fixy_grammar.md`; the per-construct elaboration rules in `fixy_semantics.md`. This appendix lists what the substrate enforces — not what the documentation aspires to.

### 24.1 The 20-dimension grade vector

Every Fixy binding carries a 20-element grade vector. Each dimension is a graded algebraic structure (semiring, lattice, typestate, foundational, or versioned) checked simultaneously by one C++26 concept gate per the Tier S/L/T/F/V dispatch. The 20 are inherited from FX's 22-dim catalog with two principled drops:

- **dim 12 Clock Domain** — Crucible does not synthesize Verilog, so per-clock signal tracking is dead weight.
- **dim 17 FP Order** — `NumericalRecipe` pinning at the Mimic-emit layer already provides equivalent guarantees: `Recipe::UNORDERED` permits arbitrary reorder, `Recipe::ORDERED` constrains to a declared topology, `Recipe::BITEXACT_TC` and `Recipe::BITEXACT_STRICT` fully pin the FP-op sequence. A separate FP-order dimension would be redundant with recipe.

Wall-clock dimensions (Latency, Energy, Power, WallClock, BitsTransferred) are deliberately NOT in the catalog — the compiler cannot prove physical bounds, and annotation-only dimensions at this layer create false guarantees.

```
PROVABLE BY THE COMPILER (Tier S/L/T)

 #  Dimension        Tier  Tracks                         Default        Grant
 1  Type             F     what kind of data              required       (always)
 2  Refinement       F     what constraints hold          none           pre / post / { pred }
 3  Usage            S     consumption count              linear         affine / @[copy] / ghost
 4  Effect           S     side effects                   Tot            with IO / Alloc / Bg / ...
 5  Security         S     observation level              classified     unclassified / declassify
 6  Protocol         T     message sequence               none           session type
 7  Lifetime         S     value validity                 explicit       <r: region>, ref(r) T
 8  Provenance       S     data origin                    External       source("x") / sanitize
 9  Trust            S     verification status            Verified       sorry / axiom / with Div pull down
10  Representation   L     memory layout                  opaque         repr(C) / repr(packed)
11  Observability    S     trace reveals value            opaque (only   transparent (grant) — checked
                            checked under `with CT`)                    only inside `with CT` regions

PROVABLE BOUNDS (must state or mark unbounded)

13  Complexity       S     declared op count              must state     cost O(n)
14  Precision        S     FP error bound                 exact          f32 / f64 / Higham bound
15  Space            S     allocation bound               zero (stack)   with Alloc(strategy, bound)
16  Overflow         S     integer overflow behavior      arbitrary      with overflow(wrap|trap|sat)

STRUCTURAL PROPERTIES

18  Mutation         S     mutation permitted             immutable      ref mut / ref append
19  Reentrancy       S     self-call permitted            non-reentrant  rec / with Reentrant
20  Size             S     codata observation depth       required       sized s; / Productive

EVOLUTION

21  Version          V     code identity across revisions implicit v1    @[version(N)] + refines

ASYNC ADMISSION

22  Staleness        S     admission delay tolerance      fresh (τ=0)    with Stale(tau_max)
```

Notes on default semantics:

- **Dim 9 Trust default = Verified (top of lattice).** Trust propagates as `min(...)` across the call-graph closure plus body content. Default top means a function is Verified-trusted unless its body contains a construct that pulls trust down: `sorry` → Sorry, `axiom name(...)` → Assumed, `with Div` rec fn → Sorry, `extern "C"` → External. Release builds enforce `trust(f) ≥ Verified` for every reachable `f` except those marked `@[trust_assumed("rationale")]`. Full algebra in §24.3.

- **Dim 11 Observability checked only under `with CT`.** Unlike other dimensions which are checked at every binding, Observability is dormant unless the function's effect row contains `CT`. Inside a `with CT` region, all values must be opaque (no transparent grant); collisions E044 (CT × Async), I003 (CT × Fail on secret), S010 (Staleness × CT) fire there. Outside `with CT`, the dimension is not enforced and carries no runtime cost.

- **Dim 13 Complexity is signature-level cost propagation, not body inference.** The compiler verifies that called functions' declared costs compose under the bound; it does NOT infer asymptotic complexity from function bodies. Functions without a cost annotation must declare `unbounded`. The discipline matches effect rows: declaration on signatures + composition through the call graph, no body analysis.

- **Dim 14 Precision discharged by recipe pinning OR Higham analysis.** The default discharge is recipe pinning (BITEXACT_TC ≤ 1 ULP, BITEXACT_STRICT byte-equal); for users who need explicit FP-error tracking beyond what recipes provide, `Std.Numerics::FpError<bound>` exposes Higham condition-number-aware bounds (Higham §3.3) as opt-in annotations. Both paths discharge the dimension; the recipe path is the default since most production code uses it.

- **Dim 22 Staleness is a logical step counter, not wall-clock.** The compiler discharges admission obligations like `η · λ_max · τ ≤ critical` via dependent-grade SMT on the `verify` preset, not by measuring time. The producer and consumer agree on the τ delta as a logical event count.

Tier dispatch:

| Tier | Shape | Dimensions | Kernel dispatch |
|---|---|---|---|
| S | Commutative semiring (par=+, seq=*, 0 annihilator) | 15 dimensions: Usage(3), Effect(4), Security(5), Lifetime(7), Provenance(8), Trust(9), Observability(11), Complexity(13), Precision(14), Space(15), Overflow(16), Mutation(18), Reentrancy(19), Size(20), Staleness(22) | App/Let/If/Lam rules per §6.2 unchanged |
| L | Lattice with validity (par=join, seq=meet, valid_D check) | 1 dimension: Representation(10) | App/Let/If/Lam plus valid_D check at every par/seq |
| T | Typestate (transitions on state; no par/seq) | 1 dimension: Protocol(6) | Session transition rules dispatch (§24.6) |
| F | Foundational (bidirectional elaboration + concept gates) | 2 dimensions: Type(1), Refinement(2) | Standard C++26 type checking + P2900R14 contracts |
| V | Versioned (consistency check at each site) | 1 dimension: Version(21) | Refines / migration adapter resolution per §15 |

Total: 15 + 1 + 1 + 2 + 1 = **20 dimensions**.

The Fn template aggregates all 20 with EBO-collapsed defaults:

```cpp
template <
    typename Type,                                          // Tier F: dim 1
    typename Refinement   = pred::True,                     // Tier F: dim 2
    UsageMode Usage       = UsageMode::Linear,              // Tier S: dim 3
    typename EffectRow    = effects::Row<>,                 // Tier S: dim 4 (Tot)
    SecLevel Security     = SecLevel::Classified,           // Tier S: dim 5
    typename Protocol     = proto::None,                    // Tier T: dim 6
    typename Lifetime     = lifetime::Static,               // Tier S: dim 7
    typename Source       = source::Internal,               // Tier S: dim 8
    TrustLevel Trust      = TrustLevel::Verified,           // Tier S: dim 9 (top of lattice)
    ReprKind Repr         = ReprKind::Opaque,               // Tier L: dim 10
    /* dim 11 Observability — derived from EffectRow: enforced iff effects::CT ∈ row */
    typename Cost         = cost::Unstated,                  // Tier S: dim 13
    typename Precision    = precision::Exact,                // Tier S: dim 14
    typename Space        = space::Zero,                     // Tier S: dim 15
    OverflowMode Overflow = Overflow::Trap,                  // Tier S: dim 16
    MutationMode Mut      = MutationMode::Immutable,         // Tier S: dim 18
    ReentrancyMode Reent  = ReentrancyMode::NonReentrant,    // Tier S: dim 19
    typename Size         = size::Unstated,                  // Tier S: dim 20
    uint32_t Version      = 1,                                // Tier V: dim 21
    typename Staleness    = stale::Fresh                      // Tier S: dim 22
>
struct Fn requires ValidComposition<...>;
```

A function with all defaults compiles to `sizeof(Fn<int>) == sizeof(int)` — every default-grade dimension EBO-collapses (regime-1 per `algebra/Graded.h`). Any function emitted by the Fixy transpiler inherits this size discipline; the substrate enforces it via `static_assert(sizeof(Fn<...>) == expected)` at every `mint_fn` site.

### 24.2 The §6.8 collision catalog — 12 ValidComposition concept gates

The §6.8 collision catalog enumerates dimension compositions the kernel rejects on soundness grounds. These are not ad-hoc rules but missing edges in the mode theory. Each rule is a `ValidComposition<F>` concept-failure with a named diagnostic (per `safety/Diagnostic.h` FOUND-E01 pattern). The diagnostic cites the rule and suggests remediation.

```
Code  Composition                          Reason                             Suggested fix
────  ────────────────────────────────     ──────────────────────────────     ──────────────────────────────
I002  classified × Fail(E)                 error payload may carry secret;    declare Fail(secret E)
                                            catch arm logging the error
                                            leaks classification
L002  borrow × Async                       borrow lifetime cannot bridge      scope borrow before await,
                                            await suspension                   or capture by value (@[copy])
E044  CT × Async                           async scheduling introduces        wrap synchronous CT core in
                                            timing variation that defeats     async at the boundary
                                            constant-time guarantee
I003  CT × Fail on secret                  fail(e) on secret-dependent        compute via ct_select first,
                                            condition exposes the branch       then fail outside secret region
                                            taken via control flow
M012  monotonic × concurrent               concurrent monotonic update        use atomic<T> or declare
                                            requires CAS or commutative       concurrency lock_free
                                            merge to be race-free
P002  ghost × runtime                      ghost values are erased at         move computation out of
                                            codegen and have no runtime        ghost context, or move
                                            representation                     conditional into ghost
I004  classified × async × session         classified value over async        synchronous CT region at
                                            session may leak via timing       send, OR explicit declassify
N002  decimal × overflow(wrap)             wrap is meaningless for exact      use trap, saturate, or widen
                                            decimal types (IEEE 754-2008
                                            decimal defines no wrap)
L003  borrow × spawn (unscoped)            unscoped spawn may outlive         use task_group / permission_fork
                                            captured borrows                   for scoped spawn, or move
                                                                              ownership into closure
M011  linear × Fail (cleanup)              linear value live at fail site     register defer/errdefer for
                                            without registered cleanup        every linear binding live at
                                            may leak                           every Fail site
S010  Staleness × CT                       runtime freshness check defeats    drop CT, or tighten admission
                                            constant-time guarantee            policy to reject any τ > 0
S011  Capability × Replay                  ephemeral capability (fd, GPU      declare @[replay_stable] or use
                                            handle) does not survive replay   content-addressed handle
```

The 22-dim grade vector has C(22, 2) = 231 pairs and C(22, 3) = 1540 triples; the catalog identifies the 12 that pose genuine soundness problems. The remaining compositions either reduce to existing infrastructure (lattice join, linearity, typestate) or are independent dimensions whose product is a straight vector check. Every catalog rule corresponds to a missing 2-cell in the mode theory — the absence of a cell IS the rejection.

```cpp
template <typename Fn>
concept ValidComposition = !(
    /*I002*/ (Fn::security == Classified && has_fail_with_secret_payload<Fn::effects>())
 || /*L002*/ (is_borrow<Fn::usage> && has<Fn::effects, effects::Async>())
 || /*E044*/ (Fn::const_time && has<Fn::effects, effects::Async>())
 || /*I003*/ (Fn::const_time && has_fail_on_secret_condition<Fn>())
 || /*M012*/ (is_monotonic<Fn::mut> && is_concurrent_context<Fn> && !is_atomic<Fn::repr>)
 || /*P002*/ (is_ghost<Fn::usage> && Fn::trust > TrustLevel::Sorry && has_runtime_branch<Fn>())
 || /*I004*/ (Fn::security == Classified && has<Fn::effects, effects::Async>()
                                          && has_session<Fn::protocol> && !Fn::const_time)
 || /*N002*/ (is_exact_decimal<Fn::type> && Fn::overflow == Overflow::Wrap)
 || /*L003*/ (has_borrow_capture<Fn> && has_unscoped_spawn<Fn>())
 || /*M011*/ (has_linear_at_fail_site_without_cleanup<Fn>())
 || /*S010*/ (Fn::staleness != stale::Fresh && Fn::const_time)
 || /*S011*/ (is_capability<Fn::usage> && needs_replay<Fn> && !is_replay_stable<Fn>())
);
```

The above `ValidComposition` is **schematic**. The actual implementation lives in `safety/CollisionCatalog.h` (planned, FOUND-E01 pattern) as a tree of small concept gates with named per-rule diagnostics rather than one monolithic disjunction. The helper traits (`is_borrow<>`, `has<>`, `has_fail_with_secret_payload<>`, `is_concurrent_context<>`, etc.) decompose into header-local concepts that the substrate's `safety/Diagnostic.h` (FOUND-E01, shipped) renders as Goal / Have / Gap / Suggestion structured errors at the offending source position.

Each concept fails with a named diagnostic via `safety/diag/CollisionCatalog.h`:

```cpp
template <typename Fn>
struct CollisionDiagnostic {
    static constexpr Category category = Category::DimensionComposition;
    static constexpr const char* rule_code() noexcept { /* I002, L002, ... */ }
    static constexpr const char* goal() noexcept;
    static constexpr const char* gap() noexcept;
    static constexpr const char* suggestion() noexcept;
    static constexpr const char* reference() noexcept;  // FX §6.8 anchor
};
```

### 24.3 Trust algebra

Trust (dim 9) is a five-element lattice with composition by minimum.

```
External < Assumed < Sorry < Tested < Verified

par(t1, t2)  =  min(t1, t2)
seq(t1, t2)  =  min(t1, t2)
```

**Default = Verified (top of lattice).** A function whose body contains nothing that pulls trust down has trust Verified. Specific constructs reduce trust:

```
trust(`sorry`)                            = Sorry      placeholder for unfinished proof
trust(`axiom name(...);`)                 = Assumed    stated mathematical assumption
trust(`with Div` rec fn)                  = Sorry      admits non-termination
trust(extern "C" fn)                      = External   FFI boundary
trust(@[trust_assumed("rationale")])     = Assumed    audited in --show-axioms
trust(@[trust_external_audited(...)])    = Tested     third-party audit recorded
```

For a function `f` with body `b`:

```
trust(f) = min(trust(b), min over symbols s used in b of trust(s))
```

Trust propagates as the minimum across the call-graph closure plus the function body's syntactic content. A function calling anything Sorry-trusted is itself at most Sorry.

Release builds enforce `trust(f) ≥ Verified` for every reachable `f` except those explicitly marked. `@[trust_assumed("rationale")]` permits Assumed if and only if the programmer documents a justification string surfaced by `fixyc --show-axioms`. `External` requires `@[trust_external_audited("auditor", "id")]` recording the third-party verification.

(Aside on FX framing: FX §1.1's dimension table reads "Default: sorry" for dim 9, which names the BINDING-LEVEL placeholder value the user can leave in code as a TODO marker. The COMPUTED trust of a function whose body uses none of `sorry`/`axiom`/`Div`/`extern` is Verified — there is nothing in the body to pull it down. Fixy makes the operational reading explicit: top of the lattice is the starting point; specific constructs reduce it.)

### 24.4 Effect lattice (built-in effects + capability tags)

Effects (dim 4) form a bounded join-semilattice under set union:

```
(Effects, ∪, Tot) is a bounded join-semilattice.

Tot ∪ e   = e                  identity
e ∪ e     = e                  idempotent
e1 ∪ e2   = e2 ∪ e1            commutative
(e1 ∪ e2) ∪ e3 = e1 ∪ (e2 ∪ e3) associative
Read ⊆ Write                    Write implies Read (built-in subsumption edge)
```

Built-in effect labels — split by provenance.

**Inherited from FX §9.4 kernel built-ins:**

```
Tot         pure, terminating (identity)
Div         may diverge
Ghost       erased at runtime
Exn         may abort via untyped panic
IO          general input/output
Alloc       may allocate heap memory
Read        may read from references / state
Write       may write (implies Read)
Async       may suspend / await
```

**Crucible-required additions (matching `effects/Capabilities.h`):**

```
Fail(E)     typed error path with closed error type E (per FX §4.9 form, promoted to first-class effect)
Block       may block synchronously
Bg          background-thread context (composes Alloc + IO + Block; CLAUDE.md context structs)
Init        startup-only context (CLAUDE.md context structs)
Test        test-harness context (CLAUDE.md context structs)
Network     network IO (subsumes IO with finer audit granularity)
Productive  codata productivity required (sized observation; FX §3.5 promoted)
CT          constant-time discipline marker (activates dim 11 Observability; FX §12.5 promoted)
```

**Crucible systems-programming extensions (matching FX §19.1 execution-context hierarchy):**

```
Passive     ordinary process context (any effect available)
Dispatch    softirq / tasklet context (subset of Passive)
SoftIRQ     soft-interrupt context (no sleeping, no mutex)
HardIRQ     hard-interrupt context (no sleeping, no mutex, only spinlocks)
MMIO        memory-mapped device IO
DMA         direct memory access (linear ownership; §24.7 systems)
Crypto      crypto primitive operations (composes with CT; FX §9.5 user-defined example promoted)
```

The execution-context hierarchy: `HardIRQ < SoftIRQ < Dispatch < Passive`. A function declared `with HardIRQ` cannot call functions requiring `with Passive` — the type system rejects sleeping in interrupt context. This maps Crucible's existing tier separation (Init / Bg / Hot) to FX's execution-context discipline.

Effect rules at every call site:

```
G ⊢ e : T [Tot]                    G ⊢ e : T [e1]    e1 ⊆ e2
─────────────────  (E-Pure)       ────────────────────────────  (E-Sub)
G ⊢ e : T [e]                      G ⊢ e : T [e2]

G ⊢ e1 : T1 [e1]    G ⊢ e2 : T2 [e2]
────────────────────────────────────────  (E-Seq)
G ⊢ e1; e2 : T2 [e1 ∪ e2]

G ⊢ f : T1 →[ef] T2 [e1]    G ⊢ x : T1 [e2]
─────────────────────────────────────────────  (E-App)
G ⊢ f(x) : T2 [e1 ∪ e2 ∪ ef]

G ⊢ body : T [<E | eff>]    handler covers E
──────────────────────────────────────────────  (E-Handle, optional / sketch only)
G ⊢ handle body ... end handle : S [eff]
```

Note: full algebraic effect handlers (E-Handle) are dropped from Fixy production paths — Crucible uses `std::expected` + RAII + Linear<T> destructors for error propagation rather than algebraic handlers. Sketch mode permits the handler form for prototyping.

Single-`Fail`-per-row rule (E045): `with Fail(E1), Fail(E2)` is rejected. The lattice rule `Fail(E1) ∪ Fail(E2) = Fail(E1 | E2)` still applies when composing across call sites, but every signature must name one `Fail(T)` with a single closed-union error type.

### 24.5 Mode (usage) semiring

Usage (dim 3) is the central example of a Tier S commutative semiring:

```
Addition (parallel use):       Multiplication (sequential use):

+ |  0    1    w               * |  0    1    w
──┼─────────────              ──┼─────────────
0 |  0    1    w               0 |  0    0    0
1 |  1    w    w               1 |  0    1    w
w |  w    w    w               w |  0    w    w
```

Grade `0` means absent (erased / ghost). Grade `1` means linear (used exactly once). Grade `w` means unrestricted (used any number of times). The `+` operation models parallel use: using a linear variable in both branches of an `if` yields `1 + 1 = w`, which is a type error for a linear binding.

The extended mode lattice with affine and relevant intermediates:

```
        w (unrestricted)
       / \
 affine   relevant
       \ /
        1 (linear)
        |
        0 (absent)
```

Surface mapping:

```
Fixy mode       Grade    Linear logic    Crucible substrate
──────────      ─────    ────────────    ────────────────────────
(default)       1        T @ 1           Linear<T>
affine          ≤ 1      T @ affine      Affine<T>  (planned; rare in Crucible)
@[copy]         w        T @ w           T (no wrapper; trivially copyable)
ref             w        box T @ w       Borrowed<const T, Source>
ref mut         1        ◇ T @ 1         Borrowed<T, Source>  (exclusive)
ghost           0        erased          (compile-time only; no runtime)
```

CSL fractional-permission extension (per CRUCIBLE.md §IX, `safety/Permission.h`):

```
type permission = Frac of p: rational { 0 < p ∧ p ≤ 1 } | Zero | Omega

Frac(p) + Frac(q) = Frac(p + q)   when p + q ≤ 1
Frac(p) + Frac(q) = CONFLICT       when p + q > 1
Zero + x = x
Omega + Omega = Omega
```

`Permission<Tag>` and `SharedPermission<Tag>` wrappers in Crucible's substrate mechanize this directly. The Wood-Atkey 2022 corrected Lam rule with context division (`G/p`) prevents linear bindings from being captured in unrestricted closures.

### 24.6 Session safety levels (7 levels)

The parametric safety predicate φ from SY19 admits seven canonical instantiations on the LTS derived from the typing context. All seven are decidable on finite-state Γ, and Fixy's Γ is finite by construction (bounded participants per §11.13, guarded recursion per §11.14, capacity-bounded queues per §11.15).

```
safe     νZ. (∀α: [α]Z)
         Every reachable communication event is well-typed.

df       νZ. (⟨⟩Z ∨ atEnd)
         Deadlock-free. From every reachable state, the protocol can
         either make further progress or is already at End.

term     μZ. atEnd ∨ (∀α: [α]Z)
         Every fair path terminates at End.

nterm    νZ. ¬atEnd ∧ ⟨⟩Z
         Never reaches End and always has a reduction.
         For long-running service loops.

live     νZ. [pendingIO]⟨α⟩ Z
         Every pending input or output eventually fires.

live+    live, subject to fair scheduling.
         Pending I/O fires given a fair scheduler.

live++   live, under any scheduling.
         Pending I/O fires regardless of scheduler, including
         adversarial. Required for crypto with timing guarantees.
```

Per-channel default: `safe ∧ df ∧ live+`. Per-session declarations override:

```
session async_grad_protocol<replay: replay_tag, n: nat>
  satisfies safe ∧ df ∧ live+ ∧ crash_safe(R={leader});
  ...
end session;
```

Crash-stop integration (BSYZ22 / BHYZ23) extends every level with reliability set R: `is_crash_safe(Γ, R)` is conjuncted into every session's chosen φ. Roles in R are assumed reliable; roles outside R may crash and produce typed Crash<p> events the protocol must handle.

Asynchronous subtyping ⊑_a uses bounded-depth SISO decomposition (default depth 64, overridable per-session via `@[subtype_depth(N)]`). General ⊑_a is undecidable; the bounded approximation is sound — accepted relations are sound, but some sound relations exceeding depth D are conservatively rejected (S017). Synchronous fallback ⩽ via Gay-Hole 2005 is always available.

### 24.7 Machine algebra (declaration + 6 composition operators + LTL properties)

A `machine` declaration defines states with typed data, transitions with guards and effects, and properties the compiler verifies. Maps to Crucible's `Machine<States>` substrate plus `Session<Proto>` for typestate transitions.

Declaration:

```fixy
machine M
  state S0;
  state S1 of { field: T };
  state S2 of { ... };

  transition t1 : S0 → S1
    requires precondition;
    ensures postcondition;
    with Effect1, Effect2;
    inverse t1_inv : S1 → S0
      ensures restoration_postcondition;
    on_fail mode;       // Recoverable | Critical | Atomic
    [other modifiers]

  initial S0;
  terminal S2;
  invariant some_invariant;
end machine;
```

Transition modifiers:

```
requires P              guard — transition enabled iff P holds
ensures Q               postcondition — Q holds after transition fires
with E                  effect row — transition performs effects in E
inverse t'              verified reverse — t; t' restores original state
timeout d → S           time-triggered fallback (logical ticks)
retry n strategy        repeated attempt with backoff
atomic                  all-or-nothing execution
idempotent(key: k)      safe to repeat (dedup by key)
commutative             order-independent concurrent firing
monotonic               forward-only in declared partial order
emits Event(data)       produces event for other machines
on_fail mode            Recoverable (stay) | Critical (goto Error) | Atomic (rollback)
```

Six composition operators:

```
Product            M = A * B                       parallel, independent
Synchronized       M = A *sync(events) B           parallel with sync points
Sequence           M = A >> B                      one then another
Choice             M = match k; ... end match     one of several
Loop               M = A *{ while cond }          repeat
Nest               M.outer { state X { machine M.inner ... } }   hierarchical
```

Properties via standard LTL operators from `Std.Ltl` (no new keywords; ordinary library functions over sized codata streams of states):

```
G(φ)        globally — φ holds in every reachable state
F(φ)        finally — φ holds in some reachable state
X(φ)        next — φ holds in the immediately next state
φ U ψ       strong until — φ holds until ψ becomes true
W(φ, ψ)     weak until = G(φ) ∨ (φ U ψ)
fair_leads_to(P, Q, fairness)   P → eventually Q under fairness assumption

assert G(state is Connected ⇒ F(state is Disconnected));
assert deadlock_free(self);
assert deterministic(self);
```

Discharge strategy depends on state-space shape:

```
finite states, no parameters             Vardi-Wolper Büchi automaton product
                                         (decidable, PSPACE in formula)
parameterized, bounded at comptime       comptime unrolling to each concrete bound
parameterized with @[cutoff(n)]          symmetry reduction (decidable mod cutoff proof)
infinite state, invariant supplied       user `assert always I;` + concept gate
infinite state, no invariant             rejected: M020 (property undecidable)
```

Refinement claim: `refinement Impl refines Spec via state_mapping;` discharges the obligation that every Impl-path maps to a valid Spec-path. Used in Crucible for: Forge IR001 → IR002 lowering refinement, Mimic IR002 → IR003* per-vendor lowering, Vigil mode-typed wrapper migration.

Bisimulation: `bisimulation name relates R: A → B → bool; initial: ...; step: ...; end bisimulation;` — discharges observational equivalence. Used for cross-vendor numerics CI: emitted output from each Mimic backend is bisimilar to the CPU oracle's output up to recipe-tier tolerance.

### 24.8 Contract grammar (versioning + migration + access mode + format bindings)

A `contract` governs how a type behaves when it crosses a boundary (network, persistence, MMIO, version N → N+1, module API surface). Maps to Crucible's CNTP frame format machinery + Cipher serialization + format bindings.

Declaration:

```fixy
contract Name for T

  version 1 { fields... };
  version 2 = T
    migration 1 → 2 { add F default D; rename F → G; alter F: T1 → T2; ... };
  version N = T
    migration (N-1) → N { ... };

  compatibility {
    v1 → v2 : backward;
    v2 → v3 : backward;
    v3 → v1 : not_compatible;
  };

  access field_a : read_only;
  access field_b : requires auth(Admin);
  access field_c : write_once;

  format json { field_a : "snake_case_name" as string; ... };  // FX §14.4
  format protobuf { field_a : 1 as string; ... };               // FX §14.4
  format wit;                                                    // FX §14.4.1 WebAssembly Component Model
  format cntp { wire_layout, byte_order, hash };                 // Crucible-specific addition for CNTP frames

  errors {
    InvalidField(string);
    VersionMismatch(expected: nat, got: nat);
    Unauthorized;
  };

  invariant field_a.length > 0;
  invariant field_b == Admin ⇒ field_a.ends_with("@company.com");

end contract;
```

Access mode algebra (generalizes across hardware registers, database columns, API fields, configuration):

```
ReadWrite        unrestricted
ReadOnly         writes rejected
WriteOnly        reads rejected (passwords, keys)
WriteOnce        write once, then read-only
AppendOnly       add only, never remove
Monotonic of order   value only increases in declared partial order
Guarded of pred  write requires predicate to hold
Unique           globally unique across instances
RuntimeConst     set at init, immutable after
HotReload        changeable at runtime without restart
Ephemeral        valid for one use, consumed on read
AutoIncrement    system-assigned (databases)
Deprecated of v  accessible but warns about removal
W1C  W1S  RC     hardware register modes (write-1-clear, write-1-set, read-clear)
```

Generated operations from one contract:

```
C.decode(fmt, raw)   : result(T, C.errors)        deserialize + validate
C.encode(fmt, val)   : bytes                       serialize
C.validate(val)      : result(T, C.errors)         check constraints
C.migrate(val, from, to) : result(T, error)       version transform
C.compatible(v1, v2) : compatibility               version compatibility check
C.project(fmt)       : schema                      external schema (OpenAPI, DDL, .proto)
```

Migration operations and their compatibility:

```
add F default D            backward compatible
remove F                   forward only
rename F → G               migration required
alter F : T1 → T2          migration required
add computed F = expr      backward compatible
reorder fields             backward compatible
```

The compiler proves each migration is total — every valid input maps to a valid output. Migration semantics: the contract author writes `migration 1 → 2 { ... }`; the compiler synthesizes `migrate_v1_to_v2 : v1 → v2` and discharges totality as a proof obligation. (FX's modal-univalence framing — transport along Wire-mode equivalence — is dropped from Fixy; the migration is just a verified total function.)

Automatic version computation (per FX §14.10): the compiler computes the semantic version of a new release from the contract diff between current source and previous published version:

```
Removed symbol                    MAJOR
Added symbol                      MINOR
requires strengthened             MAJOR
requires weakened                 MINOR
ensures strengthened              MINOR
ensures weakened                  MAJOR
Effect added to function          MAJOR
Effect removed from function      MINOR
Required field added              MAJOR
Optional field added              MINOR
```

Direction follows subtyping: contravariant on inputs (weaker pre is safe), covariant on outputs (stronger post is safe).

### 24.9 Decidability and discharge mechanism per dimension

For each dimension, what the compiler mechanically discharges, what the user must annotate, and where Z3 / measurement / runtime checks fill the gap. This is the operational story behind the substrate — the difference between "the type system claims X" and "X is actually checked at compile time".

```
Dim  Name              Discharge mechanism                          User obligation
───  ──────────────    ─────────────────────────────────────       ──────────────────────────
 1   Type              standard C++26 type checker                  declare types
 2   Refinement        P2900R14 contracts at boundary;              write pre/post + name aliases
                       [[assume]] hints inside; concept gates
                       for static refinements; Z3 on `verify`
                       preset for integer / Presburger boundary
                       obligations only
 3   Usage             Linear<T> deleted-copy; Permission<Tag>      use Linear / Borrowed / Permission
                       move-only; concept gates for ghost-vs-       at every linear binding
                       runtime
 4   Effect            effects::Row<Es...> + Subrow concept;        declare with-row on every
                       Computation<Row, T> carrier check at         public function
                       every call site
 5   Security          Secret<T> wrapper; declassify<Policy>()      wrap secret bindings;
                       grep-discoverable; concept gates for         name policies
                       I002, I003, I004 collisions
 6   Protocol          Session<Proto> typestate; per-state          declare session types;
                       handle type changes after each send/recv     use channel.send/recv API
 7   Lifetime          Borrowed<T, Source> + region tag;            carry region tags through
                       Wood-Atkey context division on closure       function signatures
                       capture
 8   Provenance        Tagged<T, source::*>; concept gate for       wrap external/sanitized inputs
                       trust-boundary crossings
 9   Trust             call-graph propagation as min;               use sorry/axiom/with Div
                       @[trust_assumed("rationale")] audit          deliberately; promote to
                       trail; release builds gate on Verified       verified via test/proof
10   Representation    repr(C, packed, aligned, big_endian);        opt in to specific repr where
                       static_assert(sizeof) at every layout site   FFI / wire format requires
11   Observability     ConstantTime<T> branch-free primitives;      use ct_select / ct_eq inside
                       concept gates for E044 (CT × Async),          `with CT` regions; checked
                       I003 (CT × Fail on secret), S010 (Stale × CT) only when CT is in effect row
13   Complexity        propagation of declared cost through call    declare cost O(...) on every
                       graph (same shape as effect rows); no body   public function or `unbounded`
                       inference
14   Precision         exact decimal types (decimal/decN);          opt in to f32/f64; declare
                       recipe pinning (BITEXACT_TC ≤ 1 ULP,         FP-error bound via
                       BITEXACT_STRICT byte-equal); Higham          Std.Numerics::FpError<bound>
                       condition-number tracking via opt-in         when recipe pinning is
                       Std.Numerics::FpError<bound>                  insufficient
15   Space             Alloc(Stack, bound: N) checked at            declare allocation strategy
                       allocation sites; ASan / LSan in CI;         and bound on signature
                       AllocClass<Tag, T> wrappers
16   Overflow          checked_add/mul_sat/sub_sat shipped;         declare overflow mode in
                       Saturated<T> carries was_clamped flag;       signature
                       UBSan in CI catches signed overflow
18   Mutation          AppendOnly<T>, Monotonic<T, Cmp>,            use specific wrapper for
                       ScopedView; deleted-copy on monotonic        monotonic / append-only state
                       types
19   Reentrancy        call-graph cycle detection;                  declare rec when needed;
                       rec / with Div grants; collision M012        with Div for genuine
                       with concurrent context                      non-termination
20   Size              codata destructor count via type-level       declare sized s; on
                       size parameter; with Productive obligation   productive codata
21   Version           refines / migration adapter resolution;      declare @[version(N)] +
                       semver bump computed from contract diff      refines / migration
22   Staleness         Stale<T> wrapper carries τ as type           wrap async-admitted values;
                       parameter; dependent-grade SMT on            assert admission obligation
                       `verify` preset for admission obligation     in pre clause
                       η · λ_max · τ ≤ critical
```

Coverage summary:

- **17 dimensions** discharged at compile time via concept gates / type system / structural wrappers.
- **3 dimensions** (Refinement boundary obligations, Precision via Higham analysis, Staleness admission) defer to Z3 on the `verify` preset for residual integer / FP-bound obligations.
- **0 dimensions** rely on runtime measurement for primary discharge.
- Cross-vendor numerics CI (§24.11) operationally discharges what static analysis cannot prove (numerical equivalence under recipes across vendors).
- Negative-compile fixture corpus (§24.12) demonstrates the rejection patterns the type system commits to.

Discharge ordering at every Fixy function declaration:

1. C++26 type checker fires (dims 1, 2 baseline)
2. Concept gates fire for ValidComposition (collision catalog §24.2 — 12 rules)
3. Per-wrapper concept gates fire (dims 3, 5, 7, 8, 11, 13–22)
4. Static_assert(sizeof) fires for layout discipline (dim 10)
5. Trust propagation runs across the linker's call graph (dim 9)
6. On `verify` preset only: Z3 fires for residual integer / FP / Staleness obligations
7. Per-PR cross-vendor numerics CI fires (operational discharge of recipe equivalence)

### 24.10 Strong-type catalog — substrate types per dimension

The Crucible C++26 substrate that Fixy emits to. One row per shipping wrapper, the dimension(s) it satisfies, the regime per `algebra/Graded.h`, and the source header.

```
Wrapper                            Dim(s)         Tier  Regime              Header
─────────────────────────────────  ─────────      ────  ─────────────────   ──────────────────────────
Linear<T>                          3              S     1 (zero-cost EBO)   safety/Linear.h
Refined<Pred, T>                   2              F     1 (zero-cost EBO)   safety/Refined.h
SealedRefined<Pred, T>             2              F     1 (zero-cost EBO)   safety/Refined.h
Tagged<T, Source>                  8              S     1 (zero-cost EBO)   safety/Tagged.h
Secret<T>                          5              S     1 (zero-cost EBO)   safety/Secret.h
Borrowed<T, Source>                7              S     1 (zero-cost EBO)   safety/Borrowed.h
BorrowedRef<T>                     7              S     1 (zero-cost EBO)   safety/Borrowed.h
ScopedView<C, Tag>                 7              S     1 (zero-cost EBO)   safety/ScopedView.h
Monotonic<T, Cmp>                  18             S     2 (T==element)      safety/Mutation.h
AppendOnly<T, Storage>             18             S     3 (derived grade)   safety/Mutation.h
Stale<T>                           22             S     4 (per-instance)    safety/Stale.h (planned)
Saturated<T>                       16             S     1 (zero-cost EBO)   safety/Saturated.h
Bits<E, Invariants>                multi          S     1 (zero-cost EBO)   safety/Bits.h
FixedArray<T, N>                   3 + 15         S     1 (zero-cost EBO)   safety/FixedArray.h
Permission<Tag>                    3 + 7          S     1 (sizeof=1, EBO)   safety/Permission.h
SharedPermission<Tag>              3 + 7          S     5 (proof-token)     safety/Permission.h
Computation<Row, T>                4              S     1 (EBO if Row=Tot)  effects/Computation.h
Capability<E, Source>              4 + 8          S     1 (zero-cost EBO)   effects/Capability.h
Session<Proto>                     6              T     n/a (typestate)     sessions/Session.h
PermissionedSessionHandle<...>     3 + 6          T     n/a (typestate)     sessions/PermissionedSession.h
Machine<States>                    multi          T     n/a (typestate)     safety/Machine.h
ConstantTime<T>                    11             S     1 (zero-cost EBO)   safety/ConstantTime.h
EpochVersioned<T>                  21             V     4 (per-instance)    safety/EpochVersioned.h
TimeOrdered<T, N, Tag>             7 + 19         S     4 (per-instance)    safety/TimeOrdered.h
WriteOnce<T>                       18             S     1 (zero-cost EBO)   safety/Mutation.h
WriteOnceNonNull<T*>               18             S     1 (zero-cost EBO)   safety/Mutation.h
BoundedMonotonic<T, Max>           18 + 15        S     1 (zero-cost EBO)   safety/Mutation.h
OrderedAppendOnly<T, KeyFn>        18             S     3 (derived grade)   safety/Mutation.h
AtomicMonotonic<T, Cmp>            18             S     2 (T==element)      safety/Mutation.h
NotInherited<T>, FinalBy<T>        structural     —     1 (CRTP)            safety/NotInherited.h
Pinned<T>                          structural     —     1 (CRTP)            safety/Pinned.h
OwnedRegion<T, Tag>                7 + 15         S     1 (zero-cost EBO)   safety/OwnedRegion.h (planned)
```

Substrate-engineer takeaway: every Fixy dimension that the type system enforces statically maps to a shipping (or planned, marked) C++26 wrapper from Crucible's `safety/`, `effects/`, `permissions/`, `sessions/` trees. There is no Fixy-specific "type theory runtime" — Fixy's transpiler emits these wrapper instantiations and the C++26 type system + concept gates discharge the obligations. The `Graded<Modality, Lattice, T>` substrate at `algebra/Graded.h` unifies the wrappers under one regime taxonomy (regimes 1–5 covering zero-cost EBO collapse through proof-token-with-runtime-state).

Coverage by tier:

```
Tier S   13 wrappers (Linear, Refined, Tagged, Secret, Borrowed×2, ScopedView,
         Monotonic family, Saturated, Bits, FixedArray, Permission family,
         Computation, ConstantTime, BoundedMonotonic, OrderedAppendOnly, ...)
Tier L    1 wrapper  (Repr* — alias-only, not a separate wrapper)
Tier T    3 wrappers (Session, PermissionedSessionHandle, Machine)
Tier F    2 wrappers (Type — built-in; Refined with predicates)
Tier V    1 wrapper  (EpochVersioned)
```

Total shipping or planned: **~25 wrappers** across the safety/effects/permissions/sessions trees, mechanizing all 20 dimensions.

### 24.11 Cross-vendor numerics CI matrix

Operational discharge mechanism for the numerical-equivalence obligation that the type system cannot prove statically. Per CRUCIBLE.md cross-vendor commitment.

For each (KernelKind × NumericalRecipe × vendor) tuple:

```
KernelKinds:    22 kernel families per FORGE.md §18.7
                (GEMM_MM, GEMM_BMM, ATTENTION_SDPA, NORM_LN, ACT_RELU,
                 REDUCE_SUM, SCAN_PREFIX_SUM, EMBEDDING, CONV2D, ...)

Recipes:        4 tiers per FORGE.md §20
                UNORDERED, ORDERED, BITEXACT_TC, BITEXACT_STRICT

Vendors:        6 backends per MIMIC.md §M2-M9
                NV   (sm_90, sm_100, sm_103)
                AM   (gfx942, gfx950)
                APL  (M-series AGX)
                TPU  (v5p, v6e, v7)
                TRN  (trn1, trn2, trn3)
                CPU  (x86_64, aarch64) — reference oracle
```

Per-tuple test:

1. Compile the kernel under (Kind, Recipe, Vendor). If `recipe.native_on(vendor) == false`, skip with a logged note (not all recipes are natively supported on every chip).
2. Generate input via Philox with content-hashed seed (deterministic, cross-vendor-identical bit pattern).
3. Execute on real hardware (for available silicon) or per-vendor calibrated simulator (for backends without hardware access in CI).
4. Compare output to CPU oracle's output element-wise.
5. Tolerance check per recipe tier:
   - `UNORDERED`: tolerance per recipe declaration (typically ≤ K × ULP for stated K)
   - `ORDERED`: tolerance per recipe declaration (typically tighter than UNORDERED)
   - `BITEXACT_TC`: ≤ 1 ULP elementwise
   - `BITEXACT_STRICT`: 0 bytes diff (binary equality)
6. Pass / fail / skip → contributes to fleet-wide numerics dashboard surfaced via `crucible numerics-dashboard`.

CI gates:

- Every PR must pass all `BITEXACT_*` cells for the kernels it touches.
- ULP-tolerant cells may regress within the recipe's declared bound; regressions exceeding the bound block merge.
- Adding a new vendor backend adds one column; failing CI pairwise against every other vendor blocks merge of the backend.
- Per-PR cell count: typically 200–800 cells (varies with the kernels touched); CI runs them in parallel across the test fleet.

This is the mechanism that operationally discharges what the type system cannot prove: that two different Mimic backends produce equivalent output up to the recipe's declared tolerance. Discharge happens once per PR, not once per call. The ~200–800 cells per PR amortize across all subsequent runtime calls of the changed kernels — the runtime never re-checks numerical equivalence, it trusts the CI gate.

Federation extension: the Cipher L1 cache (vendor-neutral IR002) federates content-addressed kernel hashes across organizations. A kernel whose CI matrix passed at one organization populates the L1 cache; a different organization downloading the kernel by hash inherits the CI evidence. The discharge is content-addressed and cross-organizational, not just per-PR.

### 24.12 Negative-compile fixture corpus

The patterns Fixy's neg-compile harness MUST reject. Per FX §30.14 Known Unsoundness Corpus, plus Crucible-specific patterns from the prior wrapper-discipline work and the §6.8 collision catalog. Each pattern lives as a standalone C++26 fixture in `test/fixy_neg/` that fails to compile with a specific named diagnostic.

```
Fixture name                                        Rule code   What it tests
──────────────────────────────────────────────────  ─────────   ──────────────────────────────────
neg_atkey_2018_lam_rule.cpp                         M001        linear value used in unrestricted closure
neg_double_consume_linear.cpp                       M001        linear value consumed twice
neg_session_endpoint_aliased_in_closure.cpp         S005        channel captured by replicable closure
neg_session_after_end.cpp                           S005        channel used after End state
neg_type_in_type_girard_paradox.cpp                 T101        impredicative encoding rejected
neg_classified_to_io_implicit.cpp                   I001        classified flows to unclassified IO
neg_secret_to_unclassified_no_declassify.cpp        I001        secret value emitted without declassify policy
neg_classified_fail_payload.cpp                     I002        Fail(E) payload carries classified data
neg_borrow_across_await.cpp                         L002        borrow live at await suspension
neg_ct_async_collision.cpp                          E044        CT × Async incompatible
neg_ct_secret_array_index.cpp                       CT002       secret-dependent memory access in CT region
neg_ct_secret_branch.cpp                            CT001       secret-dependent control flow in CT region
neg_ct_fail_on_secret.cpp                           I003        fail() on secret-dependent condition
neg_classified_async_session_no_ct.cpp              I004        classified over async session without CT
neg_monotonic_concurrent_no_atomic.cpp              M012        monotonic update concurrent without atomic
neg_decimal_overflow_wrap.cpp                       N002        wrap overflow on exact decimal type
neg_unscoped_spawn_borrow_capture.cpp               L003        unscoped spawn captures borrow
neg_linear_at_fail_no_cleanup.cpp                   M011        linear live at Fail without defer / errdefer
neg_ghost_runtime_match.cpp                         P002        ghost value as runtime scrutinee
neg_stale_ct_collision.cpp                          S010        Staleness × CT incompatible
neg_capability_replay_unstable.cpp                  S011        non-replay-stable capability
neg_fractional_permission_overallocation.cpp        M013        Frac(p1) + Frac(p2) > 1
neg_tainted_to_sink_no_sanitize.cpp                 I005        tainted reaches sink unsanitized
neg_dangling_ref_returned.cpp                       L001        reference outlives data
neg_pre_clause_violated_at_call.cpp                 R001        call site violates callee precondition
neg_repr_packed_alignment_conflict.cpp              T047        incompatible representation join
neg_clock_domain_mismatch.cpp                       CD001       cross-domain signal without synchronizer (when hardware gated in)
neg_session_subtype_undecidable.cpp                 S017        precise-async subtype undecidable within depth
neg_invalid_atomic_ordering.cpp                     T053        invalid ordering for atomic operation
neg_atomic_wide_unavailable.cpp                     T054        atomic_wide<T> on unsupporting target
```

Total: **30 negative-compile fixtures** in the v1 corpus. Each is ≤ 30 lines.

HS14 mandate: adding a new substrate primitive (per CLAUDE.md §XXI Universal Mint Pattern) requires shipping ≥ 2 additional neg-compile fixtures demonstrating the primitive's intended rejection patterns. The corpus grows monotonically over time; no fixture is ever removed (regression catalogs never shrink).

CI gating: the `default` and `release` CMake presets compile every fixture with `EXPECT_COMPILE_ERROR(diagnostic_substring)` semantics. A fixture that compiles successfully blocks merge — that means the type system regressed, accepting code it previously rejected.

### 24.13 Sketch / release / verify build profiles

Three CMake presets define how aggressively the substrate enforces obligations. All three accept identical Fixy source — the difference is what the elaborator and runtime do with the dimensions.

```
Preset      Trust gate            Contract semantic     Refinements        SMT discharge
─────       ──────────────       ──────────────────    ───────────────    ──────────────
sketch      none (warnings)       observe (log only)    runtime asserts    none
release     ≥ Verified except     enforce on boundary,  [[assume]] hints   none (compile-time
            @[trust_assumed]      ignore on hot path                       contract checks only)
verify      ≥ Verified            enforce               hint propagation   Z3 on bounded integer
                                                                            obligations on
                                                                            boundary code only
```

The `verify` preset's SMT discharge is intentionally narrow: residual integer / Presburger obligations on boundary code only (the same scope as TVM Analyzer PR #1367 — bounds, divisibility, modular arithmetic) plus Higham FP-error obligations from `Std.Numerics::FpError<bound>` opt-in annotations plus Staleness admission obligations. Default 5 ms timeout per query. Not on the hot path. Out of scope: kernel-optimality proofs, full floating-point reasoning, cost-model decidability, full algebraic-effect-handler discharge.

### 24.14 FX inheritance map — what Fixy keeps, simplifies, drops

For readers familiar with FX (`/root/iprit/FX/fx_design.md`, 16,622 lines, 30 sections + 8 appendices):

**Kept verbatim** — FX §1.2-1.6, §2 (lex), §3.1-3.4 + §3.6 + §3.8-3.9 + §3.12, §4 (expressions), §5 (declarations), §6.0-6.4 + §6.8-6.9 (graded substrate + collision catalog + boundaries), §7 (ownership), §7.11 (defer / errdefer + M011), §8.1-8.2 + §8.4-8.5 (memory), §9.1-9.5 + §9.10 (effect annotations + variables + lattice + built-ins + platform-conditional), §11.10 (memory model), §12.1-12.7 (information flow), §15 (code versioning), §17.1, 17.3-17.5 (comptime + decorators + custom attributes + physical units), §22.1, 22.8 (sketch/release profile + the gradient), §23.1-23.5, §23.7-23.8 (testing), §25 (distribution), §28 (scaling properties), §29 (domain archetypes minus hardware).

**Kept with simplification** — FX §1.1 (22 dimensions trimmed to **20**: drop dim 12 Clock Domain and dim 17 FP Order), §3.5 (codata without Later modality), §3.10 (string semantics without Codepoint/Byte measurement-unit alternatives — single grapheme-clustered `string`), §3.13 (HKT without polymorphism beyond binders), §6.5 (ghost binding without custom PCM declaration), §6.6 (user-defined dimensions but NO wall-clock dimensions in `Std.Budget` — Energy/Latency/Power/WallClock dropped entirely as false-guarantee annotations), §6.7 (refinements stay static; no SMT-discharged dependent grades on runtime values except Staleness admission), §11 (sessions: keep all 7 safety levels, all 4 primary combinators, crash-stop, SISO bounded subtyping; drop §11.24 CoindSpec encoding, §11.29 Pfenning-Toninho, §11.30 Reagent calculus formal), §13 (machines: keep §13.1-13.13, §13.15-13.17, §13.19-13.21, §13.23 event sourcing; drop §13.18 hardware temporal logic, §13.22 UI rendering, §13.24 IxMonad framing), §14 (contracts: drop §14.2's modal-univalence framing, keep migration as a verified total function), §16 (object model: keep impl/dot/resolution/type classes/existentials/variance/operator dispatch/derives; drop §16.6 algebraic-structure laws + §16.12 profunctor optics).

**Dropped entirely** — FX §3.7 (quotient types — Crucible's content-addressed Merkle DAG IS quotient operationally, no surface needed), §6.10 partial (modalities — keep `cap`, drop `later` `bridge` `fix` `next` `transport`), §9.6-9.8 (algebraic effect handlers + multi-shot continuations + generators — Crucible uses `std::expected` + RAII), §9.11 coeffect framing (keep dimensions, drop graded-comonad theory), §10.3-10.7 + §10.9-10.14 + §10.16-10.17 (verify blocks + guided proofs + calc + decide framework + proof diagnostics + organized proof work — Fixy keeps only `pre`/`post` via P2900R14 + Z3 on `verify` preset boundary), §11.17 (Association Δ ⊑_s G — Crucible enforces operationally), §11.23 (higher-order sessions — research-grade), §11.24 (CoindSpec kernel encoding — Lean artifact), §11.29-11.31 (Pfenning-Toninho / Reagent calculus / formal vector-clock Lifetime integration), §12.8 (faceted values — research-grade), §13 partial (Actors / hardware temporal logic / UI / IxMonad framing as detailed above), §17.2 (staged programming), §17.6 (codata operations), §17.7 (term reflection — use P2996 instead), §18 (entire chapter — Crucible synthesizes no Verilog; exception: §18.1 bit layouts kept for CNTP frames, §18.2 bit pattern matching kept), §19.4 (lock ordering — Crucible avoids locks on hot path), §19.6 (inline assembly), §20 (entire chapter — Fixy has no FXCore / FXLow / FXAsm IR; compiles to C++26 via the IR000/IR001 pipeline that Forge owns), §21.2 (automatic synchronization inference downgrade lattice), §21.3 (aggressive optimization / superoptimization / e-graph extraction), §22.2-22.7 (bytecode VM + record/replay + REPL inspect + traces + hot reload — Crucible has Cipher + Augur for replay/observability), §23.6 (type theory tests — Lean discharge), §24 (entire chapter — Compiler Agent Protocol HTTP daemon; Fixy v1 ships LSP only), §27.1 (fx-chip), §30 (entire chapter — Lean kernel + meta-theorems + IxMonad as Tier T + capability modality kernel rule + 2LTT mode separation + corrected Lam rule formalism; exception: §30.14 Known Unsoundness Corpus survives as the seed for Fixy's neg-compile fixture corpus per §24.12), Appendix D (Differences from F* — replaced with this map), Appendix G (ISA memory models — C++26 + std::atomic per-target handles), Appendix H (23-entry kernel axiom allowlist — Lean artifact).

**Dropped specifically per the wall-clock-not-real correction** — `Std.Budget.Energy`, `Std.Budget.Latency`, `Std.Budget.Power`, `Std.Budget.WallClock`, `Std.Budget.BitsTransferred` user-defined Tier-S dimensions. The compiler cannot prove physical bounds; annotation-only dimensions at this layer create false guarantees. Dim 13 Complexity stays because it propagates DECLARED costs through the call graph (signature-level discipline, not body-level inference) — the same shape as effect rows. SCHED_DEADLINE / realtime priority remain as OS-level scheduling-class hints (operational mechanism, not type-system claim).

Net Fixy spec footprint: projected **~12-14k lines** across grammar + lexer + semantics + this formal-specs appendix (the companion grammar / lexer / semantics docs are not yet written; the projection compares to FX's 16.6k baseline and the existing 1900-line integration doc). The drops are layers Crucible doesn't need (Lean kernel, Verilog synthesis, algebraic effect handlers, e-graph optimization, agent-protocol HTTP daemon, bytecode VM) plus research-grade fragments (faceted values, profunctor optics, modal univalence framing, dependent grades with runtime SMT, reagent calculus, wall-clock dimensions). What remains is the load-bearing typed surface that makes Fixy worth being a DSL rather than a library: 20 dimensions, 12 collision rules, 7 session safety levels, machine algebra, contract grammar, sketch/release/verify build profiles, the cross-vendor numerics CI matrix that operationally discharges what the type system cannot statically prove, and the negative-compile fixture corpus that demonstrates the rejection patterns the type system commits to.

---

*End of Fixy design document. Companion to CRUCIBLE.md (distributed substrate), FORGE.md (IR optimization pipeline, now `fixy/forge/`), MIMIC.md (per-vendor backends, now `fixy/mimic/`), and the FX semantics documents (`fixy_grammar.md`, `fixy_lexer.md`, `fixy_semantics.md`, `fixy_kernel.md`) for the language surface.*
