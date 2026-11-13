// foundation/core/Text.h declares window() of TextView and defines it.  A
// unit that includes Text.h therefore cannot give the member a definition of
// its own with the access of TextView and read the private members of a
// text: the compiler refuses the second definition.  The first definition
// deduced the return type, so the second matches no declaration.

#include <foundation/core/Text.h>

#include <cstddef>

namespace foundation::core {

constexpr auto TextView::window(std::size_t offset, std::size_t count) const noexcept { return size_ + offset + count; }

}  // namespace foundation::core

int main() { return 0; }
