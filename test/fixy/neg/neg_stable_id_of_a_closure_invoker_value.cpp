// A template argument that points at the static invoker of a closure
// takes no stable id.
//
// A value gives no reflection of the function it points at, so the walk
// reads the printed value, and the printed value names the closure.

#include <foundation/reflect/Hash.h>

namespace invoker_value {
template <auto Function>
struct Holds {};
inline constexpr int (*twice)(int) = +[](int value) { return value * 2; };
}  // namespace invoker_value

int main() { return ::foundation::reflect::stable_type_id<invoker_value::Holds<invoker_value::twice>> == 0 ? 1 : 0; }
