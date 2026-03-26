# 10. Related Work

## 10.1 ML Compilers and Runtimes

**XLA** [Google 2017] compiles whole-program HLO (High-Level Optimizer) graphs with static shape information, performing fusion, layout assignment, and memory planning at compile time. Crucible shares the goal of whole-program compilation but differs in its recording-based approach (no tracing or symbolic execution required), content-addressed caching (enabling cross-model reuse), and graduated divergence handling (rather than requiring static graphs).

**TVM and Ansor** [Chen et al. 2018, Zheng et al. 2020] provide a tensor compiler with search-based autotuning. Ansor uses learned cost models to guide the search over large configuration spaces. Crucible proposes an alternative approach: encoding the configuration problem as an SMT constraint optimization, trading search time for solver time and gaining formal safety guarantees. The approaches are complementary; empirical comparison of search quality, time-to-solution, and generalization across shapes is future work.

**Triton** [Tillet et al. 2019] provides a Python-based language for writing GPU kernels with programmer-guided tiling and automatic autotuning. Crucible operates at a different abstraction level --- it intercepts framework-level operations rather than providing a kernel authoring language. The kernel configurations that Crucible's SMT solver produces could target Triton's tiling model as one compilation backend.

**TorchInductor and torch.compile** [PyTorch 2.0, 2023] trace Python execution into FX graphs, apply pattern-matching optimizations, and generate Triton kernels. Crucible's recording-based approach avoids Python tracing limitations (graph breaks on unsupported operations) by recording at the ATen dispatch level where all operations are visible. The Merkle DAG extends the FX graph concept with content addressing, versioning, and persistence.

**Halide** [Ragan-Kelley et al. 2013] separates algorithm from schedule, enabling independent optimization of computation and data layout. Crucible's separation of the Merkle DAG (what to compute) from the KernelCache (how to compute it per device) follows a similar principle at the runtime level.

## 10.2 Automated Parallelism

**FlexFlow** [Jia et al. 2019] searches the parallelism configuration space (data, model, pipeline, expert) using a simulator-guided MCMC search. **Alpa** [Zheng et al. 2022] formulates parallelism as a two-level optimization: inter-operator (pipeline) and intra-operator (tensor) parallelism, using integer linear programming for the inter-operator level and dynamic programming for the intra-operator level. Crucible proposes formulating the complete configuration (all parallelism dimensions, communication algorithms, memory management) as a single SMT optimization problem, solving jointly rather than decomposing. The tradeoff is solver scalability vs decomposition approximation error.

**Megatron-LM** [Shoeybi et al. 2019, Narayanan et al. 2021] and **DeepSpeed** [Rasley et al. 2020] provide efficient implementations of specific parallelism strategies (tensor parallel, pipeline parallel, ZeRO) with manual configuration. Crucible aims to automate the configuration selection while reusing the same underlying parallelism mechanisms.

## 10.3 Formal Verification in Systems

**seL4** [Klein et al. 2009] provides a fully verified microkernel with machine-checked proofs of functional correctness and security properties. **CompCert** [Leroy 2009] is a formally verified C compiler. **CertiKOS** [Gu et al. 2016] extends verified OS kernels with concurrency. These projects verify complete system stacks. Crucible's verification scope is narrower --- runtime invariants (memory safety, protocol correctness, kernel bounds) rather than full functional correctness --- but targets a domain (ML runtimes) where formal verification has not been applied.

**Dafny** [Leino 2010] and **F\*** [Swamy et al. 2016] provide verified programming languages with proof obligations integrated into the development workflow. Crucible uses Z3 directly for domain-specific properties expressed as SMT formulas, rather than verifying general programs. The FX approach is less general but more targeted: specific invariants are encoded as specific formulas and proved as part of the build.

## 10.4 Fault-Tolerant and Persistent Training

**CheckFreq** [Mohan et al. 2021] optimizes checkpoint frequency by modeling the tradeoff between checkpoint overhead and wasted computation on failure. **Gemini** [Zhuang et al. 2023] provides memory-efficient checkpointing. These systems optimize within the snapshot-based checkpointing paradigm. Crucible's Cipher uses event sourcing: persisting the DAG chain (a few kilobytes per step) rather than full state snapshots, with periodic snapshots for recovery efficiency. This enables recovery to arbitrary points in training history, not just the most recent checkpoint.

**Varuna** [Athlur et al. 2022] provides fault tolerance for pipeline-parallel training on spot instances. Crucible's Canopy extends this with configurable redundancy (the α parameter), topology-aware placement, and integration with the Cipher for zero-loss recovery.

## 10.5 Content-Addressed Systems

**Git** [Torvalds 2005] uses content-addressed Merkle trees for version control, enabling O(log N) diff and distributed collaboration. **IPFS** [Benet 2014] extends content addressing to a distributed file system. **Nix** [Dolstra et al. 2004] uses content-addressed builds for reproducible software deployment. Crucible applies content addressing to computation graphs, enabling the same properties (structural diff, deduplication, distributed sharing) for compiled ML kernels.
