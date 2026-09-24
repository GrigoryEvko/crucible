// The bit operators are hidden friends of each instantiation, so a
// Bits over one enum shares no operator with a Bits over another.  A
// permission word and a colour word have the same underlying shape and
// would fold silently if the operators were free templates.

#include <fixy/Bits.h>

#include <cstdint>

namespace {

enum class Perm : std::uint8_t {
    Read = 0x01,
    Write = 0x02
};
enum class Colour : std::uint8_t {
    Red = 0x01,
    Blue = 0x02
};

}  // namespace

int main() {
    const fixy::Bits<Perm> p{Perm::Read};
    const fixy::Bits<Colour> c{Colour::Blue};
    return (p | c).raw();
}
