# 10. Related Work

## 10.1 ML Compilers and Runtimes

**XLA** [Google 2017] compiles whole-program HLO graphs with static shapes --- fusion, layout assignment, memory planning at compile time. Crucible shares whole-program compilation goals but differs: recording-based (no tracing/symbolic execution), content-addressed caching (cross-model reuse), graduated divergence handling (not requiring static graphs).

**TVM/Ansor** [Chen et al. 2018, Zheng et al. 2020]. Search-based autotuning with learned cost models. Crucible proposes SMT constraint optimization as an alternative: trading search time for solver time, gaining formal safety guarantees. Empirical comparison is future work.

**Triton** [Tillet et al. 2019]. Python-based kernel language with programmer-guided tiling. Crucible operates at a different level (framework dispatch interception vs kernel authoring). SMT-generated configurations could target Triton's tiling model as a backend.

**TorchInductor/torch.compile** [PyTorch 2.0, 2023]. Traces Python into FX graphs, generates Triton kernels. Crucible records at ATen dispatch level, avoiding graph breaks on unsupported operations. The Merkle DAG extends FX graphs with content addressing, versioning, and persistence.

**Halide** [Ragan-Kelley et al. 2013]. Algorithm/schedule separation. Crucible's DAG/KernelCache separation follows the same principle at runtime level.

## 10.2 Automated Parallelism

**FlexFlow** [Jia et al. 2019]: simulator-guided MCMC search over parallelism configurations. **Alpa** [Zheng et al. 2022]: two-level optimization (ILP for inter-op pipeline, DP for intra-op tensor). Crucible formulates all dimensions (TP, DP, PP, EP, CP), communication algorithms, and memory management as a single SMT problem, solving jointly. Tradeoff: solver scalability vs decomposition approximation error.

**Megatron-LM** [Shoeybi et al. 2019, Narayanan et al. 2021] and **DeepSpeed** [Rasley et al. 2020] provide efficient manual-configuration parallelism. Crucible automates configuration selection over the same underlying mechanisms.

## 10.3 Formal Verification in Systems

**seL4** [Klein et al. 2009]: fully verified microkernel. **CompCert** [Leroy 2009]: verified C compiler. **CertiKOS** [Gu et al. 2016]: verified concurrent OS kernel. These verify complete system stacks. Crucible's scope is narrower --- runtime invariants (memory safety, protocol correctness, kernel bounds) rather than full functional correctness --- but targets ML runtimes where formal verification has not been applied.

**Dafny** [Leino 2010] and **F\*** [Swamy et al. 2016]: verified programming languages with integrated proof obligations. Crucible uses Z3 directly for domain-specific SMT formulas rather than verifying general programs. Less general but more targeted: specific invariants as specific formulas, proved as part of the build.

## 10.4 Fault-Tolerant and Persistent Training

**CheckFreq** [Mohan et al. 2021]: optimal checkpoint frequency via cost modeling. **Gemini** [Zhuang et al. 2023]: memory-efficient checkpointing. Both optimize within snapshot-based paradigm. Crucible's Cipher uses event sourcing: DAG chain (KB per step) + periodic snapshots. Enables recovery to arbitrary training steps.

**Varuna** [Athlur et al. 2022]: fault tolerance for pipeline-parallel spot instances. Crucible extends with configurable redundancy (alpha parameter), topology-aware placement, and Cipher integration for zero-loss recovery.

## 10.5 Content-Addressed Systems

**Git** [Torvalds 2005]: Merkle trees for version control. **IPFS** [Benet 2014]: content-addressed distributed filesystem. **Nix** [Dolstra et al. 2004]: content-addressed reproducible builds. Crucible applies the same principle to computation graphs: structural diff, deduplication, distributed sharing for compiled ML kernels.
