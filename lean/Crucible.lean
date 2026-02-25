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

/-!
# Crucible — Formal Specification

Mathematical models of Crucible's core C++ invariants.
Backported from actual implementation in `include/crucible/`.

Ten modules, matching the C++ headers:
- **Basic**: Power-of-two arithmetic, alignment, bitmask ≡ modulo
- **Arena**: Bump-pointer allocator (Arena.h) — alignment, non-overlap, within-bounds
- **Ring**: SPSC ring buffer (TraceRing.h) — capacity invariant, FIFO, drain
- **MetaLog**: Parallel SPSC buffer (MetaLog.h) — bulk append, contiguous spans, cached tail
- **MemoryPlan**: Sweep-line allocation (MerkleDag.h + BackgroundThread.h)
- **Mode**: State machine (CrucibleContext.h + ReplayEngine.h) — 2 modes, 3 statuses
- **Detector**: Iteration boundary detection (IterationDetector.h) — K=5 signature
- **Dag**: Content-addressed Merkle DAG (MerkleDag.h) — hash integrity, replay
- **Effects**: Compile-time effect system (Effects.h) — capability tokens, contexts, proofs
- **Philox**: Philox4x32-10 PRNG (Philox.h) — deterministic counter-based RNG, FNV-1a key derivation
-/
