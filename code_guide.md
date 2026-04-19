# Crucible Code Guide

*The canonical reference for writing Crucible code. Every rule has a cost-of-violation, a compiler-enforcement mechanism, and a discipline fallback. Nothing is style; everything is measured.*

Design target: **~5 ns/op foreground recording, ~2 ns shadow-handle dispatch, zero UB, bit-identical across hardware under BITEXACT recipes**. Every rule below serves one or more of those targets.

---

## I. Toolchain

| Preset    | Compiler              | Role                                          |
|-----------|-----------------------|-----------------------------------------------|
| `default` | GCC 16.0.1 (rawhide)  | Primary dev. Debug. Contracts + reflection.   |
| `release` | GCC 16.0.1            | Production. `-O3 -march=native -flto=auto -DNDEBUG` |
| `bench`   | GCC 16.0.1            | Release + `CRUCIBLE_BENCH=ON`                 |
| `tsan`    | GCC 16.0.1            | ThreadSanitizer (mutually exclusive with ASan)|
| `verify`  | GCC 16.0.1            | + Z3 formal verification suite                |

**GCC 16 is the only supported compiler.** Crucible's safety axioms structurally depend on features that exist only there:

- **Contracts (P2900R14)** — the InitSafe / NullSafe / TypeSafe / MemSafe enforcement mechanism. GCC 16 exclusive.
- **Erroneous behavior for uninit reads (P2795R5)** — InitSafe ceiling. GCC 16 exclusive.
- **Reflection (P2996R13)** — auto-generated hashing and serialization. GCC 16 exclusive.
- **Expansion statements `template for` (P1306R5)** — reflection iteration. GCC 16 exclusive.
- **`constexpr` exceptions (P3068R5)** — compile-time IR verifier. GCC 16 exclusive.
- **Partial program correctness (P1494R5)** — contract violation without UB. GCC 16 exclusive.

GCC 15 cannot compile the codebase. Clang 22 cannot compile the codebase. No fallback exists, and none is pursued — the axioms are load-bearing and non-negotiable.

```bash
cmake --preset default && cmake --build --preset default && ctest --preset default
# For release perf:
cmake --preset release && cmake --build --preset release
# For race detection:
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

When GCC 17 ships, re-evaluate. When Clang eventually ships contracts + reflection + P2795R5, it joins as a parity compiler. Until then: GCC 16 only.

---

## II. The Eight Safety Axioms

Every edit checks all eight. No exceptions. No "fix it later." The axiom violated most recently is the one most likely to be violated next.

### 1. InitSafe — `read(v) ⇒ initialized(v)`

**Cost of violation:** undefined behavior, heisenbug, info leak.

**Compiler enforcement:** P2795R5 erroneous behavior + `-ftrivial-auto-var-init=zero` + `-Werror=uninitialized` + `-Wanalyzer-use-of-uninitialized-value`.

**Discipline:**
- Every struct field has NSDMI (non-static data-member initializer): `T field = sentinel_value;`.
- Padding is explicit: `uint8_t pad[N]{};` never bare arrays.
- Stack aggregates: `RegionNode r{};` always, never `RegionNode r;`.
- C arrays of strong IDs: default ctor of the strong ID initializes (e.g. `MetaIndex::none()` → `UINT32_MAX`).
- `memset` only as fast-path zeroing AFTER NSDMI already documents zero semantics.

```cpp
// ✓ CORRECT
struct TensorSlot {
  uint64_t    offset_bytes = 0;
  SlotId      slot_id;                          // default ctor = UINT32_MAX
  ScalarType  dtype        = ScalarType::Undefined;
  uint8_t     pad[3]{};                         // zero-init padding
};

// ✗ WRONG — reading `offset_bytes` before assignment reads zero by accident,
//           not by guarantee. Replace with NSDMI.
struct TensorSlot {
  uint64_t    offset_bytes;
  SlotId      slot_id;
  ScalarType  dtype;
  uint8_t     pad[3];
};
```

### 2. TypeSafe — `(⊢ e : T) ⇒ eval(e) : T`

**Cost of violation:** silent parameter swap, implicit conversion bug, type confusion.

**Compiler enforcement:** `-Werror=conversion -Werror=sign-conversion -Werror=arith-conversion -Werror=enum-conversion -Werror=old-style-cast -fno-rtti`.

**Discipline:**
- Every semantic value is a strong type. No raw `uint32_t` for anything with meaning.
- IDs: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex`, `KernelId` (all `CRUCIBLE_STRONG_ID(Name)` → `explicit(uint32_t)`, `.raw()`, `.none()`, `<=>`, no arithmetic).
- Hashes: `SchemaHash`, `ShapeHash`, `ContentHash` (all `CRUCIBLE_STRONG_HASH(Name)`).
- Enums: `enum class` with explicit underlying type. Convert via `std::to_underlying()` only.
- Bit reinterpretation: `std::bit_cast<T>()` only. `reinterpret_cast` is BANNED.
- Arithmetic: `std::add_sat` / `std::sub_sat` / `std::mul_sat` for all size/offset math.
- Casts: C-style cast is a compile error. `const_cast` is BANNED.

```cpp
// ✓ CORRECT — silent parameter swap impossible
void connect(OpIndex src, OpIndex dst, SlotId slot);

// ✗ WRONG — caller can swap src/dst/slot silently
void connect(uint32_t src, uint32_t dst, uint32_t slot);
```

### 3. NullSafe — `deref(v) ⇒ v ≠ null`

**Cost of violation:** crash (best case) or silent wrong answer.

**Compiler enforcement:** `-Werror=null-dereference -Werror=nonnull -Werror=nonnull-compare -Wanalyzer-null-dereference -Wanalyzer-possible-null-dereference`, contracts `pre(p != nullptr)` on all pointer params.

**Discipline:**
- `[[nodiscard]]` on every query returning bool or pointer.
- `(ptr, count)` pairs → `std::span` accessor. `span(nullptr, 0)` is a valid empty span.
- `alloc_array<T>(0)` returns `nullptr` AND sets count to 0 — both must agree.
- Iterate via span, never raw `(ptr, count)` loop.
- OOM → `std::abort()`. Crucible never runs on systems where OOM is recoverable.

```cpp
// ✓ CORRECT
[[nodiscard]] std::span<const TensorMeta> input_span() const {
    return {input_metas, num_inputs};
}

// Boundary function: contract-enforce non-null
void process(const TraceEntry* entry)
    pre (entry != nullptr)
{
    // body can assume entry is non-null
}
```

### 4. MemSafe — `free(v) ⇒ ¬live(v)`

**Cost of violation:** use-after-free, double-free, memory corruption, RCE.

**Compiler enforcement:** `-fsanitize=address` in debug, `-Werror=use-after-free=3 -Werror=free-nonheap-object -Werror=dangling-pointer=2 -Werror=mismatched-new-delete -Wanalyzer-use-after-free -Wanalyzer-double-free`. `-fno-exceptions` eliminates the whole class of "destructor during stack unwind" bugs.

**Discipline:**
- All graph/DAG memory lives in an Arena (bump pointer, ~2 ns alloc, no fragmentation, no UAF). Arena bulk-frees at epoch boundary.
- No `new` / `delete`. No `malloc` / `free` on hot path.
- No `std::shared_ptr`. No `std::unique_ptr` on hot path (only for top-level ring/MetaLog buffers).
- `= delete("reason")` on copy and move of non-value types. WITH a reason string.
- `static_assert(sizeof(T) == N)` on layout-critical structs.
- Arena type punning uses `std::start_lifetime_as<T>()` (C++23) — NOT `reinterpret_cast`.

```cpp
// ✓ CORRECT
class Arena {
    Arena(const Arena&) = delete("interior pointers would dangle");
    Arena(Arena&&)      = delete("interior pointers would dangle");

    template<typename T>
    T* alloc_obj() {
        void* raw = bump(sizeof(T), alignof(T));
        return std::start_lifetime_as<T>(raw);  // C++23, not reinterpret_cast
    }
};
```

### 5. BorrowSafe — no aliased mutation

**Cost of violation:** data race, torn read, spooky action at a distance.

**Compiler enforcement:** limited in C++ (no borrow checker). Discipline-enforced with review + `-fsanitize=thread` in CI.

**Discipline:**
- SPSC = one writer, one reader, per ring buffer. Never multi-producer without explicit atomic sync.
- Document ownership in a comment at the struct level: "owned by fg thread", "owned by bg thread", "SPSC via acquire/release".
- No shared mutable state except through atomics.
- Arena gives out raw pointers — once given, the arena's bump cursor is opaque to the holder; no one else can reference that region.

### 6. ThreadSafe — acquire/release only

**Cost of violation:** reordered writes, lost wakeups, ARM-specific heisenbugs that don't repro on x86.

**Compiler enforcement:** `-fsanitize=thread` catches races. Discipline for ordering.

**Discipline:**
- Foreground owns TraceRing head + MetaLog head.
- Background owns TraceRing tail + MetaLog tail.
- Cross-thread signals: atomic acquire/release ONLY. Never `memory_order_relaxed`.
- `compare_exchange_strong` with `acq_rel` on RMW.
- Spin on atomic load with `CRUCIBLE_SPIN_PAUSE` (→ `_mm_pause` on x86, `yield` on ARM).
- BANNED on hot path: `sleep_for`, `yield`, `futex`, `eventfd`, `condition_variable`, `atomic::wait/notify`, any timeout.

```cpp
// ✓ CORRECT
producer.store(new_head, std::memory_order_release);

while (consumer.load(std::memory_order_acquire) != expected) {
    CRUCIBLE_SPIN_PAUSE;  // 10-40 ns via MESI cache-line invalidation
}

head.compare_exchange_strong(exp, des, std::memory_order_acq_rel);
```

Relaxed = ARM reordering = race. On x86 it's the same MOV as acquire/release — the cost of always using acquire/release is zero on our target platforms, and the safety is real.

### 7. LeakSafe — bounded resource lifetime

**Cost of violation:** DoS over time, process growth, gradual degradation.

**Compiler enforcement:** `-fsanitize=leak` (integrated into ASan on Linux), `-Wanalyzer-malloc-leak -Wanalyzer-fd-leak`.

**Discipline:**
- Arena bulk-frees graph memory (no per-object free).
- `std::unique_ptr` for long-lived owned buffers (TraceRing, MetaLog).
- `bg_` thread member declared LAST in containing struct — destroyed first, joins before other members die.
- No Rc cycles (we don't use Rc anyway).
- Cipher tiers have explicit eviction: hot (LRU), warm (age), cold (S3 lifecycle policy).

### 8. DetSafe — `same(inputs) ⇒ same(outputs)`

**Cost of violation:** replay breaks, bit-exactness CI reddens, cross-vendor equivalence lost.

**Compiler enforcement:** no `-ffast-math` family. `-ffp-contract=on` (safe: FMA within a statement only). Pinned FTZ via recipe.

**Discipline:**
- DAG fixes execution order (topological sort with hash-based tiebreak).
- Memory plan fixes addresses (pool_base + offset, content-addressed).
- Philox4x32 RNG: counter-based, platform-independent. Zero RNG state anywhere.
- KernelCache keyed on `(content_hash, device_capability)`.
- Reduction topology: pinned binary tree sorted by UUID for BITEXACT recipes.
- No hash-table iteration order dependencies. Sort keys before iterating.
- No pointer-based ordering. Events have `(cycle, kind, sequence_number)`.

---

## III. C++26 Language Features — Opt Matrix

### Opt IN

| Feature | Paper | Usage |
|---|---|---|
| Contracts (`pre`/`post`/`contract_assert`) | P2900R14 | Every boundary function. Hot-path TUs use `contract_evaluation_semantic=ignore` |
| Erroneous behavior for uninit reads | P2795R5 | Foundation of InitSafe axiom |
| Partial program correctness | P1494R5 | Contract violation = `std::terminate`, not UB |
| Trivial infinite loops not UB | P2809R3 | Closes LLVM `while(1){}` → unreachable optimization |
| Remove UB from lexing | P2621R2 (DR) | Lexer no longer has UB corners; applied retroactively by GCC |
| Preprocessing never undefined | P2843R3 | Preprocessor UB corners removed |
| On the ignorability of standard attributes | P2552R3 (DR) | Clarified attribute ignorability — predictable behavior |
| Disallow returning ref to temporary | P2748R5 | Compile error for dangling ref |
| Deleting ptr-to-incomplete ill-formed | P3144R2 | Compile error for silent UB |
| Reflection | P2996R13 | `reflect_hash<T>`, auto-serializers (`-freflection`) |
| Annotations for reflection | P3394R4 | Tag fields for custom codegen |
| Splicing a base class subobject | P3293R3 | Reflect across hierarchy |
| Function parameter reflection | P3096R12 | Auto-generate dispatch from schema |
| `define_static_{string,object,array}` | P3491R3 | Compile-time constexpr static arrays (CKernelId tables) |
| Error handling in reflection | P3560R2 | Structured compile-time errors |
| Expansion statements (`template for`) | P1306R5 | Iterate reflected members without macros |
| `constexpr` exceptions | P3068R5 | IR verifier throws at compile time, zero runtime cost |
| `constexpr` structured bindings | P2686R4 | constexpr ergonomics |
| `constexpr` placement new | P2747R2 | Constexpr arena patterns |
| `constexpr` cast from void* | P2738R1 | Constexpr helpers |
| Pack indexing `Ts...[N]` | P2662R3 | Variadic template access |
| Structured bindings introduce pack | P1061R10 | Clean `auto [first, ...rest] = tup;` |
| Attributes for structured bindings | P0609R3 | `[[maybe_unused]]` per binding |
| Placeholder variable `_` | P2169R4 | Explicit discard |
| User-generated static_assert messages | P2741R3 | Better compile errors |
| `= delete("reason")` | P2573R2 | Every banned copy/move |

### Opt IN — from C++23

| Feature | Paper | Usage |
|---|---|---|
| Explicit lifetime management | P2590R2 | `std::start_lifetime_as<T>` fixes arena type-punning UB |
| Deducing `this` | P0847R7 | CRTP without CRTP boilerplate |
| Portable assumptions (`[[assume]]`) | P1774R8 | Compile-time invariants to optimizer |
| `static operator()` | P1169R4 | Stateless callable with no `this` |
| Relaxed `constexpr` restrictions | P2448R2 | More compile-time opportunities |
| `if consteval` | P1938R3 | Switch behavior when compile-time vs runtime |
| `consteval` propagates up | P2564R3 | Cleaner consteval chains |
| `#embed` | P1967R14 | Embed binary blobs at compile time |

### Opt OUT

| Feature | Ban via | Reason |
|---|---|---|
| Exceptions | `-fno-exceptions` | Setup cost even unthrown; unwind tables in icache; use `std::expected` or `abort()` |
| RTTI | `-fno-rtti` | Vtable bloat; use `kind` enum + `static_cast` |
| Coroutines on hot path | discipline | Heap allocation, unpredictable latency |
| `volatile` for concurrency | P1152R4 deprecated | `volatile` does not order; use `std::atomic` |
| `[=]` capturing `this` | P0806R2 deprecated; `-Werror=deprecated-this-capture` | Lifetime footgun |
| `memory_order::consume` | P3475R2 deprecated; `-Werror=deprecated-declarations` | Compilers promote to acquire anyway |
| VLAs (`int arr[n]`) | `-Werror=vla` | Stack UB |
| C-style casts | `-Werror=old-style-cast` | Silent UB conversions |
| `reinterpret_cast` | code review grep-ban | Use `std::bit_cast<T>` |
| `const_cast` | `-Werror=cast-qual` | Casting away const is almost always wrong |
| Static downcast (`static_cast<Derived*>`) | N/A — no inheritance in data types | We have no `virtual` |
| Implicit narrowing | `-Werror=conversion -Werror=sign-conversion` | Silent truncation |
| Float `==` | `-Werror=float-equal` | Use `std::abs(a-b) < eps` or exact bit compare |
| Uniform init with `initializer_list` surprises | discipline | `std::vector<int>{10,20}` is 2 elements, not 30; we ban vector anyway |
| Most vexing parse (`Widget w();`) | discipline | Always `Widget w{};` |
| Trigraphs, `register`, `auto_ptr` | already removed from standard | — |

---

## IV. Library Types — Opt Matrix

### Opt IN

| Type | Purpose |
|---|---|
| `std::inplace_vector` (C++26) | Bounded fixed-capacity container, no heap |
| `std::function_ref` (C++26) | Non-owning callable for Mimic callbacks |
| `std::mdspan` (C++23) | Multi-dim span for tensor metadata views |
| `std::expected<T,E>` (C++23) | Error paths without exceptions |
| `std::flat_map` / `std::flat_set` (C++23) | Sorted vector container, cache-friendly |
| `std::move_only_function` (C++23) | Move-only callable |
| `std::start_lifetime_as` (C++23) | Arena type punning correctness |
| `std::bit_cast` (C++20) | The ONLY type-pun primitive allowed |
| `std::add_sat` / `mul_sat` / `sub_sat` (C++26) | Saturation arithmetic at size-math sites |
| `std::breakpoint()` (C++26) | Hardware breakpoint for debug asserts |
| `std::unreachable()` (C++23) | After exhaustive switch to eliminate default branch |
| `std::countr_zero` / `popcount` (C++20) | Bit manipulation primitives |
| `std::span` (C++20) | Pointer+count replacement |
| `std::jthread` (C++20) | Auto-joining thread, no destructor-terminate |

### Opt OUT

| Type | Why |
|---|---|
| `std::function` | Type-erased, heap-allocated, indirect call; use `function_ref` or templated `auto&&` |
| `std::any` | Heap + type erasure |
| `std::regex` | 10-100× slower than alternatives, throws, huge code size |
| `std::async` | Launches threads, heap, ambiguous semantics |
| `std::promise`/`std::future` | Heap + mutex; use atomic flags + SPSC signal |
| `std::shared_mutex` | Often slower than plain mutex; we use neither on hot path |
| `std::thread` raw | Use `std::jthread` (C++20) |
| `std::endl` | Flushes; use `'\n'` |
| `std::vector<bool>` | Proxy iterators, not really a container; use `std::bitset<N>` |
| `std::cout` / `std::cerr` on hot path | Synced with stdio; use `fprintf(stderr, ...)` for debug |
| `std::printf` / `std::format` on hot path | Formatting cost; reserve for debug paths |
| `std::rand()` / `std::srand()` | Global state, poor quality; Philox only |
| `std::this_thread::sleep_for` | Already banned per ThreadSafe |
| `std::string` in hot-path structs | 32 B SSO, breaks memcpy; `const char*` + explicit lifetime |
| `std::unordered_map` | Chained buckets, pointer chasing; Swiss table open-addressing |
| `std::map` | Red-black tree, pointer chasing; `std::flat_map` |
| `std::shared_ptr` | Atomic refcount, heap; arena raw pointers |
| `std::ranges` pipelines | Compile-time bloat, debug-build performance cliff |
| `std::optional` on hot path | +1 B tag + branch; `nullptr` or `kind` enum sentinel |
| `std::variant` on hot path | Visitor dispatch; `kind` enum + `static_cast` |
| Bitfields | Often slower than manual shift+mask; manual bits |
| `std::codecvt` | Deprecated, broken API |

---

## V. Compiler Flags

### Common (every build)

```
-std=c++26                           strict C++26, no GNU dialect drift
-fcontracts                          P2900 contracts
-freflection                         P2996 reflection
-fno-exceptions                      eliminate unwind tables
-fno-rtti                            eliminate vtables of typeinfo
-fno-strict-overflow                 don't optimize assuming signed overflow impossible
-fno-delete-null-pointer-checks      don't optimize away null checks
-fno-math-errno                      math functions don't set errno (faster, vec-friendly)
-ffp-contract=on                     FMA within a statement (safe for BITEXACT)
-ftrivial-auto-var-init=zero         P2795R5 — zero-init stack, kills InitSafe class
-fstack-protector-strong             stack canaries
-fstack-clash-protection             stack clash mitigation (~0.1% cost)
-fharden-control-flow-redundancy     GCC CFG hardening
-fcf-protection=full                 Intel CET / ARM BTI+PAC
-fno-omit-frame-pointer              readable traces (<1% cost)
-fno-plt                             direct calls
-fno-semantic-interposition          allow cross-TU inlining
-fvisibility=hidden                  hidden symbols = more inlining
-fvisibility-inlines-hidden          same for inline
-ffunction-sections                  one function per section (for --gc-sections)
-fdata-sections                      one var per section
-fno-common                          no tentative definitions
-fstrict-flex-arrays=3               strict flex array rules
-fsized-deallocation                 sized `delete` overloads
-D_FORTIFY_SOURCE=3                  glibc bounds checks
-D_GLIBCXX_ASSERTIONS                libstdc++ cheap asserts (release-safe)
```

### Debug preset

```
Common flags +
-Og -g                                debuggable but fortify/analyzer friendly
-fcontract-evaluation-semantic=enforce   check + terminate
-fsanitize=address                    ASan + LeakSan
-fsanitize=undefined,bounds-strict    UBSan + strict bounds
-fsanitize=shift-exponent,pointer-overflow  extra UBSan
-fno-sanitize-recover=all             abort on first violation
-D_GLIBCXX_DEBUG                      heavy container checks
```

### Release preset

```
Common flags +
-O3
-march=native -mtune=native
-DNDEBUG
-g                                    keep frame info for profiling
-flto=auto                            whole-program LTO, ~10-20% typical win
-fcontract-evaluation-semantic=observe  log but continue (or ignore on hot TUs)
-ftree-vectorize                      on by default at -O3
-fvect-cost-model=unlimited           aggressive auto-vec
-mprefer-vector-width=512             AVX-512 where HW supports
-fipa-pta                             interprocedural pointer analysis
-fgraphite-identity                   polyhedral loop framework
-floop-nest-optimize                  Graphite loop optimizer
-fno-trapping-math                    assume FP doesn't trap (vec-friendly)
+ PGO artifacts from bench workload
```

### Verify preset

Release flags + Z3 SMT solver + `-fcontract-evaluation-semantic=enforce` + `-fanalyzer`.

### NEVER (kills determinism or wastes perf)

```
-ffast-math                       breaks IEEE 754 — kills BITEXACT
-funsafe-math-optimizations       same
-fassociative-math                reorders FP
-fno-signed-zeros                 breaks IEEE
-ffinite-math-only                assumes no NaN/Inf
-ffp-contract=fast                cross-statement FMA (bits can differ)
-fno-strict-aliasing              disables TBAA, big perf loss
-fshort-enums                     non-portable ABI
-fpermissive                      accepts non-conforming code
-ftrapv                           traps on signed overflow — expensive
-fwrapv                           ~1% global perf cost; use `std::*_sat` at sites
-funroll-all-loops                bloats icache; per-loop pragma instead
```

---

## VI. Warnings Promoted to Errors

All UB-adjacent and lifetime-adjacent warnings are hard errors.

```
-Werror=return-type
-Werror=uninitialized
-Werror=maybe-uninitialized
-Werror=null-dereference
-Werror=nonnull
-Werror=nonnull-compare
-Werror=shift-count-overflow
-Werror=shift-count-negative
-Werror=shift-negative-value
-Werror=stringop-overflow
-Werror=stringop-truncation
-Werror=array-bounds
-Werror=restrict
-Werror=dangling-pointer=2
-Werror=use-after-free=3
-Werror=free-nonheap-object
-Werror=alloc-size-larger-than
-Werror=mismatched-new-delete
-Werror=mismatched-dealloc
-Werror=implicit-fallthrough
-Werror=format-security
-Werror=format-nonliteral
-Werror=aggressive-loop-optimizations
-Werror=aliasing
-Werror=cast-qual
-Werror=conversion
-Werror=sign-conversion
-Werror=arith-conversion
-Werror=enum-conversion
-Werror=old-style-cast
-Werror=float-equal
-Werror=vla
-Werror=type-limits
-Werror=switch
-Werror=switch-default
-Werror=pessimizing-move
-Werror=redundant-move
-Werror=self-move
-Werror=deprecated-copy
-Werror=deprecated-copy-dtor
-Werror=return-local-addr
```

Non-error warnings (informational, not yet hard):
- `-Wpadded` (off — we accept padding where it occurs)
- `-Wsuggest-*` (off — suggestions, not errors)

---

## VII. Attributes and Macros

`Platform.h` provides these. Use them deliberately.

```cpp
// ── Inlining control ──────────────────────────────
#define CRUCIBLE_INLINE       [[gnu::always_inline]] inline
#define CRUCIBLE_HOT          [[gnu::hot, gnu::always_inline]] inline
#define CRUCIBLE_COLD         [[gnu::cold, gnu::noinline]]
#define CRUCIBLE_FLATTEN      [[gnu::flatten]]      // inline all calls inside
#define CRUCIBLE_NOINLINE     [[gnu::noinline]]

// ── Purity (optimizer can CSE / move) ─────────────
#define CRUCIBLE_PURE         [[gnu::pure, nodiscard]]    // depends on args + memory
#define CRUCIBLE_CONST        [[gnu::const, nodiscard]]   // depends on args only

// ── Pointer contracts ─────────────────────────────
#define CRUCIBLE_NONNULL              [[gnu::nonnull]]
#define CRUCIBLE_RETURNS_NONNULL      [[gnu::returns_nonnull]]
#define CRUCIBLE_MALLOC               [[gnu::malloc]]      // returned ptr doesn't alias
#define CRUCIBLE_ALLOC_SIZE(n)        [[gnu::alloc_size(n)]]
#define CRUCIBLE_ASSUME_ALIGNED(n)    [[gnu::assume_aligned(n)]]

// ── Tail call (state machines) ────────────────────
#define CRUCIBLE_MUSTTAIL     [[gnu::musttail]]

// ── Spin pause (hot wait) ─────────────────────────
// x86: _mm_pause() — 10-40ns via MESI invalidation
// ARM: yield instruction
CRUCIBLE_SPIN_PAUSE
```

Use in function bodies:

```cpp
// Runtime alignment hint → optimizer assumes alignment from here on
p = static_cast<decltype(p)>(__builtin_assume_aligned(p, 64));

// Compile-time invariant
[[assume(n > 0 && n % 8 == 0)]];

// Likely/unlikely
if (cache_hit) [[likely]] { ... } else [[unlikely]] { ... }

// After exhaustive switch
switch (kind) {
    case Kind::A: ... return x;
    case Kind::B: ... return y;
    case Kind::C: ... return z;
}
std::unreachable();  // removes default branch, optimizer assumes switch is exhaustive
```

Use `__restrict__` on non-aliasing pointer params in inner loops — unlocks auto-vectorization:

```cpp
void scan(const T* __restrict__ in, U* __restrict__ out, size_t n);
```

---

## VIII. Performance Discipline

### Data layout

```
L1d:  48 KB   (~750 × 64B lines)    ~4-5 cycles
L2:   2 MB    (~30K lines)           ~12-20 cycles
L3:   ~30 MB (shared)                ~35-50 cycles
DRAM:                                 ~200-300 cycles
```

Rules:

1. **Hot working set fits in L1.** Budget 48 KB per core. Measure with `perf stat -e L1-dcache-loads,L1-dcache-load-misses`.
2. **SoA over AoS.** Iteration reads one field across N rows → one cache line per field vs N lines per row.
3. **`alignas(64)`** every atomic that crosses threads. False sharing = 40× slowdown.
4. **Pack cold fields into separate structs** or explicit trailing padding.
5. **Struct member ordering**: largest to smallest to minimize implicit padding, OR explicit `uint8_t pad[N]{}` for deterministic layout.
6. **No pointer chasing on hot path.** Contiguous arrays + indices, not linked lists.
7. **`static_assert(sizeof(T) == N)`** on layout-critical structs.

Example — hot ring entry is exactly one cache line:

```cpp
struct alignas(64) TraceRingEntry {
    OpIndex       op_idx;        //  4 B
    SchemaHash    schema_hash;   //  8 B
    ShapeHash     shape_hash;    //  8 B
    MetaIndex     meta_head;     //  4 B
    uint8_t       op_flags;      //  1 B
    uint8_t       pad[39]{};     // 39 B → total 64 B
};
static_assert(sizeof(TraceRingEntry) == 64);
```

### Prefetching

Manual prefetch ahead of loop iterations when stream access pattern is clear:

```cpp
for (size_t i = 0; i < n; ++i) {
    if (i + 16 < n) [[likely]]
        __builtin_prefetch(&data[i + 16], 0, 0);  // read, no temporal locality
    process(data[i]);
}
```

Rule: prefetch 8-16 iterations ahead; `locality = 0` for streaming reads (don't pollute L1 with data you won't revisit).

### Branches

1. `[[likely]]` / `[[unlikely]]` on predictable branches.
2. `__builtin_expect_with_probability(x, v, p)` for explicit probabilities.
3. **Predication**: replace `if (x < 0) x = 0` with `x = std::max(x, 0)` — compiler emits `cmov`.
4. **Switch on small dense enum** — compiler generates jump table; one indirect branch.
5. `std::unreachable()` after exhaustive switch — eliminates default entirely.
6. **Branchless bit tricks** where appropriate: `x & -cond` for conditional zeroing.

### Vectorization

Three tiers:

1. **Auto-vectorization** — clean loops + `__restrict__` + `[[assume]]`. Audit with `-fopt-info-vec -fopt-info-vec-missed`.
2. **`std::experimental::simd`** — portable when available.
3. **Intrinsics** (`<immintrin.h>`) — for AVX-512 / AVX2 when auto-vec misses. Wrap in named helpers.

Crucible's vectorizable hot paths: Philox RNG, hash mixing, TensorMeta extraction, reduction kernels.

### Link-Time Optimization (LTO)

`-flto=auto` — parallel LTO: whole-program inlining, dead code elimination, cross-TU constant propagation. Enabled unconditionally in release.

### OS / kernel tuning

For production Keeper deployments:

1. **Huge pages**: `madvise(arena_base, size, MADV_HUGEPAGE)` — cut TLB misses.
2. **`mlock()`** hot regions — no page faults on critical path.
3. **CPU affinity**: `pthread_setaffinity_np` — pin fg thread to one core, bg to another.
4. **Kernel CPU isolation**: boot with `isolcpus=4,5,6,7 nohz_full=4,5,6,7 rcu_nocbs=4,5,6,7`.
5. **NUMA-local allocation**: `numa_alloc_onnode()` for arena matching thread's NUMA node.
6. **Avoid SCHED_FIFO unless necessary** — real-time priority can lock the machine.

### Measurement discipline

`cmake --preset bench && ctest --preset stress`

1. Report **p50, p99, p99.9, max** — mean is banned. Tail dominates at distributed scale; a 7 ns mean with 500 ns p99.9 destroys throughput when any worker's worst iteration blocks the rest.
2. Run 10+ iterations. Worst observed is the bound, not the median.
3. `taskset -c <N>` to pin to an isolated core; `std::chrono::steady_clock` or `__rdtsc()` for timing (never `system_clock` — wall-clock jumps break experiments).
4. Variance > 5% = throttling = invalid. Check `dmesg | grep thermal`, re-run on quiet machine.
5. Units: ns per op, μs for setup.

```
✗ "try_push: ~8 ns average over 1M calls"
✓ "try_push: p50=7, p99=10, p99.9=18, max=142 ns (10M calls, max from cold-start cache miss)"
```

### Profiling toolkit — concrete commands

**Aggregate counters** (first look at any hot path):

```bash
perf stat -e cycles,instructions,cache-misses,cache-references, \
             L1-dcache-loads,L1-dcache-load-misses, \
             branch-misses,dTLB-load-misses \
    -- taskset -c 4 ./build/bench/bench_dispatch
```

Healthy fg-dispatch numbers on Tiger Lake / Zen 4 / Sapphire Rapids:
- IPC: 2.5–4.0 (instructions per cycle)
- L1d miss rate: <1%
- Branch miss rate: <0.5%

**Flame graph** (per-function cycle attribution):

```bash
perf record -F 997 -g --call-graph=lbr -- ./build/bench/bench_X
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

LBR gives cycle-accurate call stacks with minimal overhead. `-F 997` = ~1 kHz sampling, prime to avoid aliasing with timer tick.

**False-sharing hunt** (cache-line contention):

```bash
perf c2c record -- ./build/bench/bench_X
perf c2c report --stdio
```

Shows per-cache-line HITMs between cores. >1000 HITMs/s on a single line = false sharing. Fix: `alignas(64)` separation.

**Auto-vectorization diagnostics:**

```bash
g++ -O3 -march=native -fopt-info-vec=vec.log \
                      -fopt-info-vec-missed=missed.log \
                      -fopt-info-loop=loop.log ...
```

`missed.log` tells you WHY the vectorizer gave up. Common fixes: `__restrict__`, `[[assume(n%8==0)]]` on trip count, align, remove branch from inner body.

**Inlining diagnostics:**

```bash
g++ -O3 -fopt-info-inline-missed=inline.log ...
```

Catches accidental non-inlining of `CRUCIBLE_INLINE` / `CRUCIBLE_HOT` functions — typically triggered by type-erasure or virtual calls you didn't realize were there.

**Disassembly of a specific function** (audit what the optimizer produced):

```bash
objdump -d --disassembler-options=intel build/bench/bench_X | less
# or
g++ -S -O3 -masm=intel src.cpp -o -
```

Read the hot path's assembly. It's short. If you see a branch you didn't expect, a `lock` prefix you didn't want, or a spill to stack — fix at source.

**Intel PT full trace** (when counters + sampling aren't precise enough):

```bash
perf record -e intel_pt//u -- ./bench_X
perf script -F insn,ip,pid | awk '/hot_function/'
```

Full instruction trace at per-cycle resolution. ~10× overhead but gives exact branch-by-branch history. Reach for when diagnosing rare heisenbugs.

### Latency budgets per operation class

Every hot operation has a target p99 latency. Functions that exceed their budget by more than 2× are rewritten, not patched.

| Operation | p99 target | Notes |
|---|---|---|
| Shadow handle dispatch | 2-5 ns | `[[gnu::flatten]]`, no function call |
| TraceRing push | 5-10 ns | SPSC + `_mm_pause` + `alignas(64)` head/tail |
| Arena bump allocation | ≤2 ns | No branch, no lock |
| MetaLog append | 5-10 ns | SPSC, write-combined |
| Cross-core signal wait | 30-40 ns intra-socket | MESI floor; 100 ns cross-socket |
| Swiss-table lookup (hit) | 15-30 ns | Open addressing + SIMD probe |
| Contract check at boundary | 1-3 ns | `semantic=observe`; 0 on hot path with `ignore` |
| ExecutionPlan submit (warm) | 80-120 ns | Cache hit + doorbell |
| ExecutionPlan submit (cold, ≤5 patches) | 120-200 ns | Plan lookup + patch writes + SFENCE + doorbell |
| Syscall | 100-500 ns | Banned on hot path |
| `malloc` | 100-500 ns | Banned on hot path |

The budget table is a hard contract. A PR that regresses any hot-path budget by >10% requires an explicit "performance tradeoff" justification in the commit message, reviewed by at least one other maintainer.

### Hot functions fit in one cache line

Target: every hot-path function compiles to ≤64 bytes of machine code (one I-cache line fetch). Audit via `objdump -d`. Commit a snapshot of hot-function disassembly under `build/asm-snapshot/` and review its diff on every PR like source code.

If a hot function exceeds 64 bytes of machine code, either (a) the design is wrong (split into hot + cold), (b) a dependency is bloated (eliminate), or (c) the budget was optimistic (document the new budget and notify).

### Cold-path outlining is mandatory

Every `[[unlikely]]` branch body that is more than one return statement or one early-abort is **outlined** to a `[[gnu::cold, gnu::noinline]]` helper function. Rationale: `[[unlikely]]` only steers the branch predictor; it does not prevent the cold body's code from occupying an I-cache line adjacent to the hot path.

Rule: if a `[[unlikely]]` block has more than 8 lines of non-trivial work, outline it. The cold helper sits in `.text.unlikely` (a separate section) and never touches the L1 I-cache during normal execution.

### PGO + AutoFDO for release

Release builds are profile-guided; non-PGO release is a dev build, not production. Typical gain on branchy code: **5-15%**.

```bash
# 1. Instrumented build
cmake --preset release -DCRUCIBLE_PGO=generate && cmake --build --preset release

# 2. Run the bench suite under production workload
./build/bench/bench_dispatch && ./build/bench/bench_trace_ring

# 3. Rebuild with profile
cmake --preset release -DCRUCIBLE_PGO=use && cmake --build --preset release
```

Alternative (continuous profiling from production runs): **AutoFDO** via `-fauto-profile=<profile.afdo>` fed from `perf record`. Same wins, no instrumented build.

### Per-deployment microarchitecture targeting

- **Local development**: `-march=native -mtune=native`. Exact features of the build machine.
- **Distribution / production**: the specific minimum microarch of the deployment fleet, declared in the build manifest. No project-wide default — the choice is deployment-local and documented alongside the fleet's hardware spec.
- **Multi-versioned dispatch**: use `[[gnu::target_clones(...)]]` for routines that have microarchitecture-specific fast paths (e.g., AVX2 vs AVX-512 vs NEON). First call pays one indirect jump; subsequent calls are direct.

No single `-march=` default ships with Crucible. The build owner picks it for their fleet.

---

## IX. Concurrency Patterns

### The only two threads

```
Foreground (hot):  records ops at ~5 ns each via TraceRing
Background (warm): drains ring, builds TraceGraph, memory plan, compiles
```

No third thread except OS / OS-adjacent (systemd, signal handlers). Multiple background workers allowed inside Mimic for parallel kernel compilation.

### SPSC ring pattern

```cpp
template<typename T, size_t N>  // N power of 2
class SpscRing {
    alignas(64) std::atomic<uint64_t> head_{0};  // fg writes
    alignas(64) std::atomic<uint64_t> tail_{0};  // bg writes
    std::array<T, N> buffer_;                    // N = capacity
    static constexpr uint64_t MASK = N - 1;

public:
    // Producer side (fg thread)
    CRUCIBLE_HOT bool try_push(const T& item) {
        uint64_t h = head_.load(std::memory_order_relaxed);  // own variable
        uint64_t t = tail_.load(std::memory_order_acquire);   // cross-thread
        if (h - t >= N) [[unlikely]] return false;
        buffer_[h & MASK] = item;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Consumer side (bg thread)
    bool try_pop(T& item) {
        uint64_t t = tail_.load(std::memory_order_relaxed);  // own variable
        uint64_t h = head_.load(std::memory_order_acquire);   // cross-thread
        if (h == t) return false;
        item = buffer_[t & MASK];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
};
```

Note: `relaxed` is OK for a thread reading its OWN atomic. Only cross-thread reads need `acquire`.

### Cross-thread shared state

| Object | Producer | Consumer | Sync |
|---|---|---|---|
| TraceRing entries | fg | bg | SPSC acquire/release |
| MetaLog entries | fg | bg | SPSC acquire/release |
| KernelCache slots | bg | fg | CAS on atomic slot, fg reads acquire |
| RegionNode::compiled | bg | fg | atomic pointer swap (release/acquire) |
| Cipher warm writes | bg | (read by any peer) | Raft-committed |

### Async event waiting — the latency hierarchy

**The floor is ~10-40 ns on intra-socket, set by MESI.** Nothing is faster. Understanding why tells you when to spin and when not to.

#### Mechanism

```
Producer core                           Consumer core
─────────────                           ─────────────
  store(1, release)                       while (load(acquire) == 0)
                                              pause()
  │                                         │
  ▼                                         ▼
  Store buffer → L1 (Modified)            L1 has line in Shared state
  │                                       load returns 0 from L1 (~1 ns)
  ▼
  RFO invalidation via
  ring/mesh interconnect ─────────────→  Line transitions Shared → Invalid
  │                                       │
  │                                       Next load misses L1
  │                                       │
  ▼                                       ▼
  Line state = Modified                   Query L3 / peer L1-L2
                                          ◄───── Line forwarded (Exclusive → Shared)
                                          │
                                          Line now in Shared state, value = 1
                                          Load returns 1

  Total wall-clock: ~10-40 ns intra-socket (L3 ring latency)
                    ~30-100 ns cross-socket (UPI/QPI hop)
```

No kernel. No syscall. No context switch. Just transistors talking to transistors through the cache-coherence fabric.

#### Latency hierarchy

| Technique | Latency | Power | Use case |
|---|---|---|---|
| `load(acquire)` + `_mm_pause` | **10-40 ns** intra-socket, 30-100 ns cross-socket | High (core busy) | **Hot-path signal from imminent event — our default** |
| Same + exponential backoff | 10 ns – 1 μs | Moderate | Unknown-delay signal; rare in Crucible |
| `UMWAIT` (WAITPKG, C0.1/C0.2) | ~100-500 ns + wait time | Low | Power-aware; expected wait 1-100 μs. Not applicable on our hot path |
| `std::atomic::wait/notify` | 1-5 μs (maps to futex on Linux) | Low | BANNED on hot path |
| `futex(FUTEX_WAIT)` | 1-5 μs | Low | BANNED on hot path |
| `pthread_cond_wait` | 3-10 μs | Low | BANNED on hot path |
| `poll` / `epoll_wait` | 5-20 μs | Low | BANNED on hot path |

**Rule:** if the expected wait is under ~1 μs, spinning wins outright. If it's over ~100 μs, UMWAIT wins on power but we don't have waits that long on a well-designed hot path. In between (1-100 μs), the architecture is wrong — pipeline the work, don't wait.

#### Why `_mm_pause()` matters

PAUSE adds no latency. It hints SMT spinning (sibling HT gets more issue bandwidth), reduces memory-order-violation pipeline flush on loop exit, lowers power, and on Skylake+ is ~140 cycles (~40 ns at 3.5 GHz) acting as natural backoff. Pre-Skylake ~10 cycles — Skylake stretched it for fairer SMT. ARM: `yield` (SEV/WFE is for sleep-wait, too slow).

#### Store-side discipline

Fast spin-wait requires equally fast signal delivery.

- x86 (TSO): aligned `store(release)` is one `MOV` — no fence. `compare_exchange` is `LOCK CMPXCHG` — 15-25 ns (drains store buffer + full MESI round-trip).
- ARM (weakly-ordered): `store(release)` emits `STLR`, 1-2 cycles.
- `atomic_thread_fence(release)` is free on x86, emits `DMB ISH` on ARM.
- Never put atomic ops in tight producer loops — batch.

#### Producer-side false sharing — the 40× trap

If the producer's head_ counter and the consumer's tail_ counter share a cache line, every store on one side invalidates the other's read. Ping-pong. 40× latency penalty:

```cpp
// ✗ WRONG — both atomics on one line, cross-thread ping-pong
struct Ring {
    std::atomic<uint64_t> head_;     // 8 B
    std::atomic<uint64_t> tail_;     // 8 B, SAME CACHE LINE
    // ...
};

// ✓ CORRECT — each atomic isolated on its own cache line
struct Ring {
    alignas(64) std::atomic<uint64_t> head_;  // fg writes, bg reads
    alignas(64) std::atomic<uint64_t> tail_;  // bg writes, fg reads
    // ... buffer after
};
```

The `alignas(64)` is not decoration. It turns a 40× slowdown into optimal throughput. Same discipline applies to EVERY cross-thread atomic.

#### `std::atomic_ref` for element-wise atomic access

For atomic CAS on an element of a plain array (e.g. SwissCtrl bytes, KernelCache slots) without paying `std::atomic<T>` per-element overhead:

```cpp
uint64_t slots[N];                             // plain array, one cache line per slot

// Atomic CAS on one slot without making every slot atomic
std::atomic_ref<uint64_t> slot_ref{slots[idx]};
slot_ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
```

Requires alignment: `alignof(T) >= std::atomic_ref<T>::required_alignment` — usually `alignof(T)`, but AVOID spanning cache lines for 16-byte atomics.

#### Compiler barriers — ordering without runtime cost

Sometimes you need ordering without any hardware operation:

```cpp
asm volatile("" ::: "memory");                 // compiler barrier: don't reorder across this
std::atomic_signal_fence(std::memory_order_seq_cst);  // same, portable
```

Use when:
- Preventing the optimizer from hoisting/sinking ops around a measurement point (benchmarks).
- Ensuring a non-atomic write is committed before signaling (together with an atomic release on a separate variable).

Zero machine cost. Just blocks the optimizer.

#### The bottom line

30 ns is the physical floor for cross-core wait; the SPSC ring hits it. "Lower" claims are single-threaded, same-thread store-buffer read-after-write (~1 ns, not cross-thread), or cooked warm-L1 benchmarks. >50 ns per signal on hot path = something wrong: false sharing, a LOCK-prefixed fence, NUMA-remote memory, or a disguised kernel call. Diagnose with `perf stat` on cache events + `perf c2c`.

---

## X. Footgun Catalog

Organized by category. Each has a remediation.

### Lifetime / ownership

| Footgun | Remediation |
|---|---|
| `return std::move(local)` — defeats NRVO | `-Werror=pessimizing-move -Werror=redundant-move` |
| `std::move` from `const` — silent copy | `-Werror=pessimizing-move` |
| Use-after-move | `-fanalyzer`; moved-from only destroyed or reassigned |
| Reference member in struct | Pointer instead; reference breaks ctor/copy |
| Dangling `string_view`/`span` return | `-Werror=dangling-reference -Werror=return-local-addr` |
| Implicit capture-by-reference in escape lambda | Capture by value or explicit pointer |
| Iterator invalidation during mutation | Never mutate while iterating; collect + apply |
| Pointer into arena after arena reset | Generation counter on arena in debug; ASan catches |
| Self-copy not guarded | Non-trivial copies = `delete`; trivial types fine |

### Object model

| Footgun | Remediation |
|---|---|
| Slicing (`Base b = derived`) | No copyable polymorphic types (we have no `virtual`) |
| Implicit shallow copy of pointer members | `-Werror=deprecated-copy -Werror=deprecated-copy-dtor`; explicit `= default` or `= delete("reason")` |
| Most vexing parse | Uniform init: `Widget w{};` always |
| ADL surprises | Qualify `std::` calls; prefer hidden friends for customization |
| Hidden virtual override typo | `-Werror=suggest-override`; `override` keyword mandatory |

### Numeric

| Footgun | Remediation |
|---|---|
| `uint16_t + uint16_t → int` promotion | `-Werror=conversion -Werror=sign-conversion -Werror=arith-conversion` |
| `for (size_t i = n-1; i >= 0; --i)` infinite loop | `-Werror=type-limits`; idiom `while (i-- > 0)` |
| Left shift of negative signed | Unsigned bitwise or `std::rotl`; UBSan |
| Shift ≥ bitwidth | UBSan `shift-exponent` |
| Float `==` | `-Werror=float-equal`; `std::abs(a-b) < eps` or bit compare |
| `NaN` compare silently false | Explicit `std::isnan()` check |
| `size_t` vs `ptrdiff_t` mix | `-Werror=sign-conversion` |
| Signed overflow at non-sat site | `-fno-strict-overflow` + `-fsanitize=signed-integer-overflow` in CI |

### Concurrency

| Footgun | Remediation |
|---|---|
| Data race on non-atomic | `-fsanitize=thread` in CI |
| `std::thread` destructor without join | `std::jthread` (auto-joins) |
| `std::atomic<T>` for non-lock-free T | `static_assert(std::atomic<T>::is_always_lock_free)` |
| Torn read on oversize atomic | same assert |
| Relaxed atomics | acquire/release discipline + code review |
| `std::shared_mutex` | Banned; use SPSC or lock-free CAS |
| `volatile` for atomicity | `std::atomic` only |

### Template / metaprogramming

| Footgun | Remediation |
|---|---|
| O(N²) variadic recursion | Fold expressions + pack indexing (C++26) |
| Deep SFINAE | Concepts (C++20) |
| Forwarding-ref ambiguity | Constrain with concepts |
| `decltype(x)` vs `decltype((x))` | Prefer `std::remove_reference_t<decltype(x)>` |

### Library

| Footgun | Remediation |
|---|---|
| `std::function` heap + indirect | `std::function_ref` or templated `auto&&` |
| `std::regex` slow / throws | Handrolled parser or skip |
| `std::async` thread launch | Explicit `std::jthread` with captured work |
| `std::endl` flush | `'\n'` |
| `std::vector<bool>` proxy | `std::bitset<N>` or custom |
| `std::rand` global | Philox |

### Preprocessor / build

| Footgun | Remediation |
|---|---|
| Macro name collision | `CRUCIBLE_` prefix; `#undef` if absolutely necessary |
| ODR violation | Every header self-contained; `inline` variables for constants |
| Static init order fiasco | No dynamic-init globals; `constexpr` / `constinit` only |
| Singleton static-local guard atomic | Explicit init from `main` / `Keeper::init` |
| Header include order sensitivity | Every header compiles standalone; IWYU discipline |

---

## XI. Canonical Patterns

### Strong ID

```cpp
#define CRUCIBLE_STRONG_ID(Name) \
    struct Name { \
        uint32_t value_ = UINT32_MAX; \
        constexpr Name() = default; \
        explicit constexpr Name(uint32_t v) noexcept : value_{v} {} \
        constexpr uint32_t raw() const noexcept { return value_; } \
        constexpr bool is_none() const noexcept { return value_ == UINT32_MAX; } \
        static constexpr Name none() noexcept { return Name{UINT32_MAX}; } \
        auto operator<=>(const Name&) const = default; \
    }
```

### NSDMI struct with strong IDs

```cpp
struct alignas(32) TraceEntry {
    OpIndex     op_idx;                           // default = none()
    SchemaHash  schema_hash;                      // default = 0
    ShapeHash   shape_hash;                       // default = 0
    MetaIndex   meta_head;                        // default = none()
    ScopeHash   scope_hash;                       // default = 0
    uint8_t     op_flags  = 0;
    uint8_t     pad[3]{};                         // explicit zero-init
};
static_assert(sizeof(TraceEntry) == 32);
```

### Arena allocation

```cpp
template<typename T>
[[nodiscard]] T* Arena::alloc_obj() {
    static_assert(std::is_trivially_destructible_v<T>,
                  "Arena does not call destructors");
    void* raw = bump(sizeof(T), alignof(T));
    if (!raw) [[unlikely]] std::abort();
    auto* obj = std::start_lifetime_as<T>(raw);
    ::new (obj) T{};                              // default-init (NSDMI fires)
    return obj;
}
```

### Contract on boundary, bare on hot path

```cpp
// Boundary: contract-checked
std::expected<PlanId, Error> submit_plan(PlanId id, std::span<const PatchValue> patches)
    pre  (!id.is_none())
    pre  (std::all_of(patches.begin(), patches.end(), valid_patch))
    post (r) (r.has_value() || !r->is_none())
{
    return runtime::submit_plan_inner(id, patches);
}

// Hot path: no contracts (compiled with contract-semantic=ignore)
CRUCIBLE_HOT void submit_plan_inner(...) {
    // Guaranteed by caller; zero runtime check.
}
```

### SPSC ring hot path

```cpp
CRUCIBLE_HOT bool try_push(const TraceEntry& entry) noexcept {
    uint64_t h = head_.load(std::memory_order_relaxed);  // own variable
    uint64_t t = tail_.load(std::memory_order_acquire);   // cross-thread
    if ((h - t) >= CAPACITY) [[unlikely]] return false;
    buffer_[h & MASK] = entry;
    head_.store(h + 1, std::memory_order_release);
    return true;
}
```

### Reflection-generated hash (GCC 16 only)

```cpp
#if CRUCIBLE_HAS_REFLECTION
template <typename T>
[[nodiscard]] consteval uint64_t reflect_hash(const T& obj) {
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a seed
    template for (constexpr auto field : std::meta::nonstatic_data_members_of(^T)) {
        h = detail::fmix64(h ^ hash_field(obj.[:field:]));
    }
    return h;
}
#endif
```

### Hot loop with all the hints

```cpp
CRUCIBLE_HOT void scan(
    const TraceEntry* __restrict__ in,
    uint64_t*         __restrict__ out,
    size_t n
) noexcept {
    in  = static_cast<const TraceEntry*>(__builtin_assume_aligned(in, 64));
    out = static_cast<uint64_t*>       (__builtin_assume_aligned(out, 64));
    [[assume(n > 0 && n % 8 == 0)]];

    for (size_t i = 0; i < n; ++i) {
        out[i] = in[i].schema_hash.raw();
    }
}
// GCC 16 emits clean AVX-512 strided load + store, no bounds churn.
```

---

## XII. Error Handling and Debug Assertions

No exceptions (compiled out via `-fno-exceptions`). Three tiers of error response:

| Class | Mechanism | Runtime cost | Example |
|---|---|---|---|
| **Impossible** (contract violation) | `pre` / `post` / `contract_assert` | 0 ns on hot path (semantic=ignore), check in debug | Null pointer, OOB index, invariant violation |
| **Expected-but-rare** | `std::expected<T, E>` return | ~1 ns (branch on `.has_value()`) | Parse error, shape out of bucket, peer timeout |
| **Catastrophic** | `crucible_abort(msg)` | — | OOM, hardware fault, corrupt state, FLR failure |

### Contract semantics per TU

Hot-path TUs compile contracts with `ignore` semantic. Boundary TUs use `enforce`. Switch per file:

```cpp
// At top of a hot-path .cpp:
#pragma GCC contract_evaluation_semantic ignore

// Boundary .cpp:
#pragma GCC contract_evaluation_semantic enforce
```

CI builds everything with `enforce`. Release builds default `observe` (log + continue) except hot TUs explicitly marked `ignore`.

### The canonical `std::expected` flow

```cpp
enum class CompileError : uint8_t {
    SchemaHashMismatch,
    ShapeOutOfBucket,
    RecipeNotInFleet,
    BackendCompileFailed,
    BudgetExceeded,
};

[[nodiscard]] std::expected<CompiledKernel, CompileError>
compile_kernel(fx::Bg bg, Arena& arena,
               const KernelNode& k, const TargetCaps& caps)
    pre (k.recipe != nullptr)
    pre (k.tile != nullptr)
{
    if (!fleet_supports(k.recipe, caps)) [[unlikely]]
        return std::unexpected(CompileError::RecipeNotInFleet);
    // ...
    return CompiledKernel{ ... };
}

// Caller:
auto r = compile_kernel(bg, arena, node, caps);
if (!r) [[unlikely]] {
    log_compile_failure(r.error(), node);
    return fall_back_to_reference_eager(node);
}
const auto& ck = *r;  // happy path
```

`std::expected` is a union + discriminator tag (≤ 24 B for most errors). Can't be silently ignored (`[[nodiscard]]`). No heap, no exception tables.

### Assertion macro triad

```cpp
// ── CRUCIBLE_ASSERT ────────────────────────────────────────────
// Always-on boundary precondition. Cheap check, runs in release.
// Maps to contracts; respects contract-evaluation-semantic.
#define CRUCIBLE_ASSERT(cond) contract_assert(cond)

// ── CRUCIBLE_DEBUG_ASSERT ──────────────────────────────────────
// Hot-path invariant. Check in debug, compiled out in release.
#ifdef NDEBUG
  #define CRUCIBLE_DEBUG_ASSERT(cond) ((void)0)
#else
  #define CRUCIBLE_DEBUG_ASSERT(cond) contract_assert(cond)
#endif

// ── CRUCIBLE_INVARIANT ─────────────────────────────────────────
// Fact the optimizer can exploit. `[[assume]]` in release (free).
#ifdef NDEBUG
  #define CRUCIBLE_INVARIANT(cond) [[assume(cond)]]
#else
  #define CRUCIBLE_INVARIANT(cond) do {                           \
      if (!(cond)) [[unlikely]] {                                  \
          fprintf(stderr, "invariant failed: %s (%s:%d)\n",        \
                  #cond, __FILE__, __LINE__);                      \
          std::breakpoint();                                       \
          std::abort();                                            \
      }                                                            \
  } while (0)
#endif
```

**When to use which:**
- `CRUCIBLE_ASSERT` — public API entry. Contracts handle it.
- `CRUCIBLE_DEBUG_ASSERT` — SPSC ring bounds, arena bump sanity, RNG counter — hot path, can't afford a branch.
- `CRUCIBLE_INVARIANT` — loop trip counts, alignment, range bounds. The optimizer uses it.

### Abort path

```cpp
[[noreturn]] CRUCIBLE_COLD
void crucible_abort(const char* msg) noexcept {
    fprintf(stderr, "crucible: fatal: %s\n", msg);
    cipher::emergency_flush();       // try to salvage hot-tier state
    std::abort();                    // SIGABRT → core dump if enabled
}
```

Every abort path is `[[gnu::cold]]` + `noinline` so it lives in a cold section and doesn't pollute hot icache.

### Logging and tracing on hot path — banned

No `fprintf` / `std::cout` / `std::printf` / `std::format` on hot path — format parsing ≥100 ns, output syscalls flush buffers, noise pollutes measurement. Hot path uses atomic counters and structured events pushed to an SPSC ring for bg drain; human-readable output only on bg thread post-capture.

Development-mode trace gated on `NDEBUG` — release binary contains zero trace calls:

```cpp
#ifndef NDEBUG
  #define CRUCIBLE_TRACE(fmt, ...) \
      fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
  #define CRUCIBLE_TRACE(fmt, ...) ((void)0)
#endif
```

---

## XIII. Testing Discipline

Tests prove axioms hold. A failing test is an axiom violation, not a style issue.

### The load-bearing suite

These tests ARE the design guarantee. If they red, the guarantee is broken — stop and investigate.

| Test | Axiom(s) | Cadence |
|---|---|---|
| `bit_exact_replay_invariant` | DetSafe | Every PR |
| `cross_vendor_step_invariant` | DetSafe | Release gate (multi-backend) |
| `fleet_reshard_replay` | DetSafe + BorrowSafe | Release gate |
| `bit_exact_recovery_invariant` | DetSafe + MemSafe | Release gate |
| `checkpoint_format_stability` | DetSafe + LeakSafe | Every PR |
| `tsan_spsc_ring_*` | ThreadSafe + BorrowSafe | Every PR (tsan preset) |
| `asan_arena_lifetime_*` | MemSafe + LeakSafe | Every PR (default preset) |
| `ubsan_numeric_*` | TypeSafe + InitSafe | Every PR (default preset) |
| `stress_repeat_50x` | ThreadSafe (race detector) | Nightly |

If `bit_exact_replay_invariant` reddens — STOP. Hidden state was introduced. Never merge.

### Per-axiom test coverage

Each new struct / function adds at least one test that exercises its axiom claims:

```cpp
// Axiom InitSafe: default-constructed state is fully specified
TEST(Axiom_InitSafe, TensorSlot_DefaultIsWellDefined) {
    TensorSlot s{};
    ASSERT_EQ(s.offset_bytes, 0u);
    ASSERT_TRUE(s.slot_id.is_none());
    ASSERT_EQ(s.dtype, ScalarType::Undefined);

    // Padding is zero (NSDMI + P2795R5 guarantee)
    const auto* raw = reinterpret_cast<const std::byte*>(&s);
    for (size_t i = offsetof(TensorSlot, pad); i < sizeof(TensorSlot); ++i)
        ASSERT_EQ(std::to_integer<uint8_t>(raw[i]), 0u);
}

// Axiom TypeSafe: strong IDs reject silent swap
TEST(Axiom_TypeSafe, OpIndexSlotIdNotInterchangeable) {
    static_assert(!std::is_convertible_v<OpIndex, SlotId>);
    static_assert(!std::is_convertible_v<SlotId, OpIndex>);
}
```

### Unit test discipline

1. **One test, one claim.** Prefix with `Axiom_<Name>_<Case>` or `Behavior_<Feature>_<Case>`.
2. **Deterministic.** Fixed seed `42`. No `std::rand`, no `system_clock`, no reading `/proc` or network. Reproducibility is load-bearing.
3. **Isolated.** Each test builds its own arena / rings / ops. No shared state between tests.
4. **Fast.** < 100 ms per test (except stress/replay). ~100 tests × 100 ms = 10 s CI — keeps iteration tight.
5. **No retries.** A flaking test = race condition or bug. Fix it. Never `--repeat until-pass`.
6. **Every contract has a test that violates it.** Use `EXPECT_DEATH(fn(bad_args), ".*contract.*")` to prove contracts fire.

### Sanitizer preset matrix

```bash
# ASan + UBSan + analyzer: MemSafe, NullSafe, TypeSafe, InitSafe
cmake --preset default && cmake --build --preset default && ctest --preset default

# TSan: ThreadSafe, BorrowSafe
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan

# Stress (repeat-until-fail ×50): ThreadSafe race detection
ctest --preset stress

# Release: perf-bound tests, catches release-only codegen bugs
cmake --preset release && cmake --build --preset release && ctest --preset release

# Verify: all axioms via Z3 formal proofs
cmake --preset verify && cmake --build --preset verify && ctest --preset verify
```

Pre-merge gate: `default` + `tsan` + `release`. Release gate: all four + `verify`.

### Property-based + fuzz

For structural invariants (Merkle hash stability, arena aliasing, IR normal form), property tests with Philox-seeded inputs:

```cpp
TEST(Property_ContentHash_StableUnderEdgePermutation) {
    for (uint64_t seed : {42ULL, 0xBADC0FFEE0DDF00DULL, 0xDEADBEEFCAFEBABEULL}) {
        Philox rng{seed};
        auto g  = generate_random_graph(rng, /*nodes=*/100);
        auto gp = permute_unordered_edges(g, rng);
        ASSERT_EQ(g.root_hash(), gp.root_hash())
            << "hash depends on edge order — DetSafe violation";
    }
}
```

Fuzzing via AFL++ / libFuzzer targets untrusted-input paths (Cipher deserialize, Merkle DAG load, recipe registry parse). Hot paths don't need fuzz — their inputs are already well-formed by construction.

### Benchmarks are not tests

`bench/` and `test/` are separate trees. Bench asserts measure **numerical targets** (≤ 10 ns/op, ≤ 500 KB binary size). A test that accidentally measures latency is bad design — split it. Benchmarks can be noisy; tests cannot.

### Coverage targets

- Every public `.h` has a test.
- Every `pre` / `post` has a test that exercises both success AND violation.
- Every enum variant has a test (round-trip if serialized).
- Every `std::expected` error has a test that triggers it.
- Every `[[assume]]` has a test that proves the condition holds at every reachable call site.

No "% line coverage" number — coverage **of the 8 axioms** is the metric.

---

## XIV. Platform Assumptions

Hardcoded values. Changing any requires an audit sweep of the affected macros and `alignas`.

| Assumption | Value | Why |
|---|---|---|
| Cache line size | **64 bytes** | x86-64, most ARM. `alignas(64)` pervasive |
| Small page size | **4 KB** | x86-64 default. ARM may differ — use `sysconf(_SC_PAGESIZE)` at runtime for variable code |
| Huge page size | **2 MB** | x86-64 default huge page |
| Memory model | **TSO (x86) / weakly-ordered (ARM)** | Acquire/release correct on both |
| Endianness | **Little-endian** | x86 + ARM in practice; hashing assumes this |
| Word size | **64-bit** | No 32-bit support |
| Min x86 baseline | **AVX2 + FMA + BMI2** | Haswell-and-later |
| Optional x86 uplift | AVX-512, AMX | Opt-in per target via `target_clones`; never assumed |
| Min ARM baseline | **ARMv8.2-A + NEON** | Graviton 2+, Apple M1+ |
| Float representation | **IEEE 754** | `-fno-fast-math` enforces |
| Stack size | **8 MB** (Linux default) | Large arrays → arena, not stack |
| Canonical VA bits | **48** (x86-64 without LA57) | Pointer-tagging schemes assume this |
| Thread API | **pthreads** (via `std::jthread`) | Linux only |
| Syscall ABI | **Linux x86_64 / aarch64** | No Windows, no macOS in production |

### Platform checks at build time

```cpp
// In Platform.h or a dedicated Assumptions.h
static_assert(sizeof(void*) == 8, "64-bit required");
static_assert(std::endian::native == std::endian::little, "little-endian required");

#if defined(__aarch64__) && defined(__APPLE__)
  #error "Apple Silicon has 128-byte cache lines — alignas(64) is insufficient. Audit before enabling."
#endif

#if !defined(__x86_64__) && !defined(__aarch64__)
  #error "Crucible targets x86-64 and aarch64 only."
#endif
```

### When the assumptions change

- **New architecture** (RISC-V, Power): audit every `alignas(64)`, every `_mm_pause`, every endian-sensitive `bit_cast`.
- **Apple Silicon target**: cache line is 128 B. All `alignas(64)` becomes `alignas(CRUCIBLE_CACHE_LINE)` where the macro resolves per platform.
- **ARM 16 KB pages** (Apple, some Android): audit huge-page / `mmap` / `MADV_HUGEPAGE` code.
- **32-bit or 128-bit target**: not supported. Reject.

### Determinism across platforms

Under `BITEXACT_STRICT`, the same IR + same seed produces byte-identical output on any supported platform. This works because:

- IEEE 754 + `RN` rounding + `-fno-fast-math` = deterministic FP
- Philox4x32 is platform-independent (counter-based, bit-exact spec)
- Memory plan offsets are content-addressed (no alloc-order dependency)
- Canonical reduction topology (UUID-sorted binary tree)
- `std::bit_cast` for serialization — no endian-dependent `reinterpret_cast`

CI verifies with `cross_vendor_step_invariant`. A new platform must pass this test before shipping.

---

## XV. Headers, Includes, and Modules

### Header discipline

1. **`#pragma once`** on every header. No include guards.
2. **Self-contained.** Every header compiles standalone. Add required includes directly; never rely on transitive pull-in.
3. **IWYU** (include-what-you-use). If a `.cpp` uses `std::span`, include `<span>` — not via some project header that happens to pull it.
4. **No circular includes.** Refactor: one side gets a forward declaration, full include in the `.cpp` only.
5. **Forward declare in headers whenever possible.** Full definitions only when needed (inline methods, templates, `sizeof`).
6. **No precompiled headers.** PCH hides dependency bugs and complicates CMake. If build is slow, audit headers.

### Include order convention

```cpp
// In a .cpp file:
#include <crucible/ThisUnit.h>        // 1. own header first (catches missing includes)

#include <crucible/OtherUnits.h>      // 2. project, alphabetical

#include <third_party/lib.h>          // 3. third-party (rare in Crucible)

#include <cstdint>                    // 4. C stdlib
#include <atomic>                     // 5. C++ stdlib
```

### Hot path is header-only

- **Hot path** (`TraceRing`, `Arena`, `Graph`, `Expr`, `MerkleDag`, `CKernel`, `PoolAllocator`): header-only. Enables cross-TU inlining even without LTO.
- **Cold path** (Cipher cold tier, serialization, CLI, tools): `.h` + `.cpp` split. Reduces rebuild cost when implementation changes.
- **Templates**: always header-only (mandatory).
- **`inline` functions**: header-only with `inline` keyword (ODR).

### Namespace discipline

- All code in `namespace crucible` or nested (`crucible::mimic::nv`, `crucible::forge::phase_d`).
- **No** `using namespace std;` at file or namespace scope, ever. Inside function body OK when the benefit is clear.
- Anonymous namespaces for TU-local helpers. Not the `static` keyword (deprecated for this purpose).
- Don't use `using X::foo;` to leak implementation details from a nested namespace to a parent.

### C++20 Modules — deferred

GCC 16 has ABI-stable modules but we defer: CMake/ninja integration still maturing, debug info less mature than headers, and `-flto=auto` on header-only hot paths already delivers cross-TU inlining. Revisit when clean-rebuild exceeds ~30 s or modules become part of the public API. Until then: `#include`-based, header-only hot, split cold.

### One-definition rule (ODR) discipline

- `inline` on every function in a header (including single-line getters).
- `inline` on every `constexpr` variable in a header.
- Template specializations: inline + in a header.
- Anonymous namespace for TU-local: never in a header.

---

## XVI. Safety Wrappers

Library types in `include/crucible/safety/` that mechanize the axioms from §II at compile time. Every wrapper is a phantom-type newtype with **zero runtime cost** — `sizeof(Wrapper<T>) == sizeof(T)`, same machine code as the bare primitive under `-O3`. Generated assembly is indistinguishable from the unwrapped equivalent.

### Header catalog

| Header | Axioms it enforces | Role |
|---|---|---|
| `Linear.h` | MemSafe, LeakSafe, BorrowSafe | Move-only `Linear<T>`. `.consume() &&` takes ownership; `.peek() const&` borrows. Construction is `[[nodiscard]]`. |
| `Refined.h` | InitSafe, NullSafe, TypeSafe | `Refined<Pred, T>` — predicate checked by contract at construction; function bodies treat the invariant as `[[assume]]` downstream. |
| `Secret.h` | DetSafe + information-flow discipline | Classified-by-default `Secret<T>`. Escapes only via `declassify<Policy>()` with a grep-able `secret_policy::*` tag. |
| `Tagged.h` | TypeSafe | Phantom tags for provenance (`source::FromUser`, `source::FromDb`, `source::Internal`) and trust (`trust::Verified`, `trust::Unverified`). Mismatch at call sites = compile error. |
| `Session.h` | BorrowSafe | Type-state protocol channels. Each `.send()` / `.recv()` returns a new type carrying the remaining protocol. Wrong order or missing step = compile error. State lives in the type; zero runtime cost. |
| `Checked.h` | TypeSafe, DetSafe | `checked_add` / `wrapping_add` / `trapping_add` over `__builtin_*_overflow`. `std::add_sat` / `std::mul_sat` / `std::sub_sat` pass-through for saturation. |
| `Mutation.h` | MemSafe, DetSafe | `AppendOnly<T>` — no erase/resize. `Monotonic<T, Cmp>` — advance-only with contract guard on the step. |
| `ConstantTime.h` | DetSafe (side-channel resistance) | `ct::select`, `ct::eq`, branch-free primitives for crypto paths and Cipher key handling. |

Each header is ≤150 lines, header-only, depending only on `<type_traits>` and `Platform.h`.

### Usage rules

1. **Public API params wrap raw primitives.** `fn(Refined<positive, int> n)` — never `fn(int n)`. Bodies then trust the invariant without re-validating.
2. **Every resource type wraps in `Linear<T>`** — file handles, mmap regions, TraceRing, channel endpoints, arena-owned objects with drop semantics.
3. **Every load-bearing predicate gets a named alias** — `PositiveInt`, `NonNullTraceEntry`, `ValidSlotId`, `NonEmptySpan<T>`. Not anonymous refinements at call sites.
4. **Every classified value wraps in `Secret<T>`** — Philox keys, Cipher encryption keys, private weights, credentials. Declassification requires a `secret_policy::*` tag.
5. **Every trust-boundary crossing uses `Tagged<T, source::*>`** — deserialized input, network payload, FFI return. Sanitized-only APIs demand `source::Internal`.
6. **Every fixed-order protocol uses `Session<...>`** — handshakes, init sequences, channel lifecycles, plan-chain acquisition.
7. **Every append-only or monotonic structure wraps** in `AppendOnly<>` / `Monotonic<T, Cmp>` — event logs, generation counters, version numbers, Cipher warm writes.
8. **Every crypto path uses `ct::*` primitives** for comparisons and selections. Non-CT code in a `with Crypto` context is a review reject.

### Compiler enforcement

- `-Werror=conversion` + the wrapper types together prevent accidental unwrapping across boundaries.
- `-Werror=use-after-move` + `Linear<>`'s deleted copy constructor catches double-consume at compile time.
- `[[nodiscard]]` on every wrapper type's constructor forces the caller to capture the return value.
- Contracts on `Refined<>` and `Monotonic<>` constructors fire at construction sites under `semantic=enforce` (debug, CI, boundary TUs) and under `semantic=ignore` on hot-path TUs they compile to `[[assume]]` hints, optimizing downstream code as if the invariant always holds.
- Deleted copy + defaulted move on `Linear<>` / `Secret<>` / `Session<>` means the compiler rejects accidental duplication.
- Contract violations abort via `std::terminate` (P1494R5), never invoke undefined behavior.

### Review enforcement

Rules for code review and grep-guards:

- `declassify<` without a `secret_policy::*` policy tag → reject.
- `const_cast` → reject (banned per §III).
- `reinterpret_cast` → reject; use `std::bit_cast`.
- `std::chrono::system_clock` → reject (use `steady_clock` or `rdtsc`).
- A new public API taking raw `int`, `size_t`, `void*`, or `T*` without a wrapper → questioned on review; almost always rewritten.
- A new resource-carrying type without `Linear<>` → questioned; must have justification.
- Any `[[unlikely]]` body of more than 8 non-trivial lines without being outlined into a `CRUCIBLE_COLD` helper → reject.

### GCC 16 contracts — implementation gotchas

Real-world issues encountered implementing the wrappers.  Document them here so the next person doesn't rediscover them.

**Contract clause order on member functions.**  On a constructor or member function, `pre` / `post` must appear *after* `noexcept` and before the member initializer list (or body).  Newlines between clauses don't cost compilation, but `pre` separated from the colon by other tokens will not parse.

```cpp
// ✓ CORRECT
constexpr R(int x) noexcept pre(x > 0) : v{x} {}

// ✗ WRONG — pre before noexcept
constexpr R(int x) pre(x > 0) noexcept : v{x} {}

// ✗ WRONG — pre separated from colon by a different clause order
constexpr R(int x)
    pre(x > 0)         // parser error: expected ‘;’
    noexcept
    : v{x} {}
```

**`-fcontracts` and `-freflection` require `-std=c++26`.**  CMake's compiler-probe step runs before the project's `CMAKE_CXX_STANDARD` takes effect, so putting these flags in `CMAKE_CXX_FLAGS` via the preset breaks configuration.  Instead, set them at target level via `target_compile_options(crucible INTERFACE -freflection -fcontracts)` after `project()` has declared the standard.

**`handle_contract_violation` must be defined by the program.**  GCC 16 / libstdc++ 16 does not ship a default handler.  Every program that enables contracts must provide one; otherwise the link fails with `undefined reference to handle_contract_violation(std::contracts::contract_violation const&)`.  The project default belongs in a shared `CrucibleContractHandler.cpp` wired to `crucible_abort()`.

```cpp
#include <contracts>
void handle_contract_violation(const std::contracts::contract_violation& v) {
    fprintf(stderr, "contract: %s\n", v.comment());
    std::abort();
}
```

**Postconditions with value parameters.**  P2900R14 forbids using a by-value parameter in a postcondition unless it is `const`.  `fn(int n) post(r: r == n*2)` fails with "value parameter used in postcondition must be const"; write `fn(int const n) post(r: r == n*2)` instead.  Fix is trivial once you know; confusing if you don't.

### GCC 16 reflection — implementation gotchas

**Reflection APIs return `std::vector<std::meta::info>`.**  Since `std::vector` uses `operator new`, you cannot assign the result directly to a non-`static` `constexpr` local and then iterate it with `template for` — the `operator new` allocation is not constant in the expansion-statement's required-constant-expression context.

```cpp
// ✗ WRONG — non-static constexpr local crossing consteval→runtime
constexpr auto members = std::meta::nonstatic_data_members_of(^^T, ctx);
template for (constexpr auto m : members) { ... }  // ERROR

// ✓ CORRECT — static gives the local a constant address
static constexpr auto members = std::define_static_array(
    std::meta::nonstatic_data_members_of(^^T, ctx));
template for (constexpr auto m : members) { ... }
```

`std::define_static_array` (P3491R3) materializes the vector into a static constexpr array, bypassing the allocator boundary.

**`access_context::current()` is mandatory.**  Reflection introspection primitives take an `access_context` as their second argument (P3293R3 plumbing).  `std::meta::nonstatic_data_members_of(^^T)` compiles, but returns fewer members than expected; always pass `std::meta::access_context::current()`.

**Unstructured bindings in `template for`.**  An expansion statement iterating a pack where each element needs destructuring must use a `constexpr auto` binding, not `auto&`.  `&` reference bindings create a non-constant expression.

### GCC 16 toolchain — build gotchas

**Clangd parses with stale config after a toolchain swap.**  After dropping Clang from the presets, IDE diagnostics may still show `Unknown argument: '-freflection'` and `'__config_site' file not found` errors.  Delete `build/compile_commands.json` and reconfigure: `rm -rf build && cmake --preset default` so clangd re-reads the new compile flags.  The actual build is unaffected.

**Runtime libstdc++ resolution.**  Binaries compiled with the local GCC 16 tree link against that tree's `libstdc++.so.6` which is newer than the system's.  Without an rpath, they fail at runtime.  The `default` preset's `CMAKE_EXE_LINKER_FLAGS` / `SHARED_LINKER_FLAGS` / `MODULE_LINKER_FLAGS` all carry `-Wl,-rpath,$(gcc16 prefix)/usr/lib64`; verify with `readelf -d build/test/test_X | grep RUNPATH`.

### What the wrappers do not cover

- **Flow-sensitive refinement propagation.** GCC does not prove a `Refined<>` invariant holds across multiple function boundaries. The wrapper checks at construction; the body inside the function trusts the invariant. Chaining requires either re-wrapping at each boundary or an SMT-discharged proof via the `verify` preset (§I).
- **Alias analysis.** `Linear<>` catches double-consume on a single value but not two pointers to the same underlying object created through unsafe channels. Review + `-fsanitize=address` + the axioms from §II are the line of defense.
- **Compile-time information flow.** Branching on a `Secret<>` value is not rejected at compile time. `ct::*` primitives and the constant-time discipline (§III opt-out of `memory_order::consume`, crypto paths using `ct::select`) are opt-in.

For those properties, the `verify` preset runs SMT (Z3) over the annotated code; see §I.

---

## XVII. Hard Stops (Review Checklist)

Every PR passes all hard stops or is rejected.

**HS1.** Fix root cause, never bump a timeout or workaround. Timeouts mask races.

**HS2.** Clean on `default`, `tsan`, and `release` presets before merge. All three are GCC 16 — no other compiler is supported (§I).

**HS3.** No unwanted files — no .md summaries, no helper/utils. New files only when structurally necessary.

**HS4.** No destructive git — no `checkout`/`reset`/`clean`/`stash` without explicit permission. No `--no-verify`. Atomic commits.

**HS5.** Measure, don't guess. Run 10+ iterations, worst observed is the bound. Variance > 5% = throttling = results invalid.

**HS6.** No `sed`/`awk`. Edit tool only. Every change visible in diff.

**HS7.** All 8 axioms checked on every struct, every function, every edit. Not just the axiom being worked on.

**HS8.** Zero `-Werror=` violations. Warnings-as-errors are the minimum bar.

**HS9.** No vendor libraries. No cuBLAS, no cuDNN, no NCCL, no libcuda. Kernel driver ioctls only (see MIMIC.md §36).

**HS10.** No allocation on hot path. Arena or preallocation. `new`/`delete` in hot code = rejected.

**HS11.** Determinism preserved. Same inputs → same outputs, bit-identical under BITEXACT recipe. CI test `bit_exact_replay_invariant` must pass.

---

## XVIII. When to Update This Guide

Update rules here when:

1. A new UB class is discovered in production or CI → add to footgun catalog.
2. A new GCC/libstdc++ feature lands that measurably helps an axiom → add to opt-in.
3. A measurement invalidates a "perf wisdom" here → replace with the measured version.
4. A rule is consistently violated without consequence → investigate whether the rule is still necessary.

Every change to this guide is a semi-major commit with rationale. This guide is the contract between engineer and codebase.

---

## XIX. The Cost Hierarchy

When in doubt, the cost of failure ordering is:

```
Correctness  >  Determinism  >  Security  >  Latency  >  Throughput  >  Code size
```

Never trade correctness for latency. Never trade determinism for throughput. Security is non-negotiable (replay determinism IS a security property). Latency matters because of the ~5 ns/op budget — but only after the first three.

---

ZERO COPY. ZERO ALLOC ON HOT PATH. EVERY INSTRUCTION JUSTIFIED.
L1d = 48 KB. L2 = 2 MB. Squat upfront, point into it, write into it.

IF VARIANCE > 5% IN BENCHES — IGNORE RESULTS, THE LAPTOP IS THROTTLING.

IF A RULE HERE CONFLICTS WITH REALITY — MEASURE, FIX REALITY, OR FIX THE RULE.
NEVER WRITE CODE THAT CONTRADICTS BOTH.
