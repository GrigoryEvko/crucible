# 5. Formal Verification (FX)

The type-system layer and effects system are implemented. Z3 integration has scaffolding and an enhanced solver fork. Reflection-based verification requires GCC 16 with `-freflection` and is partially implemented. The Lean 4 formalization accompanies the implementation as an independent proof artifact.

## 5.1 Verification Layers

| Layer | Mechanism | Guarantee | Scope |
|-------|-----------|-----------|-------|
| 4 | Z3 SMT solver | Universal (forall x. P(x)) | Mathematical properties over all inputs |
| 3 | `consteval` | Bounded (N inputs, UB-free) | Implementation correctness for exercised paths |
| 2 | Static reflection | Structural (every field, every struct) | Completeness and consistency of data layout |
| 1 | Type system | API boundaries (zero-cost) | Compile error on misuse |

Each layer proves what the layers below cannot. Layer 1 prevents calling the wrong function. Layer 2 verifies struct completeness. Layer 3 proves UB-freedom for exercised inputs. Layer 4 proves mathematical properties for all inputs.

Crucible proves: memory plan non-overlap, hash function quality, protocol deadlock freedom, kernel access safety, algebraic combining laws. Crucible does NOT prove: memory ordering on real hardware (ThreadSanitizer), floating-point stability (empirical bounds), global kernel optimality (model fidelity limitation).

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

Using C++26 static reflection (P2996, GCC 16 with `-freflection`), Crucible performs compile-time structural checks. Reflect.h (guarded by `CRUCIBLE_HAS_REFLECTION`) implements:

`reflect_hash<T>()` iterates all non-static data members via `nonstatic_data_members_of(^^T, access_context::unchecked())` and hashes each field with `fmix64`: integral types via cast, floats via `bit_cast`, pointers via `reinterpret_cast<uintptr_t>`, C arrays element-by-element, nested structs recursively. Adding a field automatically includes it in the hash; forgetting is structurally impossible.

`reflect_print<T>()` generates debug output for every field, dispatching on type category via the same reflection machinery.

Four structural checks per field: (1) `has_default_member_initializer` (InitSafe: every field has NSDMI), (2) offset sequence (no unintended padding holes), (3) `type_of` (TypeSafe: raw uint32_t/uint64_t with ID-like names triggers compile error), (4) `sizeof(T)` (MemSafe: catches silent layout changes). Cross-checks verify hand-written hash functions agree with reflected versions.

## 5.5 Type System Enforcement

**Capability tokens** (Effects.h). Three effect types with private constructors: `fx::Alloc` (heap allocation), `fx::IO` (file/network I/O), `fx::Block` (blocking operations). Three authorized contexts: `fx::Bg` (background thread: all three), `fx::Init` (initialization: Alloc + IO, no Block), `fx::Test` (unrestricted). Each context is a 1-byte struct with `[[no_unique_address]]` members --- `static_assert(sizeof(fx::Bg) == 1)`. C++20 concepts enforce requirements: `fx::CanAlloc<Ctx>` checks for an `alloc` member; `fx::Pure<Ctx>` checks for absence of all effects. Foreground hot-path code holds no tokens; the compiler rejects effectful calls. Example: `Arena::alloc(fx::Alloc, size_t, size_t)` --- the first parameter is a compile-time proof of authorization.

**Strong types** (Types.h). `CRUCIBLE_STRONG_ID(Name)` generates a uint32_t wrapper with explicit constructor, `.raw()` unwrap, `.none()` sentinel (UINT32_MAX), `.is_valid()` check, `operator<=>`, and no arithmetic. Five ID types: OpIndex, SlotId, NodeId, SymbolId, MetaIndex. `CRUCIBLE_STRONG_HASH(Name)` generates a uint64_t wrapper with explicit constructor, `.raw()`, `.sentinel()` (UINT64_MAX), `operator<=>`, and no arithmetic. Six hash types: SchemaHash, ShapeHash, ScopeHash, CallsiteHash, ContentHash, MerkleHash. All are `static_assert`-verified to have the same size as the underlying integer and are trivially relocatable.

**Typestate.** Mode transitions (INACTIVE -> RECORD -> COMPILED -> DIVERGED) are encoded as types. `start_recording(Inactive)` compiles; `start_recording(Compiled)` has no overload.

## 5.6 Lean 4 Formalization

Independent of the C++ implementation, Crucible maintains a Lean 4 formalization that proves properties of every core data structure and algorithm. The formalization covers:

- **Arena:** pairwise disjointness of allocations, alignment correctness, bounded fragmentation.
- **PoolAllocator:** slot disjointness for overlapping lifetimes, offset + size <= pool_bytes.
- **Memory plan:** sweep-line non-overlap, offset + size <= pool size, alignment waste bounds.
- **SPSC ring:** FIFO ordering for N push/pop sequences, batch drain correctness, capacity invariant preservation.
- **MetaLog:** bulk read-after-write correctness for all indices.
- **Iteration detector:** detection latency bounds (exactly K ops), false positive probability bounds under hash independence assumptions.
- **TraceGraph:** CSR construction correctness, bidirectional consistency.
- **Graph IR:** DCE fixpoint convergence, topological sort validity.
- **Merkle DAG:** collision probability bounds, structural diff correctness, replay completeness.
- **Scheduling:** Graham's list scheduling bound, Brent's theorem, critical path optimality.
- **Roofline:** multi-level cache model, wave quantization, correction factor model.
- **Fusion:** chain selection optimality, occupancy constraints.

All theorems are proved with no `sorry` (unresolved proof obligations). The formalization serves as a specification that the C++ implementation targets and as an independent check on algorithm correctness.
