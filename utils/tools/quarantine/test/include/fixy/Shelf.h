// A stand-in for a header of the substrate.  The plugin test treats the
// directory that holds include/fixy/ as the source root, so this header is
// substrate code, and its uses of the library are not findings.

#pragma once

#include <cstring>
#include <source_location>
#include <vector>

namespace fixy {

template <class T>
struct Shelf {
    T* first = nullptr;
    int counts[4] = {};
    std::vector<T> items;

    void fill(const T& value) {
        items.push_back(value);
        std::memset(counts, 0, sizeof counts);
        first = new T(value);
        delete first;
        first = nullptr;
    }
};

inline void clear_bytes(char* bytes, unsigned count) { std::memset(bytes, 0, count); }

// The compiler copies a default argument into each call.  The substrate spells
// these two, so a call of them in a quarantined file names nothing.  The copy
// of an immediate invocation has the location of the call.
inline unsigned long limit_of(const char* text, unsigned long limit = std::strlen(std::strerror(0))) {
    return std::strlen(text) < limit ? limit : 0;
}

inline unsigned line_of(std::source_location where = std::source_location::current()) { return where.line(); }

template <class T>
unsigned line_of_value(const T&, std::source_location where = std::source_location::current()) {
    return where.line();
}

}  // namespace fixy
