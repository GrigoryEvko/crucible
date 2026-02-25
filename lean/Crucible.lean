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
-/
