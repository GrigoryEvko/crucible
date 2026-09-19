// The value constructor is private, so a Sanitized tag cannot be written
// around a value nobody sanitized.  The compiler names the constructor
// and says it is private within this context.

#include <fixy/Tagged.h>

namespace tags = ::fixy::tags;

int main() {
    fixy::Tagged<int, tags::source::Sanitized> forged{-999};
    return forged.value();
}
