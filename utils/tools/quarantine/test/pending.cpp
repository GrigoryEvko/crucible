// The uses that an admit row with `until FAMILY` admits.  check_plugin.py
// compiles this quarantined file in report mode with rules.txt.  Each such use
// is a finding of the kind of its row, and a use that a restriction of the
// row refuses is a std_entity.

#include <bit>
#include <limits>
#include <source_location>
#include <utility>

unsigned long pending_bits(double value) { return std::bit_cast<unsigned long>(value); }

bool pending_flag(unsigned char byte) { return std::bit_cast<bool>(byte); }

template <class T>
unsigned long pending_bits_of(T value) {
    return std::bit_cast<unsigned long>(value);
}

template <class T>
T pending_from_bits(unsigned long bits) {
    return std::bit_cast<T>(bits);
}

unsigned pending_line(std::source_location where) {
    return where.line();
}

int pending_most() { return std::numeric_limits<int>::max(); }

template <class T>
T pending_most_of() {
    return std::numeric_limits<T>::max();
}

[[noreturn]] void pending_stop() { std::unreachable(); }

// A pointer holds no bool, also when its target is dependent.
template <class T>
T* pending_pointer(unsigned long bits) {
    return std::bit_cast<T*>(bits);
}
