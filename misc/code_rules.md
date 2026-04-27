# Crucible C++26 Code Rules

*Performance is non-negotiable. Safety is structural, not aspirational.*

Crucible is a symbiotic runtime organism where the foreground hot path
records ops at the lowest cost the hardware allows and the background
thread builds, compiles, and replays computation graphs. Every design
decision serves this dual reality: the foreground must never stall,
and the background must produce correct compiled output. These rules
encode how we write C++26 to achieve both.

## Toolchain

Three compilers, different strengths:

| Preset    | Compiler                  | Stdlib      | Role                                          |
|-----------|---------------------------|-------------|-----------------------------------------------|
| `default` | Clang 22.1.0 + libc++ 22  | libc++ 22   | Primary dev build. Best diagnostics, fastest compile. |
| `release` | Clang 22.1.0 + libc++ 22  | libc++ 22   | Production. `-O3 -march=native -DNDEBUG`      |
| `gcc`     | GCC 15.2.1                | libstdc++ 15| Conservative fallback. Stable, well-tested.    |
| `gcc16`   | GCC 16.0.1 (rawhide)      | libstdc++ 16| Bleeding-edge. Reflection, `inplace_vector`, expansion statements. |

**Core headers** (everything in `include/crucible/`) must compile
clean on **all three compilers** with zero warnings. The intersection
of features across all three is the baseline.

Compiler-specific features (`-freflection`, trivial relocatability,
`std::inplace_vector`) are used behind `#ifdef` guards or in
non-header code only.

---

## The Four Safety Axioms

Rust satisfies seven safety axioms by construction. C++ satisfies
roughly two by default. We close four of the seven through coding
discipline and C++26 features.

### 1. InitSafe — `read(v) -> initialized(v)`

**Every value is initialized before first read.**

**Rule: Every struct field has a default member initializer (NSDMI).**

```cpp
// CORRECT — InitSafe by construction
struct TensorSlot {
  uint64_t offset_bytes = 0;
  uint64_t nbytes = 0;
  SlotId slot_id;                         // default ctor = UINT32_MAX (none)
  OpIndex birth_op;                       // default ctor = UINT32_MAX (none)
  ScalarType dtype = ScalarType::Undefined;
  bool is_external = false;
  uint8_t pad[3]{};                       // zero-init array
};

// WRONG — uninitialized fields cause hash instability, UB on read
struct TensorSlot {
  uint64_t offset_bytes;   // garbage on stack allocation
  uint64_t nbytes;         // garbage
  SlotId slot_id;          // OK (default ctor), but inconsistent style
};
```

**Why**: `alloc_obj<T>()` returns unzeroed Arena memory. Without NSDMI,
every field is garbage until explicitly assigned. NSDMI costs zero at
runtime — the compiler elides the dead store when the caller overwrites
immediately. But if any code path reads before writing, NSDMI catches it.

**Corollary**: `memset(ptr, 0, sizeof(T))` is acceptable as a *fast
path* for trivially-copyable structs (e.g. `alloc_node_()` zeros 64B),
but NSDMI must still be present to document the *intended* zero state.

**Corollary**: Padding bytes must be `uint8_t pad[N]{}` (zero-init), not
bare `uint8_t pad[N]`. Uninitialized padding contaminates hashes that
operate on the raw bytes of a struct.

### 2. TypeSafe — `(|- e : T) -> eval(e) : T`

**The type system prevents category errors at compile time.**

**Rule: Semantic IDs are strong types, never raw integers.**

```cpp
// CORRECT — compiler rejects mixing OpIndex with SlotId
void connect(OpIndex src, OpIndex dst, SlotId slot);
connect(OpIndex{3}, OpIndex{7}, SlotId{0});  // OK
connect(OpIndex{3}, SlotId{7}, OpIndex{0});  // COMPILE ERROR

// WRONG — silent argument swap
void connect(uint32_t src, uint32_t dst, uint32_t slot);
connect(3, 0, 7);  // compiles, wrong, silent data corruption
```

The `CRUCIBLE_STRONG_ID(Name)` macro in `Types.h` generates:
- `explicit Name(uint32_t)` — no implicit conversion from raw int
- `.raw()` — explicit unwrap for array indexing
- `.none()` / `.is_valid()` — named sentinel (UINT32_MAX)
- `operator<=>` — full comparison
- `explicit operator bool` — truthiness check without int promotion
- **No arithmetic** — must unwrap, compute, rewrap

Five strong IDs: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex`.

**Rule: Enums are `enum class`, never plain `enum`.**

```cpp
// CORRECT
enum class ScalarType : int8_t { Float = 6, Double = 7, Undefined = -1 };

// WRONG — pollutes namespace, implicitly converts to int
enum ScalarType { Float = 6, Double = 7 };
```

**Rule: Enum-to-integer conversion uses `std::to_underlying()`.**

```cpp
// CORRECT — type-safe, returns the underlying type
auto raw = std::to_underlying(dtype);  // int8_t for ScalarType

// WRONG — manually specifying the target type, can mismatch
auto raw = static_cast<uint8_t>(dtype);  // sign mismatch: ScalarType is int8_t
```

**Rule: Bitwise reinterpretation uses `std::bit_cast<T>()`.**

```cpp
// CORRECT — constexpr-safe, well-defined, no UB
double d = std::bit_cast<double>(payload);

// WRONG — UB in constexpr, unclear intent
double d = *reinterpret_cast<double*>(&payload);
```

**Rule: `Inst::dtype` is `ScalarType`, not `int8_t`.**

The Graph IR micro-op instruction uses the enum directly. Any place
where a dtype appears in a struct, it must be `ScalarType`, `DeviceType`,
or `Layout` — never the underlying integer type. The enum IS the type.

### 3. NullSafe — `deref(v) -> v != null`

**No null pointer dereference is possible in correct usage.**

**Rule: Pointer+count pairs have `std::span` accessors.**

```cpp
// CORRECT — span encapsulates the null check
struct TraceEntry {
  TensorMeta* input_metas = nullptr;
  uint16_t num_inputs = 0;

  [[nodiscard]] std::span<const TensorMeta> input_span() const {
    return {input_metas, num_inputs};
  }
};

// Usage: bounds-checked in debug, zero-cost in release
for (auto& m : entry.input_span()) { ... }  // empty span if nullptr+0

// WRONG — raw pointer arithmetic with no bounds check
for (uint16_t j = 0; j < entry.num_inputs; j++) {
  auto& m = entry.input_metas[j];  // null deref if input_metas == nullptr
}
```

`std::span(nullptr, 0)` is a valid empty span (C++ standard guarantee).
This means span accessors are safe even on default-initialized structs
where all pointers are nullptr and all counts are 0.

**Rule: Use `span::at()` for debug-mode bounds checking.**

Available since libc++ 22 (`__cpp_lib_span_at = 202311`). Throws
`std::out_of_range` on OOB. Use `operator[]` in release-critical paths.

**Rule: `[[nodiscard]]` on every query function.**

```cpp
// CORRECT — compiler warns if result is ignored
[[nodiscard]] uint32_t count_live() const;
[[nodiscard]] const SlotId* input_slots(NodeId id) const;

// WRONG — caller can silently ignore a null return
const SlotId* input_slots(NodeId id) const;  // forgetting to check = UB
```

**Rule: Allocation failures abort, never return null silently.**

```cpp
// CORRECT — fail-fast on OOM
MetaLog()
    : entries(static_cast<TensorMeta*>(
          std::malloc(CAPACITY * sizeof(TensorMeta)))) {
  if (!entries) [[unlikely]] std::abort();
}

// WRONG — returns null, caller might not check
MetaLog()
    : entries(static_cast<TensorMeta*>(
          std::malloc(CAPACITY * sizeof(TensorMeta)))) {}
```

Arena allocation (`alloc()`, `alloc_obj<T>()`, `alloc_array<T>(n)`)
internally does `malloc` which can fail. The Arena must abort on OOM.
A Crucible runtime that can't allocate arena memory is unrecoverable.

### 4. MemSafe — `free(v) -> !live(v)`

**No use-after-free, no double-free, no buffer overflow.**

**Rule: All graph/DAG memory is arena-allocated.**

```cpp
// CORRECT — arena owns all memory, freed when Arena dies
auto* node = arena.alloc_obj<RegionNode>();
auto* ops = arena.alloc_array<TraceEntry>(n);
// No delete, no free. Arena destructor frees everything.

// WRONG — individual allocation requires manual lifetime tracking
auto* node = new RegionNode();
// ... who deletes this? When? What if an exception fires?
```

The Arena is a bump-pointer allocator: allocation is a pointer increment
(~2ns), deallocation is destroying the Arena. No fragmentation, no
use-after-free (all pointers valid until Arena dies), no double-free
(no individual deallocation exists). This trades memory efficiency
(can't reclaim individual objects) for absolute safety and speed.

**Rule: Non-copyable, non-movable types use `= delete("reason")`.**

```cpp
// CORRECT — documents WHY the operation is forbidden
Arena(const Arena&) = delete("interior pointers would dangle");
Arena(Arena&&) = delete("interior pointers would dangle");
TraceRing(const TraceRing&) = delete("SPSC ring is pinned to thread pair");

// WRONG — deletes without explaining why
Arena(const Arena&) = delete;
```

The C++26 `= delete("reason")` feature (`__cpp_deleted_function = 202403`)
turns a mysterious compiler error into documentation.

**Rule: `static_assert` verifies struct layout.**

```cpp
static_assert(sizeof(GraphNode) == 64, "GraphNode must be 64 bytes");
static_assert(sizeof(Inst) == 8, "Inst must be 8 bytes");
static_assert(sizeof(Edge) == 12, "Edge must be 12 bytes");
static_assert(sizeof(TensorMeta) == 144, "TensorMeta layout check");
```

Layout assertions catch silent breakage from field reordering, padding
changes, or accidental additions. If a struct is designed to fit a
cache line (64B), the assert proves it at compile time.

**Rule: Use saturation arithmetic for size computations.**

```cpp
// CORRECT — clamps on overflow instead of wrapping
#include <numeric>  // std::mul_sat, std::add_sat
uint64_t nbytes = std::mul_sat(static_cast<uint64_t>(max_offset + 1),
                                static_cast<uint64_t>(element_size(dtype)));

// WRONG — silent overflow on large tensors
uint64_t nbytes = (max_offset + 1) * element_size(dtype);
```

Available via `__cpp_lib_saturation_arithmetic = 202311`.

---

## Performance Rules

### P1. Cache-Line Discipline

```cpp
// Atomic variables in SPSC buffers get their own cache line
alignas(64) std::atomic<uint64_t> head{0};  // producer writes
alignas(64) std::atomic<uint64_t> tail{0};  // consumer writes
```

False sharing between `head` (written by foreground) and `tail` (written
by background) would cause cache-line bouncing across cores. Each atomic
on its own 64B line eliminates this. The `alignas(64)` is the cheapest
performance win in the codebase.

### P2. Data Layout: SoA and Parallel Arrays

```cpp
// CORRECT — parallel arrays: each field in its own contiguous run
alignas(64) Entry entries[CAPACITY];       // 64B × 64K = 4MB
uint32_t meta_starts[CAPACITY];            // 4B × 64K = 256KB
uint64_t scope_hashes[CAPACITY];           // 8B × 64K = 512KB
uint64_t callsite_hashes[CAPACITY];        // 8B × 64K = 512KB

// WRONG — AoS: one big struct per entry wastes cache on unused fields
struct Entry {
  /* 64B core */ + uint32_t meta_start; + uint64_t scope_hash; ...
};  // 88B, crosses cache line boundary
```

When the iteration detector only needs `schema_hash + shape_hash`
(first 16B of each entry), the parallel-array layout means it reads
a contiguous 16B stripe. AoS would force loading 88B per entry and
wasting 72B of cache per access.

### P3. No Locks on the Hot Path

The foreground→background communication uses SPSC (Single Producer,
Single Consumer) ring buffers. No mutexes, no condition variables,
no lock-free CAS loops. The producer does:

```
entry[head & MASK] = data;
head.store(h + 1, release);
```

An SPSC ring touches one cache line per op with no kernel transition; a mutex lock/unlock pair adds at minimum a kernel-mediated futex round-trip. The structural difference is what matters; absolute numbers depend on the platform.

For shared data structures accessed by multiple threads (KernelCache),
use lock-free CAS on atomic slots:

```cpp
entry.content_hash.compare_exchange_strong(expected, content_hash, acq_rel);
```

### P4. Fixed-Capacity Structures

```cpp
static constexpr uint32_t CAPACITY = 1 << 16;  // 65536 entries
static constexpr uint32_t MASK = CAPACITY - 1;
```

Power-of-two capacities enable bitmask indexing (`& MASK`) instead of
modulo (`% CAPACITY`). Single AND instruction vs. integer division.
Pre-allocated at startup — no runtime resizing on the hot path.

### P5. Branching Hints

```cpp
if (h - t >= CAPACITY) [[unlikely]] {
  return false;  // ring full — rare slow path
}
```

Use `[[likely]]` / `[[unlikely]]` (C++20 attributes) at branch sites.
Never use `__builtin_expect` macros. The standard attributes are
portable, readable, and produce identical codegen.

### P6. Forced Inlining

```cpp
[[nodiscard]] CRUCIBLE_INLINE bool try_append(const Entry& e, ...) {
```

`CRUCIBLE_INLINE` = `__attribute__((always_inline)) inline`. Used only
on the foreground hot path (~5 functions total). Do not use everywhere —
excessive inlining bloats instruction cache and hurts performance.
The compiler's inliner is correct 99% of the time; override it only
when profiling shows a function-call overhead in a nanosecond-critical
path.

### P7. Immutable, Interned, Identity-Compared

```cpp
// Expr nodes: same structure → same pointer
// Comparison: a == b is pointer comparison (~1ns)
const Expr* a = pool.intern(Op::ADD, {x, y});
const Expr* b = pool.intern(Op::ADD, {x, y});
assert(a == b);  // same pointer — interned
```

Interning transforms structural equality into pointer equality. One
pointer comparison (~1ns) replaces recursive tree comparison (unbounded).
All Expr nodes are immutable and arena-allocated — no reference counting,
no garbage collection, no lifetime management.

### P8. Zero-Cost Abstractions for Strong IDs

```cpp
CRUCIBLE_STRONG_ID(OpIndex);
static_assert(sizeof(OpIndex) == sizeof(uint32_t));
```

The strong ID wrapper compiles to identical machine code as raw
`uint32_t`. No vtable, no heap allocation, no indirection. The
`explicit` constructor and `.raw()` accessor are zero-cost — they
exist only at the type level and vanish in codegen.

### P9. `constexpr` Everything Possible

```cpp
[[nodiscard]] constexpr uint8_t element_size(ScalarType t) {
  switch (t) { ... }
  std::unreachable();
}
```

`constexpr` functions are evaluated at compile time when arguments are
known. This catches UB (uninitialized reads, null derefs, OOB access)
as compiler errors. It also enables constant folding: `element_size(ScalarType::Float)`
becomes the literal `4` with zero runtime cost.

Use `std::unreachable()` (not `__builtin_unreachable()`) after exhaustive
switches. Standard, portable, caught by sanitizers in debug.

---

## Anti-Patterns

### A1. No `new` / `delete`

All graph/DAG/trace memory goes through the Arena. The only raw
`malloc`/`free` is inside `Arena::alloc()` itself and in `MetaLog`/
`KernelCache` (pre-allocated buffers with known lifetimes). If you're
writing `new` or `delete`, something is wrong.

### A2. No `std::string` in Data Structs

```cpp
// WRONG — std::string allocates on the heap, breaks memcpy/memset
struct ExternInfo {
  std::string python_kernel_name;
};

// CORRECT — arena-allocated, null-terminated, pointer only
struct ExternInfo {
  const char* python_kernel_name = nullptr;
};
```

All strings are arena-allocated via `copy_string_()`. A `const char*`
is 8B and trivially copyable. `std::string` is 32B (SSO) and
non-trivially-copyable, breaking `memset`-based initialization and
cache-line layout guarantees.

### A3. No `std::shared_ptr`

Arena-allocated memory has a single owner (the Arena). Reference
counting is unnecessary and expensive (~10ns atomic increment per copy).
If you need shared access, use raw pointers — the Arena guarantees
all pointers are valid until it's destroyed.

### A4. No `virtual` in Data Structs

```cpp
// WRONG — vtable pointer adds 8B, breaks 64B cache-line layout
struct GraphNode {
  virtual ~GraphNode() = default;
  // ... now 72B, no longer fits one cache line
};

// CORRECT — kind enum + static_cast for dispatch
struct TraceNode {
  TraceNodeKind kind;  // 1B enum
};
auto* region = static_cast<RegionNode*>(node);  // zero-cost downcast
```

A vtable pointer costs 8 bytes per object and forces indirection on
every method call. For 23K nodes, that's 180KB wasted plus constant
cache misses on the vtable. Use a `kind` enum and `static_cast` — the
kind check is a single byte comparison, the cast is free.

### A5. No Exceptions on Hot Paths

```cpp
// WRONG — exception setup has nonzero cost even when not thrown
try {
  ring.try_append(entry);
} catch (const std::exception& e) { ... }

// CORRECT — return bool, let caller decide
[[nodiscard]] bool try_append(const Entry& e) {
  if (full) [[unlikely]] return false;
  // ...
  return true;
}
```

Exceptions are acceptable for truly unrecoverable errors during
initialization (Arena creation failure). For hot-path error signaling,
use return values. For impossible states, use `assert` (debug) or
`std::unreachable()` (release).

### A6. No `std::unordered_map` for Hot Lookups

```cpp
// WRONG — heap allocations per bucket, pointer chasing, poor cache
std::unordered_map<uint64_t, CompiledKernel*> cache;

// CORRECT — open-addressing, flat array, SIMD-accelerated probe
class KernelCache {  // SwissCtrl.h for SIMD control bytes
  Entry* table_;     // calloc'd flat array, power-of-two capacity
};
```

The standard `unordered_map` uses separate chaining (linked-list
buckets), which means pointer chasing and poor cache locality. For
performance-critical maps, use open-addressing with Swiss table
control bytes (SIMD-parallel probe).

`std::flat_map` (available: `__cpp_lib_flat_map = 202511`) is
appropriate for small sorted maps with frequent iteration. Not for
hash-based O(1) lookup.

### A7. No Implicit Conversions

```cpp
// WRONG — implicit conversion from int allows bugs
struct NodeId { uint32_t v; };  // no explicit keyword

// CORRECT — explicit constructor requires named construction
struct NodeId {
  constexpr explicit NodeId(uint32_t val) : v(val) {}
  // ...
};
```

Every constructor that takes a single argument must be `explicit`
unless implicit conversion is specifically desired and documented.
The `CRUCIBLE_STRONG_ID` macro enforces this.

---

## C++26 Feature Map — What We Use From Where

Three compilers make different bets. We pick the best of each.

### Baseline Features (all three compilers)

These are safe to use unconditionally in any header:

| Feature | Crucible usage |
|---------|----------------|
| NSDMI (`= value` on fields) | InitSafe: every struct field has a default |
| `= delete("reason")` | Document why copies/moves forbidden |
| `std::span` | Safe pointer+count accessors |
| `std::to_underlying()` | Safe enum→int conversion |
| `std::unreachable()` | Impossible branches after switches |
| `std::bit_cast<T>()` | Type-safe bitwise reinterpretation |
| `std::expected<T,E>` | Typed error returns for fallible ops |
| `std::countr_zero()` | Branchless lowest-set-bit in BitMask |
| `std::saturation_arithmetic` | Overflow-safe size computations |
| `operator<=>` | Defaulted comparison in strong IDs |
| `constexpr` (extended) | Compile-time UB detection |
| `[[likely]]`/`[[unlikely]]` | Branch hints on hot paths |
| `[[nodiscard]]` | All query functions |
| `alignas(64)` | Cache-line isolation for SPSC atomics |
| Pack indexing `Ts...[0]` | Direct type-safe pack access |
| Structured binding packs | Safer destructuring |

### Clang 22 Exclusive (libc++ 22)

Available only in the `default`/`release` presets. Guard with `#ifdef`
or use only in non-header code:

| Feature | Macro | Crucible usage |
|---------|-------|----------------|
| Trivial relocatability | `__cpp_trivial_relocatability = 202502` | `static_assert` that Arena memcpy patterns are sound |
| `std::span::at()` | `__cpp_lib_span_at = 202311` | Debug-mode bounds checking |
| `std::flat_map` | `__cpp_lib_flat_map = 202511` | Cache-friendly sorted containers (not for hot hash lookups) |

**Why Clang leads here:** Trivial relocatability (P2786) grew from
Clang's existing `[[clang::trivial_abi]]` extension. `std::flat_map`
shipped in libc++ 22 before libstdc++. `span::at()` is a libc++ 22
library addition.

### GCC 16 Exclusive (libstdc++ 16)

Available only in the `gcc16` preset. Guard with `#ifdef` or use
only in tests/tools:

| Feature | Macro | Crucible usage |
|---------|-------|----------------|
| **Static reflection** | `__cpp_impl_reflection = 202506` | Auto-generated hash, serialize, compare for all structs. Requires `-freflection`. |
| **Expansion statements** | `__cpp_expansion_statements = 202506` | `template for` over packs — reflection iteration |
| **`std::inplace_vector`** | `__cpp_lib_inplace_vector = 202406` | Bounds-checked fixed-capacity arrays (planned: TensorMeta sizes/strides) |
| **constexpr exceptions** | `__cpp_constexpr_exceptions = 202411` | Meaningful compile-time errors from consteval functions |
| `std::indirect<T>` | `__cpp_lib_indirect = 202502` | Value-semantic heap pointer (limited use — Arena is faster) |
| `std::polymorphic<T>` | `__cpp_lib_polymorphic = 202502` | Value-semantic polymorphism (avoid — kind enum is zero-cost) |
| `std::function_ref` | `__cpp_lib_function_ref = 202306` | Lightweight non-owning callable reference |
| `std::copyable_function` | `__cpp_lib_copyable_function = 202306` | Copyable type-erased callable |
| `<debugging>` | `__cpp_lib_debugging = 202403` | `std::breakpoint()`, `std::is_debugger_present()` |

**Why GCC leads here:** Reflection (P2996) was co-authored by
EDG/Bloomberg contributors who collaborated with the GCC team.
`std::inplace_vector` shipped in libstdc++ 16 before libc++.
Expansion statements (P1306) are a prerequisite for usable reflection.

### Neither Has Yet

| Feature | Status | Impact when available |
|---------|--------|----------------------|
| Contracts (`pre`/`post`/`assert`) | No compiler ships it | Compiler-checked preconditions — biggest single safety win |
| Pattern matching (`inspect`) | Committee stage | Exhaustive matching on enums — eliminates missed cases |
| `std::simd` | GCC experimental only | Replaces SwissCtrl.h's 5 SIMD backends with one |
| Lifetime annotations | Not proposed for C++ | Rust's `'a` — would prove Arena borrowing safe |

### Feature Decision Matrix

When choosing between alternatives:

| Need | Clang 22 choice | GCC 16 choice | Baseline choice |
|------|-----------------|---------------|-----------------|
| "Is memcpy safe for this type?" | `static_assert(is_trivially_relocatable_v<T>)` | Not available | `static_assert(is_trivially_copyable_v<T>)` |
| Auto-generate struct hash | Not available | `reflect_hash<T>(obj)` via `<meta>` | Hand-written `hash()` with NSDMI ensuring all fields initialized |
| Fixed-capacity array | `T arr[N]{}` + separate count | `std::inplace_vector<T, N>` | `T arr[N]{}` + separate count |
| Bounds-checked span access | `span.at(i)` | `span[i]` (no `.at()` in libstdc++) | `assert(i < span.size()); span[i]` |
| Non-owning callable | Template parameter | `std::function_ref<Sig>` | Template parameter |
| Sorted flat container | `std::flat_map` | `std::flat_map` (different version) | `std::flat_map` (both have it) |

### Conditional Feature Guard Pattern

```cpp
// Trivial relocatability — Clang 22 only
#if __has_cpp_attribute(__cpp_trivial_relocatability)
static_assert(std::is_trivially_relocatable_v<GraphNode>,
              "GraphNode must be trivially relocatable for Arena memcpy");
#endif

// Reflection — GCC 16 with -freflection only
#ifdef __cpp_impl_reflection
#include <meta>
template <typename T>
uint64_t reflect_hash(const T& obj) { /* ... */ }
#endif

// inplace_vector — GCC 16 libstdc++ only
#ifdef __cpp_lib_inplace_vector
#include <inplace_vector>
using Dims = std::inplace_vector<int64_t, 8>;
#else
// Fallback: raw array + count
struct Dims { int64_t data[8]{}; uint8_t n = 0; };
#endif
```

## C++26 Features We Deliberately Avoid

| Feature | Available? | Why we don't use it |
|---------|-----------|---------------------|
| `std::format` / `std::print` | Both | Not in hot paths. `fprintf` is fine for debug output. |
| `std::ranges` (pipelines) | Both | Range adaptor chains add compile time. Raw loops are clearer for simple iteration. |
| `std::mdspan` | Both | Our tensor metadata is fixed 8-dim arrays, not arbitrary multi-dimensional views. |
| `std::optional` | Both | Arena pointers use nullptr as "absent". Optional adds 1 byte overhead per value. |
| `std::variant` | Both | Kind enum + static_cast is zero-cost. Variant adds type-index storage + visitation overhead. |
| `std::indirect`/`polymorphic` | GCC 16 | Arena allocation is faster. Value-semantic heap wrappers add per-object overhead. |
| RTTI (`dynamic_cast`, `typeid`) | Both | Disabled at compile level. Zero runtime type info. Use kind enums. |
| Exceptions (hot path) | Both | `-fno-exceptions` in release. assert/abort for unrecoverable. Return values for expected failures. |

---

## Concurrency Model

Two threads. Period.

```
Foreground (hot):  record ops at ~5ns each via TraceRing
Background (warm): drain ring, build TraceGraph, DAG, memory plan, compile
```

Communication is strictly through SPSC ring buffers (TraceRing, MetaLog).
No shared mutable state except:
- `KernelCache` — lock-free CAS on atomic slots (background writes, foreground reads)
- `RegionNode::compiled` — atomic pointer (background writes, foreground reads)

**Rule: Never acquire a lock on the foreground thread.**

If a new data structure needs cross-thread access, it must use either:
1. SPSC ring (one writer, one reader, known at design time)
2. Atomic CAS on a flat array (open-addressing pattern)
3. Atomic pointer swap (single-word publish)

---

## Struct Design Checklist

When adding a new struct:

- [ ] Every field has NSDMI (= default value or `{}`)
- [ ] `static_assert(sizeof(T) == N)` if layout matters
- [ ] Semantic IDs use strong types (OpIndex, SlotId, NodeId, etc.)
- [ ] Pointer+count fields have a `std::span` accessor method
- [ ] All accessors are `[[nodiscard]]`
- [ ] Copy/move is explicitly deleted with a reason, or defaulted
- [ ] Padding bytes are `uint8_t pad[N]{}` (zero-initialized)
- [ ] If arena-allocated: verify trivially copyable or placement-new
- [ ] Enums are `enum class` with explicit underlying type
