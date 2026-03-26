# 5. Formal Verification (FX)

This section describes Crucible's approach to proving runtime invariants at build time. The type-system layer and effects system are implemented. The Z3 integration has scaffolding and an enhanced solver fork. The Lean 4 formalization accompanies the implementation as an independent proof artifact. Reflection-based verification requires GCC 16 with `-freflection` and is partially implemented.

## 5.1 Verification Philosophy

Crucible pursues a layered verification strategy organized by the strength of guarantee each layer provides:

| Layer | Mechanism | Guarantee | Scope |
|-------|-----------|-----------|-------|
| 4 | Z3 SMT solver | Universal (∀x. P(x)) | Mathematical properties over all inputs |
| 3 | `consteval` | Bounded (N test inputs, UB-free) | Implementation correctness for exercised paths |
| 2 | Static reflection | Structural (every field, every struct) | Completeness and consistency of data layout |
| 1 | Type system | API boundaries (zero-cost) | Compile error on misuse |

Each layer proves what the layers below cannot. Layer 1 prevents calling the wrong function. Layer 2 verifies that every struct is complete and consistent. Layer 3 proves the implementation is free of undefined behavior for exercised inputs. Layer 4 proves mathematical properties hold for every possible input.

The boundary between formal verification and empirical validation is explicit. Crucible proves: memory plan non-overlap, hash function quality, protocol deadlock freedom, kernel access safety, algebraic laws of combining operations. Crucible does NOT prove: memory ordering on real hardware (requires runtime sanitizers), numerical stability of floating-point computation (requires empirical bounds), global optimality of kernel configurations (requires hardware fidelity beyond the analytical model).

## 5.2 Z3 Integration

Crucible integrates a Z3 fork enhanced with CaDiCaL dual-mode search (VSIDS + VMTF switching), ProbSAT local-search walker, on-the-fly self-subsumption, arena-based clause allocation, and several additional optimizations. The enhanced SAT solver provides speedups on bitvector problems that dominate Crucible's proof obligations.

**Build integration.** A CMake custom target (`crucible_verify`) compiles an executable that links `libz3` and Crucible headers, encodes properties as SMT formulas, and runs proofs. If any theorem fails, the build fails. All targets depend on verification passing.

**What Z3 proves --- memory.** Arena alignment: the expression `(base + offset + align - 1) & ~(align - 1)` produces a correctly aligned result for all 2^192 (base, offset, align) combinations where align is a power of two. Memory plan non-overlap: for N tensors (parameterized, up to a bound) with arbitrary lifetimes, sizes, and alignments, the sweep-line allocator produces non-overlapping assignments for all simultaneously live tensors. Saturation arithmetic: `mul_sat(a, b)` equals `min(a*b, UINT64_MAX)` for all 2^128 input pairs.

**What Z3 proves --- hashing.** `fmix64(x) ≠ x` for all x ∈ [0, 2^64). Each of 64 input bit flips changes at least 20 output bits, for all inputs. Content hash determinism: same field values produce same hash output.

**What Z3 proves --- protocol.** SPSC ring: bitmask indexing `h & MASK` equals `h % CAPACITY` for all power-of-two capacities and all head values. Enqueue preserves the invariant `head - tail ≤ CAPACITY` for all (head, tail) pairs. No index collision when `0 < used < CAPACITY`.

**What Z3 proves --- kernels.** Each compiled kernel configuration is verified for: no shared-memory bank conflicts (32 threads access different banks or broadcast), coalesced global memory access (minimum 128B transactions), no out-of-bounds access (thread address < buffer size for all valid block/thread indices), register count within limits.

**Z3 as optimizer.** Beyond verification, Z3's `optimize` module solves for optimal configurations: minimum-footprint memory plans, roofline-optimal kernel parameters, and topology-optimal parallelism factorizations. When used as an optimizer, the result is optimal within the encoded model; correctness constraints (no OOB, no bank conflicts) are simultaneously enforced.

## 5.3 Consteval Verification

A `consteval` function executes inside the C++ compiler's abstract machine, which is a sound interpreter: null dereference, out-of-bounds access, signed integer overflow, use-after-free, double-free, memory leak, and uninitialized read are all compile errors. If a `consteval` function completes, every execution path was free of undefined behavior for the supplied inputs.

**Dual-mode Arena.** The Arena allocator uses `if consteval` to select between compile-time tracking (compiler verifies alignment, bounds, no overlap, no leak) and runtime execution (zero-overhead bump pointer). Same algorithm, two execution modes.

**Consteval fuzzing.** Philox4x32 is pure integer arithmetic and trivially `constexpr`. Crucible generates deterministic random inputs at compile time: random memory plans (verify non-overlap), random topological sorts (verify edge ordering), random struct instances (verify serialization roundtrip and hash determinism). The compiler proves all trials are UB-free.

**Finite-state model checking.** The SPSC ring protocol has finite state: (fg_phase × bg_phase × ring_count). For small capacity, exhaustive BFS at compile time verifies deadlock freedom and liveness. The mode transition state machine (INACTIVE/RECORD/COMPILED/DIVERGED) is similarly exhausted.

## 5.4 Reflection-Based Structural Verification

Using C++26 static reflection (P2996, available in GCC 16 with `-freflection`), Crucible performs compile-time structural checks on every layout-critical struct.

Four checks per field via `nonstatic_data_members_of(^^T)` and expansion statements:

1. `has_default_member_initializer(member)` --- InitSafe: every field has a default value. No uninitialized reads.
2. Offset sequence --- no unintended padding holes. For cache-critical structs, proves the layout matches design.
3. `type_of(member)` --- TypeSafe: raw `uint32_t` or `uint64_t` with ID-like names trigger a compile error demanding a strong type wrapper.
4. `sizeof(T)` --- MemSafe: matches expected value, catches silent layout changes.

**Auto-generation.** Reflection generates `reflect_hash<T>()`, `reflect_serialize<T>()`, `reflect_compare<T>()`, and `reflect_print<T>()` that operate on ALL fields. Adding a field automatically includes it; forgetting is impossible. Cross-checks verify hand-written versions agree with reflected versions for random test instances.

## 5.5 Type System Enforcement

The lowest verification layer uses the C++ type system for zero-cost, always-on enforcement.

**Capability tokens.** Functions requiring side effects take empty-struct parameters: `fx::Alloc` for arena allocation, `fx::IO` for I/O, `fx::Block` for potentially-blocking operations. Only authorized contexts construct tokens (`fx::Bg` for background thread, `fx::Init` for startup, `fx::Test` for testing). Foreground hot-path code holds no tokens; the compiler rejects effectful calls. Tokens are `[[no_unique_address]]` empty structs --- zero runtime cost.

**Thread-affinity phantom types.** `FgTag` and `BgTag` tag data structure handles with their owning thread. `TraceRingHandle<FgTag>` exposes `try_append()`; `TraceRingHandle<BgTag>` exposes `drain()`. Cross-thread access requires explicit `unsafe_borrow<OtherTag>()`. The compiler prevents cross-thread misuse; phantom types vanish in codegen.

**Strong types.** Every semantic value is a distinct type: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex` (uint32_t wrappers), `SchemaHash`, `ShapeHash`, `ScopeHash`, `CallsiteHash`, `ContentHash`, `MerkleHash` (uint64_t wrappers). Explicit construction, no implicit conversion, no arithmetic. The compiler rejects argument-order swaps that would silently corrupt data with raw integers.

**Typestate.** Mode transitions (INACTIVE → RECORD → COMPILED → DIVERGED) are encoded as types. `start_recording(Inactive)` compiles; `start_recording(Compiled)` has no overload and fails at compile time.

## 5.6 Lean 4 Formalization

Independent of the C++ implementation, Crucible maintains a Lean 4 formalization that proves properties of every core data structure and algorithm. The formalization covers:

- **Arena:** pairwise disjointness of allocations, alignment correctness, bounded fragmentation.
- **Memory plan:** sweep-line non-overlap, offset + size ≤ pool size, alignment waste bounds.
- **SPSC ring:** FIFO ordering for N push/pop sequences, batch drain correctness, capacity invariant preservation.
- **Iteration detector:** detection latency bounds, false positive probability bounds under hash independence assumptions.
- **Graph IR:** DCE fixpoint convergence, topological sort validity.
- **Merkle DAG:** collision probability bounds, structural diff correctness, replay completeness.
- **Scheduling:** Graham's list scheduling bound, Brent's theorem, critical path optimality.
- **Roofline:** multi-level cache model, wave quantization, correction factor model.
- **Fusion:** chain selection optimality, occupancy constraints.

All theorems are proved with no `sorry` (unresolved proof obligations). The formalization serves as a specification that the C++ implementation targets and as an independent check on algorithm correctness.
