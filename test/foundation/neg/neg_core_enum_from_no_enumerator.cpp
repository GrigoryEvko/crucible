// An enumeration with no enumerator is a strong integer: no enumerator can
// be the result of enum_from.  enum_from refuses such an enumeration.

#include <foundation/core/Scalar.h>

#include <cstdint>

enum class Strong : std::uint8_t {
};

int main() { return ::foundation::core::enum_from<Strong>(1).is_some() ? 0 : 1; }
