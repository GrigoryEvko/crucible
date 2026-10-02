// A file of the language: language.txt holds it in a language row with the
// layer low, and its own layer is high.  check_plugin.py compiles
// language_user.cpp, which includes this header, in report mode with no plant
// and with each PLANT_LANGUAGE_ macro.  Each plant is an error, and the header
// with no plant has no finding.

#pragma once

#include <cstddef>
#include <source_location>
#include <vector>

#if defined(PLANT_LANGUAGE_HIGH_HEADER)
#include <cstdint>
#endif

#if defined(PLANT_LANGUAGE_SYSTEM_HEADER)
#include <unistd.h>
#endif

#if defined(PLANT_LANGUAGE_DOOR_HEADER)
#include <sys/socket.h>
#endif

namespace fixy::language_test {

// The base holds the raw pointers and the arrays of its families, and the row
// with `in` admits std::nullptr_t in this file.
struct Plain {
    int* raw = nullptr;
    int values[2]{};
    std::nullptr_t none = nullptr;
};

inline int plain_sum(Plain const& plain) {
    int const first = plain.values[0];
    return first + plain.values[1];
}

#if defined(PLANT_LANGUAGE_MEMBER)
struct Holder {
    std::vector<int> items;
};
#endif

#if defined(PLANT_LANGUAGE_LOCAL)
inline unsigned long local_count() {
    std::vector<int> items{};
    return items.size();
}
#endif

#if defined(PLANT_LANGUAGE_PARAMETER)
inline unsigned line_of(std::source_location where) { return where.line(); }
#endif

#if defined(PLANT_LANGUAGE_ALIAS)
using Items = std::vector<int>;
#endif

#if defined(PLANT_LANGUAGE_TYPEDEF)
inline std::ptrdiff_t distance = 0;
#endif

#if defined(PLANT_LANGUAGE_BASE)
struct Derived : std::vector<int> {};
#endif

#if defined(PLANT_LANGUAGE_TEMPLATE)
template <class T>
struct Box {
    std::vector<T> items;
};
#endif

}  // namespace fixy::language_test
