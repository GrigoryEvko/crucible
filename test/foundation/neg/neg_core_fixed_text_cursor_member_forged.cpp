// foundation/core/Text.h declares begin() of FixedText and defines it in the
// class.  A unit that includes Text.h therefore cannot give the member a
// definition of its own with the access of FixedText and read the private
// members of a buffer: the second definition is a redefinition.

#include <foundation/core/Text.h>

#include <cstddef>

namespace foundation::core {

template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity <= static_cast<std::size_t>(__PTRDIFF_MAX__))
constexpr ViewCursor<char const> FixedText<Capacity>::begin() const& noexcept {
    return view().end();
}

}  // namespace foundation::core

int main() { return 0; }
