# 2. Design Principles

This section describes the principles that guide Crucible's architecture. Each principle constrains the design space and has concrete consequences for the system's structure.

## 2.1 Content-Addressed Computation

Every compilable sub-graph in Crucible is identified by a content hash computed from the semantics of its computation: which operations execute, in what order, on what shapes, with what data types and scalar parameters. Two sub-graphs that perform identical computation produce identical hashes, regardless of which model they appear in, which training run generated them, or which organization executed them.

This has three consequences. First, compiled kernels are naturally reusable. The kernel cache maps (content_hash, device_capability) to compiled artifacts. A kernel compiled for one model is available to any future model that happens to contain the same sub-computation on the same hardware class. Second, structural comparison between computation graph versions reduces to hash comparison. If two DAG nodes share a Merkle hash, their entire sub-trees are identical --- enabling O(log N) diff, analogous to how Git identifies changes between commits. Third, content addressing provides a natural cache key for every level of the system: compiled kernels, memory plans, calibration data, and proof certificates.

## 2.2 Observe Before Optimizing

Crucible does not speculate about what a model will do. It records one complete iteration of what the model actually does, then optimizes based on observations.

This means the system never needs shape inference, symbolic tracing, or compiler-level analysis of Python control flow. The recording captures the concrete execution: actual shapes, actual data flow (tracked via pointer identity), actual iteration boundaries (detected via operation signature matching). The compilation target is a graph of what happened, not a graph of what might happen.

When the model's behavior changes --- dynamic shapes, data-dependent control flow, architecture modifications during training --- the compiled plan's guard hashes detect the mismatch. The system falls back to eager execution, records the new behavior, and recompiles. The observe-compile-execute cycle is continuous and self-correcting.

## 2.3 Formal Verification Where Tractable

Crucible pursues formal verification for properties that are (a) expressible in a decidable theory, (b) critical for correctness, and (c) insufficiently covered by testing.

Memory plan non-overlap is expressible as a bitvector satisfiability problem and is critical (overlapping allocations corrupt data silently). SPSC ring buffer protocol safety is a finite-state system amenable to exhaustive exploration. Kernel access bounds are expressible as bitvector constraints over thread and block indices. Hash function quality (no fixed points, avalanche) is expressible as universal bitvector queries. These properties are proved at build time.

Properties that depend on hardware timing (memory ordering on real CPUs), continuous quantities (numerical stability of floating-point computation), or unbounded state spaces (general program correctness) are outside the scope of static verification. For these, Crucible relies on runtime sanitizers (ThreadSanitizer), empirical validation (calibration benchmarks), and architectural mitigation (deterministic execution order).

The boundary is explicit: the system documents which properties are proved, which are tested, and which are mitigated by design.

## 2.4 Hardware-Agnostic Representation, Hardware-Specific Execution

The Merkle DAG captures what to compute. The kernel cache contains how to compute it on specific hardware. These are decoupled.

A single DAG describes a transformer's forward pass. The kernel cache may contain compiled kernels for that DAG on an NVIDIA H100 (sm_90, tensor core v4), an AMD MI300X (gfx942, matrix core), an older NVIDIA A100 (sm_80, tensor core v3), and a consumer 3090 (sm_86). Each entry is keyed by (content_hash, device_capability). When the same model migrates to new hardware, only the kernel cache entries for the new device need to be generated; the DAG is unchanged.

This decoupling makes heterogeneous clusters natural rather than exceptional. Different nodes in a distributed training run may execute different compiled kernels for the same logical computation, each specialized for the local hardware.

## 2.5 No Training/Inference Distinction

The compiled Merkle DAG is both the training artifact and the serving artifact. There is no export step, no format conversion, no operator coverage gap.

Training and inference differ only in which data flows through the DAG: training includes backward pass operations and optimizer updates; inference includes only the forward pass. Both execute through the same compiled kernels, the same memory plans, the same dispatch mechanism. Deploying a model means copying its persistent state (the Cipher) to a serving node. The node compiles hardware-specific kernels if needed and begins execution.

This eliminates a class of bugs where models behave differently in training and serving due to operator coverage differences, numerical precision changes, or execution order variations introduced by the export process.
