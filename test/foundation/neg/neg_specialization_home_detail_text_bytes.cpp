// A file outside Text.h writes an explicit specialization of the template
// detail::text_bytes_, which FixedText befriends.  The body gives a pointer
// that no text holds, so a format writes through it.  The rule
// specialization-home refuses the specialization in each build, because a
// function template of the tree gets an explicit specialization only in its
// own file.

#include <foundation/core/Text.h>

template <>
constexpr char* foundation::core::detail::text_bytes_<8>(FixedText<8>&) noexcept {
    return nullptr;
}

int main() { return 0; }
