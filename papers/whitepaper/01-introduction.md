# 1. Introduction

## 1.1 The Problem

Modern ML frameworks prioritize flexibility and programmer ergonomics. PyTorch's eager execution model lets researchers write arbitrary Python control flow around tensor operations, debug interactively, and iterate rapidly. This design achieves its goals well --- it is the dominant paradigm for ML research. However, it structurally prevents several categories of optimization.

**Static kernel selection.** When a researcher calls `torch.mm(A, B)`, the framework dispatches to a pre-compiled library kernel (cuBLAS, cuDNN, CUTLASS) selected by operation name and data type. The same kernel executes whether the matrices are 64×64 or 8192×8192, whether the GPU is an H100 or a 3090, whether the input is contiguous or transposed, whether adjacent operations could be fused. The kernel was compiled for generality, not for this specific invocation.

**Per-operation overhead.** Each operation independently allocates output memory, dispatches a kernel, and returns. For a model executing 1,000 operations per iteration, this means 1,000 allocator calls (each acquiring a mutex, searching a freelist, potentially splitting blocks), 1,000 kernel launches, and 1,000 Python-level returns with associated reference counting and error checking. The cumulative overhead can represent a significant fraction of iteration time for small-to-medium models.

**Training/inference discontinuity.** Frameworks maintain separate paths for training and deployment. A model trained in PyTorch must be exported (via `torch.export`, ONNX, TorchScript) to a serving runtime. Each export path has coverage gaps --- operations that work during training but fail during export. The result is a separate engineering effort to achieve deployment, with no guarantee of numerical equivalence.

**Fixed configuration.** Parallelism strategy (data parallel, tensor parallel, pipeline parallel), learning rate schedules, precision choices, and batch sizes are configured before training begins. If hardware characteristics change (thermal throttling, network degradation, node failure), or if the model's own training dynamics shift (gradient health, effective rank, layer redundancy), the configuration cannot adapt.

**No formal guarantees.** Memory plans may overlap. Ring buffer protocols may deadlock. Kernel configurations may access out-of-bounds memory. Hash functions may have fixed points. These properties are validated through testing and fuzzing, which can demonstrate the presence of bugs but not their absence.

These are not flaws in current systems --- they are consequences of design choices that prioritize other goals. Crucible explores a different point in the design space.

## 1.2 Approach

Crucible is structured around a single observation: if the runtime records what the framework does, it can do it better next time.

We introduce the following terminology. The **Vessel** is the framework adapter through which user code enters Crucible --- currently PyTorch's ATen dispatch layer, though the architecture is not framework-specific. The **Vigil** is the model: its computation graph, weights, and accumulated knowledge, treated as a persistent entity that outlives any individual hardware node. The **Cipher** is the event-sourced persistent state (DAG chain, weight snapshots, kernel cache, RNG state) that enables the Vigil to survive hardware failure and resume on new devices. The **Relay** is a compute node running a Crucible daemon. The **Keeper** is the per-Relay daemon responsible for health monitoring, mesh participation, and executing optimization recommendations. The **Canopy** is the masterless mesh of Keepers providing distributed coordination. **Longitude** is the startup calibration system that measures hardware and solves for optimal configuration. **Augur** is the continuous monitoring system that predicts performance, detects drift, and diagnoses bottlenecks. **FX** is the formal verification layer that proves runtime invariants at build time.

The execution cycle has three phases. In the **recording** phase, Crucible intercepts Vessel dispatch and records each operation's identity, input/output tensor metadata, and scalar arguments into a lock-free SPSC ring buffer. The Vessel executes normally; recording adds approximately 20ns of overhead per operation. In the **analysis** phase, a background thread drains the ring buffer, constructs a bidirectional dataflow graph, detects iteration boundaries, and builds a content-addressed Merkle DAG. From the DAG, it computes a static memory plan (eliminating runtime allocation), selects kernel configurations (via SMT constraint solving within an analytical cost model), and prepares a compiled execution plan. In the **compiled** phase, Crucible intercepts Vessel dispatch and returns pre-allocated tensor handles with correct metadata, while the GPU executes the compiled plan asynchronously. Python receives shadow handles at the cost of a pointer advance; the GPU executes without Python involvement.

The Merkle DAG is the central abstraction. Each node's content hash is computed from the semantics of the computation it represents --- which operations, which shapes, which data types. Identical computations produce identical hashes regardless of where they appear. This enables compiled kernel reuse across iterations, across runs, across models that share sub-computations, and across organizations. The DAG also serves as the versioning mechanism (Merkle hashes enable O(log N) structural diff), the guard system (hash mismatch triggers recompilation), the deployment artifact (no export step --- the compiled DAG IS the Vigil), and the persistence format (the Cipher is event-sourced from DAG state).

Formal verification is integrated at build time through FX's four complementary layers: a Z3 SMT solver for universal mathematical proofs, `consteval` for bounded model checking by the compiler's abstract machine, static reflection for structural completeness checks, and the C++ type system for zero-cost API enforcement. Together, these layers prove properties ranging from memory plan non-overlap (for all lifetime configurations) to hash function quality (no fixed points in all 2^64 inputs) to protocol deadlock freedom (exhaustive finite-state exploration).

## 1.3 Contributions

1. A content-addressed computation representation (Merkle DAG) that enables cross-model, cross-run kernel reuse through semantic hashing of computation sub-graphs.
2. An SMT-based kernel configuration synthesis approach that formulates hardware constraints and cost models as satisfiability problems, producing configurations with formal safety guarantees.
3. A static memory planning algorithm based on dataflow lifetime analysis that replaces per-operation dynamic allocation with a single pre-computed layout.
4. A graduated divergence detection mechanism with four severity levels, enabling pre-emptive fallback from compiled to eager execution without error.
5. A four-layer formal verification architecture (FX) that proves runtime invariants at build time, spanning Z3 universal proofs, compiler-based bounded model checking, structural reflection, and type-system enforcement.
6. An event-sourced persistence model (the Cipher) that enables deterministic replay, hardware migration, and fault recovery from any point in training history.
7. A prototype implementation of the recording and compilation infrastructure, accompanied by a Lean 4 formalization proving properties of every core algorithm and data structure.

## 1.4 Paper Organization

Section 2 describes Crucible's design principles. Section 3 covers recording and the Merkle DAG representation. Section 4 describes compilation, memory planning, and the execution model. Section 5 presents the formal verification architecture (FX). Section 6 describes hardware adaptation through Longitude calibration and Augur monitoring. Section 7 covers distribution (Canopy, Keepers, Relays) and fault-tolerant persistence (Cipher). Section 8 discusses model-aware optimizations enabled by the DAG representation. Section 9 reports on implementation status. Section 10 positions Crucible relative to existing work. Section 11 outlines the research roadmap. Section 12 concludes.
