// A caller reads the pointer of a View through first_of_ with an empty brace
// in place of the tag.  The constructor of the tag is explicit, so each call
// of first_of_ names detail::BaseLayerRead, and the rule owners keeps a
// caller outside the base out.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 2> const values{1, 2};
    static_cast<void>(first_of_({}, values.view()));
    return 0;
}
