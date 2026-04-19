#pragma once

// Standalone replacements for c10/macros/Macros.h.
// Only the subset Crucible actually uses.
//
// Branch hints use C++20 [[likely]]/[[unlikely]] attributes directly
// at usage sites instead of macros wrapping __builtin_expect.

#include <version>
#include <type_traits>

#if defined(_MSC_VER)
  #define CRUCIBLE_INLINE __forceinline
  #define CRUCIBLE_NOINLINE __declspec(noinline)
  #define CRUCIBLE_API __declspec(dllexport)
#else
  #define CRUCIBLE_INLINE __attribute__((always_inline)) inline
  #define CRUCIBLE_NOINLINE __attribute__((noinline))
  #define CRUCIBLE_API __attribute__((visibility("default")))
#endif

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
// C++26 feature detection
//
// Three-compiler strategy: Clang 22 (primary), GCC 15 (fallback),
// GCC 16 (bleeding-edge safety). Each has exclusive features.
// Guard conditional code with these macros so the same codebase
// compiles on all three with maximum safety on each.
//
// Language features: defined by the compiler frontend, no include needed.
// Library features: defined by <version>, included above.
// ═══════════════════════════════════════════════════════════════════

// ── GCC 16 exclusive: P1306 expansion statements ──
// `template for (auto m : ...) { }` — iterate over packs/reflections.
#if defined(__cpp_expansion_statements) && __cpp_expansion_statements >= 202506L
  #define CRUCIBLE_HAS_EXPANSION_STMT 1
#else
  #define CRUCIBLE_HAS_EXPANSION_STMT 0
#endif

// ── GCC 16 exclusive: std::inplace_vector<T, N> ──
// Fixed-capacity, bounds-checked replacement for T[N] arrays.
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
  #define CRUCIBLE_HAS_INPLACE_VECTOR 1
#else
  #define CRUCIBLE_HAS_INPLACE_VECTOR 0
#endif

// ── GCC 16 exclusive: std::function_ref ──
// Non-owning type-erased callable (cheaper than std::function).
#if defined(__cpp_lib_function_ref) && __cpp_lib_function_ref >= 202306L
  #define CRUCIBLE_HAS_FUNCTION_REF 1
#else
  #define CRUCIBLE_HAS_FUNCTION_REF 0
#endif

// ── GCC 16 exclusive: std::indirect / std::polymorphic ──
// Value-semantic heap indirection (SOO) and polymorphic containers.
#if defined(__cpp_lib_indirect) && __cpp_lib_indirect >= 202502L
  #define CRUCIBLE_HAS_INDIRECT 1
#else
  #define CRUCIBLE_HAS_INDIRECT 0
#endif

// ── Clang 22 exclusive: trivial relocatability ──
// [[clang::trivially_relocatable]] + std::is_trivially_relocatable_v.
#if defined(__cpp_trivial_relocatability)
  #define CRUCIBLE_HAS_TRIVIAL_RELOC 1
#else
  #define CRUCIBLE_HAS_TRIVIAL_RELOC 0
#endif

// ── GCC 16 exclusive: std::breakpoint / std::is_debugger_present ──
#if defined(__cpp_lib_debugging) && __cpp_lib_debugging >= 202403L
  #define CRUCIBLE_HAS_DEBUGGING 1
#else
  #define CRUCIBLE_HAS_DEBUGGING 0
#endif

// ── Trivial relocatability assert (Clang 22) ──
// Verifies that a type can be safely memcpy'd (critical for Arena).
// No-op on compilers without the builtin.
#if CRUCIBLE_HAS_TRIVIAL_RELOC
  #define CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(T) \
    static_assert(__builtin_is_cpp_trivially_relocatable(T), \
                  #T " must be trivially relocatable for Arena memcpy safety")
#else
  #define CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(T) \
      static_assert(std::is_trivially_copyable_v<T>, \
                    #T " must be trivially copyable for Arena memcpy safety")
#endif
