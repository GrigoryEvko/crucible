// A template argument that points at the static invoker of a closure
// takes no stable id.
//
// A value gives no reflection of the function it points at, so the walk
// reads the printed value, and the printed value names the closure.

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace invoker_value {
template <auto Function>
struct Holds {};
inline constexpr int (*twice)(int) = +[](int value) { return value * 2; };
inline constexpr std::uint64_t refused_id = ::foundation::reflect::stable_type_id(^^Holds<twice>);
}  // namespace invoker_value

int main() { return invoker_value::refused_id == 0 ? 1 : 0; }
