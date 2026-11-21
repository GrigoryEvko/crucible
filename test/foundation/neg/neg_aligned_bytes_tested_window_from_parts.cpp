// A tested window holds the three facts that a start of the lifetime of its
// elements needs, so the start checks nothing.  Only the tests of the frame
// door make one with an element.  The constructor from a first byte and a
// count is private, so a window that no test passed gives no tested window.

#include <foundation/AlignedBytes.h>

#include <cstddef>

int main() {
    ::foundation::detail::TestedWindow<float> const forged{nullptr, std::size_t{4}};
    static_cast<void>(forged);
    return 0;
}
