#pragma once

// Standalone replacements for c10/macros/Macros.h.
// Only the subset Crucible actually uses.
//
// Branch hints use C++20 [[likely]]/[[unlikely]] attributes directly
// at usage sites instead of macros wrapping __builtin_expect.

#include <version>
#include <type_traits>

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

#define CRUCIBLE_INLINE    __attribute__((always_inline)) inline
#define CRUCIBLE_NOINLINE  __attribute__((noinline))
#define CRUCIBLE_API       __attribute__((visibility("default")))

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
