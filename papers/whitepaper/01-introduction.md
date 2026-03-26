# 1. Introduction

## 1.1 The Problem

PyTorch's eager execution model lets researchers write arbitrary Python control flow around tensor operations, debug interactively, and iterate rapidly. This design dominates ML research. It also structurally prevents several categories of optimization.

**Static kernel selection.** `torch.mm(A, B)` dispatches to a pre-compiled library kernel (cuBLAS, cuDNN, CUTLASS) selected by operation name and data type. The same kernel executes for 64x64 and 8192x8192 matrices, for H100 and 3090, for contiguous and transposed inputs, regardless of whether adjacent operations could be fused.

**Per-operation overhead.** Each operation independently allocates output memory, dispatches a kernel, and returns. A 1,000-operation model incurs 1,000 allocator calls (mutex acquisition, freelist search, block splitting), 1,000 kernel launches, and 1,000 Python-level returns with reference counting. PyTorch's CUDACachingAllocator averages 200--2000ns per allocation; at 1,000 allocations per iteration, allocation alone consumes 0.2--2ms.

**Training/inference discontinuity.** A model trained in PyTorch must be exported (`torch.export`, ONNX, TorchScript) to a serving runtime. Each export path has operator coverage gaps. The result is a separate engineering effort with no guarantee of numerical equivalence.

**Fixed configuration.** Parallelism strategy, learning rate schedules, precision choices, and batch sizes are fixed before training begins. If hardware degrades (thermal throttling, network congestion, node failure) or training dynamics shift (gradient health, effective rank, layer redundancy), the configuration cannot adapt.

**No formal guarantees.** Memory plans may overlap. Ring buffer protocols may deadlock. Kernel configurations may access out-of-bounds memory. Hash functions may have fixed points. Testing and fuzzing demonstrate the presence of bugs but not their absence.

These are consequences of design choices that prioritize other goals. Crucible explores a different point in the design space.

## 1.2 Approach

Crucible records what the framework does, then executes it better next time.

**Terminology.** The **Vessel** is the framework adapter --- currently PyTorch's ATen dispatch layer, though the architecture is framework-agnostic. The **Vigil** is the model as a persistent entity: computation graph, weights, and accumulated knowledge, outliving any hardware node. The **Cipher** is event-sourced persistent state (DAG chain, weight snapshots, kernel cache, RNG state) enabling hardware migration. A **Relay** is a compute node running a Crucible daemon. The **Keeper** is the per-Relay daemon for health monitoring, mesh participation, and executing optimization recommendations. The **Canopy** is the masterless Keeper mesh. **Longitude** is startup calibration (hardware measurement and configuration optimization). **Augur** is continuous monitoring (performance prediction, drift detection, bottleneck diagnosis). **FX** is the formal verification layer proving runtime invariants at build time.

**Three execution phases.** In **recording**, Crucible intercepts Vessel dispatch and records each operation's identity, input/output tensor metadata, and scalar arguments into a lock-free SPSC ring buffer (65,536 entries of 64 bytes each, with parallel arrays for scope and callsite hashes). The Vessel executes normally; recording adds 3--5ns per operation on the hot path (one cache-line write plus a release store on the head pointer). A separate MetaLog (1M entries, 144 bytes each) stores full tensor metadata. In **analysis**, a background thread drains the ring buffer in batches via memcpy, constructs a bidirectional CSR dataflow graph, detects iteration boundaries via K=5 schema hash signature matching, and builds a content-addressed Merkle DAG. From the DAG, it computes a static memory plan and prepares a compiled execution plan. In **compiled** mode, the Vessel interceptor advances an operation index, checks a guard hash, and returns a pre-allocated shadow handle --- no execution, no allocation, no redispatch.

**The Merkle DAG** is the central abstraction. Each node's content hash is computed from computation semantics: operation identities, shapes, data types, scalar arguments. Identical computations produce identical hashes regardless of origin. This enables kernel reuse across iterations, runs, models, and organizations. The DAG simultaneously serves as versioning mechanism (Merkle hashes enable O(log N) structural diff), guard system (hash mismatch triggers recompilation), deployment artifact (no export step), and persistence format (the Cipher is event-sourced from DAG state).

**Formal verification** is integrated at build time through four layers: Z3 SMT solver for universal proofs over all inputs, `consteval` for bounded model checking by the compiler's abstract machine, static reflection (C++26 P2996) for structural completeness checks, and the C++ type system for zero-cost API enforcement (capability tokens, strong types, phantom thread-affinity tags).

## 1.3 Contributions

1. A content-addressed Merkle DAG enabling cross-model, cross-run kernel reuse through semantic hashing of computation sub-graphs.
2. SMT-based kernel configuration synthesis formulating hardware constraints and roofline cost models as satisfiability problems, producing configurations with formal safety guarantees.
3. Static memory planning from dataflow lifetimes, replacing per-operation dynamic allocation with a single pre-computed pool layout.
4. Graduated divergence detection with four severity levels (schema, shape, scope, callsite hash mismatches), enabling seamless compiled-to-eager fallback.
5. A four-layer formal verification architecture (FX) proving runtime invariants at build time: Z3 universal proofs, `consteval` bounded model checking, reflection-based structural checks, and type-system enforcement via capability tokens and strong types.
6. An event-sourced persistence model (the Cipher) enabling deterministic replay, hardware migration, and recovery from any training step.
7. A prototype implementation (~9,500 lines of C++26, 24 tests, compiling clean on Clang 22 and GCC 15), accompanied by a Lean 4 formalization (36 modules, 1,312 theorems, zero `sorry`).

## 1.4 Paper Organization

Section 2: design principles. Section 3: recording and Merkle DAG representation. Section 4: compilation, memory planning, execution model. Section 5: formal verification (FX). Section 6: hardware adaptation (Longitude, Augur). Section 7: distribution (Canopy, Keepers, Relays) and persistence (Cipher). Section 8: model-aware optimizations. Section 9: implementation status. Section 10: related work. Section 11: roadmap. Section 12: conclusion.
