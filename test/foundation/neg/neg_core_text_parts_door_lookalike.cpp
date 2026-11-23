// A function that takes the name and the shape of mint_c_text in its own
// namespace calls the door of the mints of foundation/core/Text.h to make a
// CText with no zero.  The friend is foundation::core::mint_c_text, so the
// member of the door is private here.

#include <foundation/core/Text.h>

namespace lookalike {

[[nodiscard]] constexpr foundation::core::CText mint_c_text(foundation::core::TextView text) noexcept {
    return foundation::core::detail::TextPartsDoor::c_text_(foundation::core::detail::text_first_(text), text.size());
}

}  // namespace lookalike

int main() { return static_cast<int>(lookalike::mint_c_text(foundation::core::TextView{"abc"}).size()); }
