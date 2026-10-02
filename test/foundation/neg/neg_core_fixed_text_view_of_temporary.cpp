// A view of a FixedText borrows its buffer.  A temporary FixedText ends
// with the full expression, and the view would dangle, so view() refuses
// it.

#include <foundation/core/Text.h>

int main() {
    auto const view = ::foundation::core::FixedText<8>{}.view();
    return static_cast<int>(view.size());
}
