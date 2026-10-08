// A second file of the language.  utils/assay/guard/legacy/check_plugin.py compiles
// language_other_user.cpp, which includes this header, in report mode, and
// each body below gives one error.  The call of memset names a function of
// the C library.  The row of std::nothrow has `until` and `in`, and it admits
// the name only in fixy/high/Language.h.

#pragma once

#include <cstring>
#include <new>

namespace fixy::language_test {

inline void clear_byte(unsigned char* bytes) { std::memset(bytes, 0, 1); }

inline void* allocate_other_byte() noexcept { return ::operator new(1, std::nothrow); }

}  // namespace fixy::language_test
