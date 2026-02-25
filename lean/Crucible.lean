import Crucible.Graph
import Crucible.SwissTable
import Crucible.Pool
import Crucible.Expr
import Crucible.Serialize
import Crucible.Ops
import Crucible.TraceGraph
import Crucible.CKernel
import Crucible.SymbolTable
import Crucible.Cipher
import Crucible.Basic
import Crucible.Arena
import Crucible.Ring
import Crucible.MetaLog
import Crucible.MemoryPlan
import Crucible.Mode
import Crucible.Detector
import Crucible.Dag
import Crucible.Effects
import Crucible.Philox
import Crucible.BitVec
import Crucible.Algebra
import Crucible.Protocol
import Crucible.Redundancy
import Crucible.Topology
import Crucible.Roofline
import Crucible.Scaling
import Crucible.Attention
import Crucible.Scheduler
import Crucible.Fusion
import Crucible.Migration
import Crucible.TokenMerge
import Crucible.Quantize
import Crucible.Canopy

/-!
# Crucible -- Formal Specification

Mathematical models of Crucible's core C++ invariants.
Backported from actual implementation in `include/crucible/`.

Modules matching the C++ headers:
- **Basic**: Power-of-two arithmetic, alignment, bitmask = modulo
- **Arena**: Bump-pointer allocator (Arena.h) -- alignment, non-overlap, within-bounds
- **Ring**: SPSC ring buffer (TraceRing.h) -- capacity invariant, FIFO, drain
- **MetaLog**: Parallel SPSC buffer (MetaLog.h) -- bulk append, contiguous spans, cached tail
- **MemoryPlan**: Sweep-line allocation (MerkleDag.h + BackgroundThread.h)
- **Mode**: State machine (CrucibleContext.h + ReplayEngine.h) -- 2 modes, 3 statuses
- **Detector**: Iteration boundary detection (IterationDetector.h) -- K=5 signature
- **Dag**: Content-addressed Merkle DAG (MerkleDag.h) -- hash integrity, replay
- **Effects**: Compile-time effect system (Effects.h) -- capability tokens, contexts, proofs
- **Philox**: Philox4x32-10 PRNG (Philox.h) -- deterministic counter-based RNG, FNV-1a key derivation
- **Cipher**: Persistent state (Cipher.h) -- event-sourced object store, time travel, three-tier recovery
- **TraceGraph**: Bidirectional CSR property graph (TraceGraph.h) -- dataflow, alias edges, acyclicity
- **Ops**: Symbolic expression operations (Ops.h) -- 60 ops, arity, commutativity, associativity
- **Expr**: Interned symbolic expressions (Expr.h + ExprPool.h) -- interning correctness, canonicalization
- **Pool**: PoolAllocator (PoolAllocator.h) -- 256B alignment, bounds, init from MemoryPlan
- **SwissTable**: SIMD hash table (SwissTable.h + ExprPool.h) -- control bytes, probing, insert/find correctness
- **Graph**: Computation graph IR (Graph.h) -- acyclicity, topological ordering, DCE semantics, SSA well-formedness
- **BitVec**: Bitvector proofs via `bv_decide` -- arena alignment, bitmask indexing, saturation arithmetic
- **Algebra**: Algebraic structures -- ScalarType lattice, hash XOR monoid, Galois connection, DAG transforms
- **Protocol**: Protocol verification -- SPSC deadlock freedom, mode transition completeness, protocol liveness
- **Redundancy**: L12 RAID shard redundancy -- fault tolerance, ring replication, DiLoCo sync
- **Topology**: L16 Meridian parallelism -- 3D/5D factorization, comm cost, placement, load balance, bucketing
- **Roofline**: L17 Augur digital twin -- roofline model, bottleneck classification, Amdahl's law, iteration time
- **Scaling**: L17 Augur convergence -- loss models, LR schedules, SNR, effective rank, CKA, Chinchilla, batch scaling
- **Attention**: L8 attention head classification -- cost model, replacement bounds, gradient strategy, bottleneck, NaN detection
- **Scheduler**: L5 multi-stream scheduling -- task graphs, EST, critical path, compute-comm overlap, pipeline bubbles
- **Fusion**: L1/L6 kernel fusion -- legality, cost model, chain savings, register pressure, elementwise, shared memory tiers
- **Migration**: L13 lifecycle -- three-tier recovery, event-sourced replay, snapshot recovery, DAG chain integrity, reincarnation, deterministic replay, time travel, proof persistence
- **TokenMerge**: L7 token optimization -- adaptive merging, O(n^2) attention savings, early exit, adaptive patching, mixed precision, variable-length batching, combined savings
- **Quantize**: L10 mixed precision -- per-op precision selection, error/cost model, error propagation, gradient precision, sensitivity classification
- **Canopy**: L12 distributed mesh -- gossip protocol, Raft consensus, peer discovery, partition healing, health propagation, no-master architecture
-/
