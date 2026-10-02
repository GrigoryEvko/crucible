// A file of the language that calls a function of the C library.  The rule of
// the language reads the type of each declaration and each include, and not a
// call, so the call gives no finding.  KNOWN_GAPS of check_plugin.py pins it.

#pragma once

#include <cstring>

namespace fixy::language_test {

inline void gap_clear(unsigned char* bytes) { std::memset(bytes, 0, 1); }

}  // namespace fixy::language_test
