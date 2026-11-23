// A function that takes the name of mint_foreign_text in its own namespace
// calls the door of the mint to make a ForeignText with no check of the count
// or the null pointer.  The friend is foundation::core::mint_foreign_text, so
// the member of the door is private here.

#include <foundation/core/Region.h>

namespace lookalike {

[[nodiscard]] inline foundation::core::ForeignText mint_foreign_text() noexcept {
    return foundation::core::ForeignTextDoor::text_(static_cast<char const*>(nullptr), 4);
}

}  // namespace lookalike

int main() { return static_cast<int>(lookalike::mint_foreign_text().characters().size()); }
