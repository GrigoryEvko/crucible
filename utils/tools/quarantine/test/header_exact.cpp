// The test table admits <type_traits>.  The entry admits the file that an
// #include of that exact name enters, and not experimental/type_traits.

#include <experimental/type_traits>
#include <type_traits>

using HeaderExactKept = std::is_same<int, int>;
using HeaderExactProbe = std::experimental::nonesuch;
