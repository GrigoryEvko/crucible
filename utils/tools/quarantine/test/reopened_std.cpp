// A quarantined file that opens library namespaces again.  The walk of main
// skipped each library namespace, and each specialization of a library
// template, so the objects below gave no finding.

#include <cstddef>
#include <functional>
#include <vector>

struct Reopened {
    int value = 0;
};

namespace std {
inline int* reopened_pointer = nullptr;
}  // namespace std

template <>
struct std::hash<Reopened> {
    std::vector<int> cache;
    std::size_t operator()(Reopened const& reopened) const noexcept { return static_cast<std::size_t>(reopened.value); }
};

namespace __gnu_cxx {
inline int* gnu_pointer = nullptr;
}  // namespace __gnu_cxx
