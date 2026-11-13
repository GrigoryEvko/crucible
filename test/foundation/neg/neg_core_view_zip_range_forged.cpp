// foundation/core/Region.h names detail::ZipRange as a friend of View, and
// it also defines the range.  A unit that includes Region.h therefore cannot
// give the range a definition of its own and read the private first pointer
// of a View: the second definition is a redefinition.

#include <foundation/core/Region.h>

template <class First, class Second, ::foundation::brand::IsBrand Brand>
class foundation::core::detail::ZipRange {
public:
    bool is_null = false;
    constexpr ZipRange(View<First> first, View<Second> second) noexcept
        : is_null{first.first_ == nullptr || second.first_ == nullptr} {}
};

int main() { return 0; }
