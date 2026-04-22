#pragma once

// Standalone replacements for c10/macros/Macros.h.
// Only the subset Crucible actually uses.
//
// Branch hints use C++20 [[likely]]/[[unlikely]] attributes directly
// at usage sites instead of macros wrapping __builtin_expect.

#include <version>
#include <type_traits>

#include <contracts>   // CRUCIBLE_ASSERT / CRUCIBLE_DEBUG_ASSERT
#include <cstdio>      // CRUCIBLE_INVARIANT diagnostic path
#include <cstdlib>     // CRUCIBLE_INVARIANT abort path
#include <cstring>     // is_debugger_present: strstr parse of /proc status
#include <fcntl.h>     // is_debugger_present: open(/proc/self/status)
#include <unistd.h>    // is_debugger_present: read/close

// ═══════════════════════════════════════════════════════════════════
// Toolchain floor — hard-require C++26 and GCC 16 (or Clang 22 for
// editor-only parsing). This is not a recommendation; the codebase
// unconditionally uses contracts (pre/post), static reflection (^^T),
// expansion statements (template for), std::inplace_vector, and other
// C++26 features with no fallback. Any lower toolchain silently
// miscompiles the moment one of those hits, so the guard fires at the
// top of Platform.h — every TU sees it before anything else is parsed.
//
// Memo: feature guards (`#if CRUCIBLE_HAS_*`, `#if __cpp_*`) are
// BANNED everywhere in this codebase per
//     .claude/.../memory/feedback_no_feature_guards.md
// Write the feature directly; trust the toolchain.
// ═══════════════════════════════════════════════════════════════════

// GCC 16 with `-std=c++26` reports `__cplusplus = 202400L` (C++26 draft
// value). The ISO-final C++26 publication will bump this to 202600L;
// bump the floor then. Until then, 202400L is the right floor — C++23
// is 202302L, C++20 is 202002L.
static_assert(__cplusplus >= 202400L,
              "Crucible requires C++26 (-std=c++26). See CMakePresets.json.");

#if !defined(__clang__)
static_assert(__GNUC__ >= 16,
              "Crucible requires GCC 16 for -fcontracts / -freflection. "
              "See CLAUDE.md toolchain section.");
#endif

// ═══════════════════════════════════════════════════════════════════
// Attribute macros — code_guide §VII
//
// Inlining control, purity, pointer contracts, and tail-call intent.
// Macros (not direct [[gnu::...]]) so call sites grep cleanly and so
// the project can adjust the underlying attribute form per-toolchain
// in one place later.
//
// Style:
//   [[gnu::X]] is the standard-attribute form GCC honors since ~5.x.
//   Preferred over __attribute__((X)) because it composes cleanly
//   with C++26 [[assume]], [[nodiscard]], [[likely]], etc.
//
// Usage (from §VII):
//   CRUCIBLE_HOT    on per-op dispatch, SPSC push, MetaLog append
//   CRUCIBLE_COLD   on init / factory / divergence-recovery helpers
//   CRUCIBLE_FLATTEN on hot wrappers that should inline their callees
//   CRUCIBLE_PURE   on [nodiscard + depends on args+memory] query fns
//   CRUCIBLE_CONST  on [nodiscard + depends only on args] math helpers
//   CRUCIBLE_NONNULL          when every pointer param is required
//   CRUCIBLE_RETURNS_NONNULL  when the return is guaranteed non-null
//   CRUCIBLE_MALLOC           allocator returns non-aliasing ptr
//   CRUCIBLE_ALLOC_SIZE(n)    argument-indexed size in bytes
//   CRUCIBLE_ASSUME_ALIGNED(n) return alignment for optimizer
//   CRUCIBLE_MUSTTAIL         GCC 16 tail-call guarantee
// ═══════════════════════════════════════════════════════════════════

// ── Inlining control ───────────────────────────────────────────────
#define CRUCIBLE_INLINE       [[gnu::always_inline]] inline
#define CRUCIBLE_HOT          [[gnu::hot, gnu::always_inline]] inline
#define CRUCIBLE_COLD         [[gnu::cold, gnu::noinline]]
#define CRUCIBLE_FLATTEN      [[gnu::flatten]]
#define CRUCIBLE_NOINLINE     [[gnu::noinline]]

// ── Purity (optimizer can CSE / move across side-effect points) ────
// PURE:  depends on args + observable memory (no side effects,
//        but may dereference pointers).  Pair with [[nodiscard]] —
//        a PURE call that discards its result is a bug.
// CONST: depends on args only; does not read memory.  Strictly
//        stronger than PURE.  Use on sat-math, bit helpers, simple
//        arithmetic where the args alone determine the result.
#define CRUCIBLE_PURE         [[gnu::pure, nodiscard]]
#define CRUCIBLE_CONST        [[gnu::const, nodiscard]]

// ── Pointer contracts ──────────────────────────────────────────────
#define CRUCIBLE_NONNULL              [[gnu::nonnull]]
#define CRUCIBLE_RETURNS_NONNULL      [[gnu::returns_nonnull]]
#define CRUCIBLE_MALLOC               [[gnu::malloc]]
#define CRUCIBLE_ALLOC_SIZE(n)        [[gnu::alloc_size(n)]]
#define CRUCIBLE_ASSUME_ALIGNED(n)    [[gnu::assume_aligned(n)]]

// ── Tail call ──────────────────────────────────────────────────────
// Used in state-machine-style dispatch where the called function's
// return is the enclosing function's return — GCC 16 treats it as a
// hard requirement (compile error if the call can't be tail-optimized).
#define CRUCIBLE_MUSTTAIL     [[gnu::musttail]]

// ── Symbol visibility ─────────────────────────────────────────────
#define CRUCIBLE_API          __attribute__((visibility("default")))

// ═══════════════════════════════════════════════════════════════════
// Spin-pause hint — the ONLY cross-thread synchronization primitive.
//
// Pure atomic::load(acquire) spin is how we wait. MESI cache-line
// invalidation delivers writes in 10-40ns. No OS, no kernel, no
// syscall. Just transistors talking to transistors.
//
// _mm_pause() / __yield tells the CPU "I'm spinning" — saves power,
// avoids pipeline flush penalty on loop exit. Costs zero latency.
//
// NEVER use sleep_for, yield(), futex, condition_variable, or any
// other OS-mediated wait. They add microseconds of jitter minimum.
// ═══════════════════════════════════════════════════════════════════

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #include <immintrin.h>
  #define CRUCIBLE_SPIN_PAUSE _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define CRUCIBLE_SPIN_PAUSE __asm__ volatile("yield")
#else
  #define CRUCIBLE_SPIN_PAUSE ((void)0)
#endif

// ═══════════════════════════════════════════════════════════════════
// Thread safety annotations — Clang's -Wthread-safety (P1179R1)
//
// Compile-time data race detection via capability annotations.
// -Wthread-safety is already enabled (part of -Weverything).
// These macros make the analysis effective by annotating ownership.
//
// Usage:
//   class CRUCIBLE_CAPABILITY("mutex") SpinLock { ... };
//   int counter CRUCIBLE_GUARDED_BY(mu);
//   void inc() CRUCIBLE_REQUIRES(mu) { counter++; }
//
// For SPSC patterns (no mutex, safe by design), use:
//   void try_append(...) CRUCIBLE_NO_THREAD_SAFETY { ... }
//
// GCC silently ignores all of these.
// ═══════════════════════════════════════════════════════════════════

#if defined(__clang__)
  // Mark a type as a mutex/lock capability.
  #define CRUCIBLE_CAPABILITY(name) \
    __attribute__((capability(name)))

  // Data protected by a capability.
  #define CRUCIBLE_GUARDED_BY(cap) \
    __attribute__((guarded_by(cap)))
  #define CRUCIBLE_PT_GUARDED_BY(cap) \
    __attribute__((pt_guarded_by(cap)))

  // Function requires capability held / not held.
  #define CRUCIBLE_REQUIRES(...) \
    __attribute__((requires_capability(__VA_ARGS__)))
  #define CRUCIBLE_REQUIRES_SHARED(...) \
    __attribute__((requires_shared_capability(__VA_ARGS__)))
  #define CRUCIBLE_EXCLUDES(...) \
    __attribute__((locks_excluded(__VA_ARGS__)))

  // Function acquires / releases / tries to acquire.
  #define CRUCIBLE_ACQUIRE(...) \
    __attribute__((acquire_capability(__VA_ARGS__)))
  #define CRUCIBLE_RELEASE(...) \
    __attribute__((release_capability(__VA_ARGS__)))
  #define CRUCIBLE_TRY_ACQUIRE(...) \
    __attribute__((try_acquire_capability(__VA_ARGS__)))

  // Escape hatch for SPSC / atomic patterns safe by design.
  #define CRUCIBLE_NO_THREAD_SAFETY \
    __attribute__((no_thread_safety_analysis))

  // Assert capability held at a point (runtime no-op, static check).
  #define CRUCIBLE_ASSERT_CAPABILITY(cap) \
    __attribute__((assert_capability(cap)))
#else
  #define CRUCIBLE_CAPABILITY(name)
  #define CRUCIBLE_GUARDED_BY(cap)
  #define CRUCIBLE_PT_GUARDED_BY(cap)
  #define CRUCIBLE_REQUIRES(...)
  #define CRUCIBLE_REQUIRES_SHARED(...)
  #define CRUCIBLE_EXCLUDES(...)
  #define CRUCIBLE_ACQUIRE(...)
  #define CRUCIBLE_RELEASE(...)
  #define CRUCIBLE_TRY_ACQUIRE(...)
  #define CRUCIBLE_NO_THREAD_SAFETY
  #define CRUCIBLE_ASSERT_CAPABILITY(cap)
#endif

// ═══════════════════════════════════════════════════════════════════
// D3390 "Safe C++" annotations — Clang 22 approximations
//
// Borrow checking:  CRUCIBLE_LIFETIMEBOUND marks functions whose
//   return value's lifetime is bounded by `*this` or a parameter.
//   Clang emits -Wdangling when temporaries outlive the binding.
//
// Unsafe context:   CRUCIBLE_UNSAFE_BUFFER_USAGE marks functions that
//   intentionally use raw ptr[i] / ptr+n. Suppresses -Wunsafe-buffer-usage
//   inside the function body; warns at call sites instead.
//
// Ownership model:  CRUCIBLE_OWNER marks types that own heap memory.
//   Enables -Wdangling-gsl to catch dangling from owner temporaries.
//   CRUCIBLE_POINTER would mark borrowing types — deferred (Arena
//   borrowing model doesn't fit the GSL Owner→Pointer pattern cleanly).
//
// GCC warns on unknown [[clang::*]] / [[gsl::*]] via -Wattributes,
// so the #ifdef guard is mandatory.
// ═══════════════════════════════════════════════════════════════════

#if defined(__clang__)
  #define CRUCIBLE_LIFETIMEBOUND [[clang::lifetimebound]]
  #define CRUCIBLE_OWNER [[gsl::Owner]]
  #define CRUCIBLE_POINTER [[gsl::Pointer]]
  #define CRUCIBLE_UNSAFE_BUFFER_USAGE [[clang::unsafe_buffer_usage]]
#else
  #define CRUCIBLE_LIFETIMEBOUND
  #define CRUCIBLE_OWNER
  #define CRUCIBLE_POINTER
  #define CRUCIBLE_UNSAFE_BUFFER_USAGE
#endif

// ═══════════════════════════════════════════════════════════════════
// Arena memcpy-safety assertion
//
// Crucible's Arena moves objects by bulk memcpy during block growth,
// which is sound for trivially-copyable types. We use std::is_trivially
// _copyable_v unconditionally — trivial-relocatability (P2786) is still
// a Clang-only extension in 2026 and a superset we don't need.
// ═══════════════════════════════════════════════════════════════════

#define CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(T)                       \
    static_assert(std::is_trivially_copyable_v<T>,                     \
                  #T " must be trivially copyable for Arena memcpy safety")

// Strong variant: BOTH trivially-copyable (for memcpy) AND standard-layout
// (for offsetof / per-field serialization).  Use on types that are written
// to disk via explicit offset math; the plain macro is enough for Arena
// memcpy where absolute field offsets are never computed.
//
// Note: inheritance with data members in BOTH base and derived breaks
// standard-layout.  TraceNode + RegionNode/BranchNode/LoopNode hierarchy
// uses the plain variant; leaf struct types (MemoryPlan, TensorMeta,
// TensorSlot, CallSiteTable entries, serialized records) get this one.
#define CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(T)                \
    static_assert(std::is_trivially_copyable_v<T>,                     \
                  #T " must be trivially copyable for Arena memcpy safety"); \
    static_assert(std::is_standard_layout_v<T>,                        \
                  #T " must be standard-layout so offsetof() and "     \
                  "serialize/deserialize via per-field offsets is "    \
                  "well-defined")

// ═══════════════════════════════════════════════════════════════════
// Debugging primitives — shim for C++26 <debugging> (P2546R5).
//
// GCC 16.0.1 rawhide declares std::breakpoint / breakpoint_if_debugging
// / is_debugger_present in <debugging> but libstdc++ 16.0.1 does not
// yet ship the definitions (symbols absent from libstdc++.so.6.0.35).
// We provide spec-equivalent implementations in crucible::detail so
// the assertion triad below links today; once libstdc++ ships the
// definitions, call sites can be migrated to std::* mechanically.
// ═══════════════════════════════════════════════════════════════════

namespace crucible::detail {

// True iff this process is being traced by a debugger.  Linux impl
// parses /proc/self/status for a non-zero TracerPid.  Cost is a few
// syscalls — acceptable on the failure path (we're about to abort).
// Returns false conservatively on any read/parse failure.
[[nodiscard]] inline bool is_debugger_present() noexcept {
    int fd = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[4096];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    const char* p = buf;
    while ((p = std::strstr(p, "TracerPid:")) != nullptr) {
        p += sizeof("TracerPid:") - 1;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p != '0' && *p >= '0' && *p <= '9') return true;
        break;
    }
    return false;
}

// Unconditional programmatic breakpoint.  Platform-specific trap:
// int3 on x86, brk #0 on aarch64.  __builtin_trap is portable and
// emits the same opcode GCC uses for std::unreachable on UB.
[[gnu::always_inline]] inline void breakpoint() noexcept {
    __builtin_trap();
}

// Breakpoint only when a debugger is attached.  Under unattended CI
// the call no-ops; under gdb/lldb it drops the operator at the
// failure site so the stack frame can be inspected before abort.
[[gnu::always_inline]] inline void breakpoint_if_debugging() noexcept {
    if (is_debugger_present()) [[unlikely]] __builtin_trap();
}

} // namespace crucible::detail

// ═══════════════════════════════════════════════════════════════════
// Assertion triad — code_guide §XII
//
// Strictly-ordered by when they fire and what they cost:
//
//   CRUCIBLE_ASSERT(cond)        Always-on boundary precondition.
//                                Contract-backed; respects
//                                contract-evaluation-semantic.  Runs in
//                                release under the default "observe"
//                                semantic (logs + continues); hot TUs
//                                opt to "ignore" for zero cost.
//
//   CRUCIBLE_DEBUG_ASSERT(cond)  Hot-path invariant.  Checked in debug
//                                builds (contract_assert), compiled
//                                out entirely in release via NDEBUG.
//
//   CRUCIBLE_INVARIANT(cond)     Fact the optimizer can exploit.
//                                Release: lowers to [[assume(cond)]]
//                                — zero runtime code, steers codegen
//                                (range analysis, loop vectorization,
//                                switch densification).
//                                Debug: prints diagnostic, pauses the
//                                attached debugger (P2546R5 via the
//                                detail:: shim above), then aborts.
//
// The INVARIANT debug path uses breakpoint_if_debugging() rather than
// unconditional breakpoint(): on unattended CI the traphint no-ops and
// execution falls through to abort; under an attached debugger it
// drops the operator at the failure site for inspection.  Without
// this distinction, CI runs would core-dump with SIGTRAP and hide the
// diagnostic message we just printed.
// ═══════════════════════════════════════════════════════════════════

#define CRUCIBLE_ASSERT(cond) contract_assert(cond)

#ifdef NDEBUG
  #define CRUCIBLE_DEBUG_ASSERT(cond) ((void)0)
#else
  #define CRUCIBLE_DEBUG_ASSERT(cond) contract_assert(cond)
#endif

#ifdef NDEBUG
  #define CRUCIBLE_INVARIANT(cond) [[assume(cond)]]
#else
  #define CRUCIBLE_INVARIANT(cond)                                            \
      do {                                                                    \
          if (!(cond)) [[unlikely]] {                                         \
              if (!::crucible::detail::is_debugger_present()) {               \
                  std::fprintf(stderr,                                        \
                               "crucible: invariant failed: %s (%s:%d)\n",   \
                               #cond, __FILE__, __LINE__);                    \
              }                                                               \
              ::crucible::detail::breakpoint_if_debugging();                  \
              std::abort();                                                   \
          }                                                                   \
      } while (0)
#endif
