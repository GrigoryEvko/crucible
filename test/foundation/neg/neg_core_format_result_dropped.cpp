// The Result of format() says whether the text fit its buffer.  A call that
// drops the Result would read a truncated text as a whole one, so the drop
// stops the build.

#include <foundation/core/Format.h>

int main() {
    ::foundation::core::FixedText<4> text;
    ::foundation::core::format(text, "the queue holds {} items", 3);
    return 0;
}
