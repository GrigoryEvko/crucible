// foundation/core/Region.h names detail::EnumerateRange as a friend of View,
// and it also defines the range.  A unit that includes Region.h therefore
// cannot give the range a definition of its own and read the private first
// pointer of a View: the second definition is a redefinition.

#include <foundation/core/Region.h>

#include <cstddef>

template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand>
class foundation::core::detail::EnumerateRange {
public:
    bool is_null = false;
    constexpr explicit EnumerateRange(View<Element, Count> elements) noexcept : is_null{elements.first_ == nullptr} {}
};

int main() { return 0; }
