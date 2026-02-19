#pragma once

// Standalone replacements for c10/macros/Macros.h.
// Only the subset Crucible actually uses.
//
// Branch hints use C++20 [[likely]]/[[unlikely]] attributes directly
// at usage sites instead of macros wrapping __builtin_expect.

#include <version>

#if defined(_MSC_VER)
  #define CRUCIBLE_INLINE __forceinline
  #define CRUCIBLE_API __declspec(dllexport)
#else
  #define CRUCIBLE_INLINE __attribute__((always_inline)) inline
  #define CRUCIBLE_API __attribute__((visibility("default")))
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

// ── GCC 16 exclusive: P2996 static reflection ──
// Requires -freflection flag. Enables reflect_hash<T>, reflect_print<T>.
#if defined(__cpp_impl_reflection) && __cpp_impl_reflection >= 202506L
  #define CRUCIBLE_HAS_REFLECTION 1
#else
  #define CRUCIBLE_HAS_REFLECTION 0
#endif

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
