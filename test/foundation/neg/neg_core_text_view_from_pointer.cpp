// A pointer carries no count and no proof that the characters outlive the
// view, so it does not become a TextView, also when it points at a literal.

#include <foundation/core/Text.h>

int main() {
    char const* text = "the queue is full";
    ::foundation::core::TextView const view{text};
    return static_cast<int>(view.size());
}
