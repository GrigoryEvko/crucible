// The library names that the type walks of main did not read: a template
// argument of an admitted library class, a function type, a default argument
// that no call uses and the default of a non-type template parameter.

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

int walk_marker(std::type_identity<std::string> marker) { return static_cast<int>(sizeof marker); }

using walk_handler = void(std::string const&);
using walk_pointer = int (*)(std::vector<int>);

unsigned long walk_default(unsigned long count = std::strlen("abc")) { return count; }

template <auto Function = &std::strlen>
struct WalkHolder {};

void walk_callback(void (*callback)(std::string)) { static_cast<void>(callback); }
