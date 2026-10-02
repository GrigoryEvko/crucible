// format() gives a view of its FixedText.  A temporary FixedText ends with
// the full expression, and the view would dangle, so format() refuses it.

#include <foundation/core/Format.h>

int main() {
    auto const result = ::foundation::core::format(::foundation::core::FixedText<16>{}, "the queue holds {} items", 3);
    return result.is_ok() ? 0 : 1;
}
