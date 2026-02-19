#pragma once

// Standalone replacements for c10/macros/Macros.h.
// Only the subset Crucible actually uses.

#if defined(_MSC_VER)
  #define CRUCIBLE_INLINE __forceinline
  #define CRUCIBLE_UNLIKELY(x) (x)
  #define CRUCIBLE_LIKELY(x) (x)
  #define CRUCIBLE_API __declspec(dllexport)
#else
  #define CRUCIBLE_INLINE __attribute__((always_inline)) inline
  #define CRUCIBLE_UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define CRUCIBLE_LIKELY(x) __builtin_expect(!!(x), 1)
  #define CRUCIBLE_API __attribute__((visibility("default")))
#endif
