#pragma once

// Standalone replacements for c10/macros/Macros.h.
// Only the subset Crucible actually uses.
//
// Branch hints use C++20 [[likely]]/[[unlikely]] attributes directly
// at usage sites instead of macros wrapping __builtin_expect.

#if defined(_MSC_VER)
  #define CRUCIBLE_INLINE __forceinline
  #define CRUCIBLE_API __declspec(dllexport)
#else
  #define CRUCIBLE_INLINE __attribute__((always_inline)) inline
  #define CRUCIBLE_API __attribute__((visibility("default")))
#endif
